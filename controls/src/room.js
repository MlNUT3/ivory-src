// Hub Durable Object: accounts, sessions (grim tokens), profiles, tags, chat,
// and per-user feature config. All state lives in SQLite-backed DO storage.
//
//   POST   /api/register {username,password} -> {token, user}
//   POST   /api/login    {username,password} -> {token, user}
//   GET    /api/me                           -> user
//   PATCH  /api/me       {username?,display?,bio?,avatar?}
//   GET    /api/users                        -> [user]
//   PATCH  /api/users/:id {tag}              (owner only)
//   GET    /api/config                       -> {rev,features,params}
//   POST   /api/config   {features?,params?}
//   GET    /api/chat                         -> [message]
//   WS     /api/ws                           live config + chat + presence
//
// Token sent as header `x-auth` or `?token=`.

const NAME_RE = /^[A-Za-z0-9_]{3,20}$/;
const ITERS = 100000;
const MAX_MSG = 300;
const MSG_LEN = 500;
const AL = 'abcdefghijklmnopqrstuvwxyz0123456789';
const ALLOWED_TAGS = ['Member', 'Close Friends', 'Owner'];

/* --------------------------------------------------------------- crypto */

const enc = new TextEncoder();

function bytesToHex(b) { return [...b].map((x) => x.toString(16).padStart(2, '0')).join(''); }
function hexToBytes(h) { const a = new Uint8Array(h.length / 2); for (let i = 0; i < a.length; i++) a[i] = parseInt(h.substr(i * 2, 2), 16); return a; }
function randHex(n) { return bytesToHex(crypto.getRandomValues(new Uint8Array(n))); }

async function hashPassword(password, saltHex) {
  const key = await crypto.subtle.importKey('raw', enc.encode(password), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', salt: hexToBytes(saltHex), iterations: ITERS, hash: 'SHA-256' }, key, 256);
  return bytesToHex(new Uint8Array(bits));
}

function seg() {
  const r = crypto.getRandomValues(new Uint8Array(4));
  let s = '';
  for (const b of r) s += AL[b % AL.length];
  return s;
}
function newToken() { return 'grim-' + seg() + '-' + seg(); }

/* --------------------------------------------------------------- helpers */

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
  });
}

function publicUser(u, online) {
  return {
    id: u.id, username: u.username, display: u.display || u.username,
    bio: u.bio || '', av: u.av || 0, tag: ALLOWED_TAGS.includes(u.tag) ? u.tag : 'Member',
    role: u.role, online: !!online,
    hwidShort: u.hwid ? String(u.hwid).slice(0, 8) : '',
  };
}

function sanitize(s, max) {
  return String(s == null ? '' : s).replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, max);
}

/* --------------------------------------------------------------- object */

export class Room {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  async users() { return (await this.state.storage.get('users')) || {}; }
  async saveUsers(u) { await this.state.storage.put('users', u); }

  async userCfg(uid) {
    return (await this.state.storage.get('cfg:' + uid)) || { rev: 1, features: {}, params: {} };
  }

  async onlineSet() {
    const set = new Set();
    for (const ws of this.state.getWebSockets()) {
      try { const a = ws.deserializeAttachment(); if (a && a.uid) set.add(a.uid); } catch { /* ignore */ }
    }
    return set;
  }

  async byToken(token) {
    if (!token) return null;
    const users = await this.users();
    for (const id in users) if (users[id].token === token) return users[id];
    return null;
  }

  broadcastAll(msg) {
    const s = JSON.stringify(msg);
    for (const ws of this.state.getWebSockets()) {
      try { ws.send(s); } catch { /* dropped */ }
    }
  }

  sendTo(uid, msg) {
    const s = JSON.stringify(msg);
    for (const ws of this.state.getWebSockets()) {
      try { const a = ws.deserializeAttachment(); if (a && a.uid === uid) ws.send(s); } catch { /* ignore */ }
    }
  }

  /* ----------------------------------------------------------- fetch */

  async fetch(request) {
    const url = new URL(request.url);
    const path = url.pathname;
    const method = request.method;
    const token = request.headers.get('x-auth') || url.searchParams.get('token') || '';

    // ---- no-auth endpoints ----
    if (path === '/api/register' && method === 'POST') return this.register(await request.json().catch(() => ({})));
    if (path === '/api/login' && method === 'POST') return this.login(await request.json().catch(() => ({})));
    if (path === '/api/login-token' && method === 'POST') return this.loginToken(await request.json().catch(() => ({})));

    // ---- websocket ----
    if (path === '/api/ws') {
      const me = await this.byToken(token);
      if (!me) return json({ error: 'unauthorized' }, 401);
      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      server.serializeAttachment({ uid: me.id });
      this.state.acceptWebSocket(server);

      // hwid gate - only enforced for connections that present one (the
      // loader exe). Browsers and the cheat's own client connect bare.
      const hwidRaw = (url.searchParams.get('hwid') || '').toLowerCase();
      if (/^[a-f0-9]{16,64}$/.test(hwidRaw)) {
        if (me.hwid && me.hwid !== hwidRaw) {
          server.send(JSON.stringify({ type: 'blocked', reason: 'hwid' }));
          server.close(4003, 'hwid');
          return new Response(null, { status: 101, webSocket: client });
        }
        if (!me.hwid) {
          me.hwid = hwidRaw;
          const users0 = await this.users();
          users0[me.id] = me;
          await this.saveUsers(users0);
          this.broadcastAll({ type: 'users' });
        }
      }

      const online = await this.onlineSet();
      const users = await this.users();
      server.send(JSON.stringify({ type: 'config', ...(await this.userCfg(me.id)) }));
      server.send(JSON.stringify({ type: 'chat', messages: await this.history() }));
      server.send(JSON.stringify({ type: 'presence', users: Object.values(users).map((u) => publicUser(u, online.has(u.id) || u.id === me.id)) }));
      server.send(JSON.stringify({ type: 'players', list: await this.players(me.id) }));
      server.send(JSON.stringify({ type: 'commands', list: await this.cmds(me.id) }));
      server.send(JSON.stringify({ type: 'logs', text: await this.logs(me.id) }));
      server.send(JSON.stringify({ type: 'cfgs', ...(await this.cfgs(me.id)) }));
      return new Response(null, { status: 101, webSocket: client });
    }

    // ---- public: profile images ----
    const mAva = path.match(/^\/api\/avatar\/([^/]+)$/);
    if (mAva && method === 'GET') {
      const stored = await this.state.storage.get('ava:' + mAva[1]);
      const m = stored && /^data:(image\/[a-z+]+);base64,(.*)$/.exec(stored);
      if (!m) return new Response('', { status: 404 });
      const bin = Uint8Array.from(atob(m[2]), (c) => c.charCodeAt(0));
      return new Response(bin, { headers: { 'content-type': m[1], 'cache-control': 'public, max-age=31536000, immutable' } });
    }

    // ---- everything below needs auth ----
    const me = await this.byToken(token);
    if (!me) return json({ error: 'unauthorized' }, 401);

    if (path === '/api/me' && method === 'GET') {
      const online = await this.onlineSet();
      return json(publicUser(me, online.has(me.id)));
    }

    if (path === '/api/me' && (method === 'PATCH' || method === 'PUT')) {
      const b = await request.json().catch(() => ({}));
      const users = await this.users();
      if (b.username != null) {
        const uname = sanitize(b.username, 20);
        if (!NAME_RE.test(uname)) return json({ error: 'invalid username' }, 400);
        const lower = uname.toLowerCase();
        for (const id in users) if (id !== me.id && users[id].username.toLowerCase() === lower) return json({ error: 'username taken' }, 409);
        me.username = uname;
      }
      if (b.display != null) me.display = sanitize(b.display, 24);
      if (b.bio != null) me.bio = sanitize(b.bio, 160);
      if (b.avatar != null) {
        const a = String(b.avatar);
        if (a === '') { await this.state.storage.delete('ava:' + me.id); me.av = 0; }
        else if (/^data:image\/(png|jpe?g|webp|gif);base64,[A-Za-z0-9+/=]+$/.test(a) && a.length <= 300000) {
          await this.state.storage.put('ava:' + me.id, a);
          me.av = (me.av || 0) + 1;
        } else return json({ error: 'invalid image' }, 400);
      }
      users[me.id] = me;
      await this.saveUsers(users);
      this.broadcastAll({ type: 'users' });
      return json(publicUser(me, true));
    }

    if (path === '/api/users' && method === 'GET') {
      // the users area is owner + close friends only
      if (me.role !== 'owner' && me.tag !== 'Close Friends') return json({ error: 'forbidden' }, 403);
      const online = await this.onlineSet();
      const users = await this.users();
      return json(Object.values(users).map((u) => publicUser(u, online.has(u.id))).sort((a, b) => a.username.localeCompare(b.username)));
    }

    const mUser = path.match(/^\/api\/users\/([^/]+)$/);
    if (mUser && method === 'DELETE') {
      if (me.role !== 'owner') return json({ error: 'forbidden' }, 403);
      const users = await this.users();
      const t = users[mUser[1]];
      if (!t) return json({ error: 'no such user' }, 404);
      if (t.role === 'owner' || t.id === me.id) return json({ error: 'cannot delete that account' }, 403);
      delete users[mUser[1]];
      await this.saveUsers(users);
      for (const k of ['cfg:', 'cmd:', 'players:', 'logs:', 'cfgs:', 'plocal:'])
        await this.state.storage.delete(k + mUser[1]);
      this.broadcastAll({ type: 'users' });
      return json({ ok: true });
    }
    if (mUser && (method === 'PATCH' || method === 'PUT')) {
      const b = await request.json().catch(() => ({}));
      const users = await this.users();
      const t = users[mUser[1]];
      if (!t) return json({ error: 'no such user' }, 404);
      const isOwner = me.role === 'owner';
      const isSelf = me.id === t.id;
      // members may only touch their own row, and only to reset their hwid
      if (!isOwner && !(isSelf && b.resetHwid)) return json({ error: 'forbidden' }, 403);
      if (b.tag != null && isOwner) {
        const tag = sanitize(b.tag, 20);
        // the Owner tag belongs to the owner account only
        const okTag = ALLOWED_TAGS.includes(tag) && (tag !== 'Owner' || t.role === 'owner');
        t.tag = okTag ? tag : 'Member';
      }
      if (b.resetHwid) {
        if (!isOwner) {
          const last = Number(t.lastHwidReset) || 0;
          if (Date.now() - last < 2 * 24 * 3600 * 1000) return json({ error: 'hwid resets are limited to once every 2 days' }, 429);
        }
        t.hwid = null;
        t.lastHwidReset = Date.now();
      }
      await this.saveUsers(users);
      this.broadcastAll({ type: 'users' });
      return json(publicUser(t, false));
    }

    if (path === '/api/invites' && method === 'POST') {
      if (me.role !== 'owner') return json({ error: 'forbidden' }, 403);
      const b = await request.json().catch(() => ({}));
      const n = Math.max(1, Math.min(20, Number(b.count) || 1));
      const invites = await this.invites();
      const users = await this.users();
      const out = [];
      for (let i = 0; i < n; i++) {
        let t = newToken();
        while (invites.some((x) => x.token === t) || Object.values(users).some((u) => u.token === t)) t = newToken();
        invites.push({ token: t, used: false, created: Date.now() });
        out.push(t);
      }
      await this.state.storage.put('invites', invites);
      return json({ tokens: out });
    }
    if (path === '/api/invites' && method === 'GET') {
      if (me.role !== 'owner') return json({ error: 'forbidden' }, 403);
      return json(await this.invites());
    }

    if (path === '/api/config' && method === 'GET') return json(await this.userCfg(me.id));

    /* ------------------------------------------------------- direct messages */

    if (path === '/api/dms' && method === 'GET') return this.dmOverview(me);
    if (path === '/api/dm-thread' && method === 'GET') {
      const other = sanitize(url.searchParams.get('from') || '', 32);
      const th = await this.dmThread(me.id, other);
      const users = await this.users();
      return json({ msgs: th || [], display: (users[other] || {}).display || (users[other] || {}).username || '' });
    }
    if (path === '/api/dm' && method === 'POST') return this.dmSend(me, await request.json().catch(() => ({})));
    if (path === '/api/dm-accept' && method === 'POST') return this.dmAccept(me, await request.json().catch(() => ({})));
    if (path === '/api/dm-decline' && method === 'POST') return this.dmDecline(me, await request.json().catch(() => ({})));
    if (path === '/api/config' && method === 'POST') return this.setConfig(me, await request.json().catch(() => ({})));

    if (path === '/api/chat' && method === 'GET') return json(await this.history());

    if (path === '/api/players' && method === 'GET') return json(await this.players(me.id));
    if (path === '/api/cmd' && method === 'POST') {
      const b = await request.json().catch(() => ({}));
      await this.pushCmd(me, b.cmd);
      return json({ ok: true });
    }

    return json({ error: 'not found' }, 404);
  }

  /* ------------------------------------------------------- auth ops */

  async invites() { return (await this.state.storage.get('invites')) || []; }

  async loginToken(b) {
    const t = sanitize(b && b.token, 40).toLowerCase();
    if (!t) return json({ error: 'missing token' }, 400);
    const found = await this.byToken(t);
    if (!found) return json({ error: 'invalid access token' }, 401);
    return json({ token: found.token, user: publicUser(found, true) });
  }

  async register(b) {
    const username = sanitize(b.username, 20);
    const password = String(b.password || '');
    if (!NAME_RE.test(username)) return json({ error: 'username must be 3-20 letters/numbers/_' }, 400);
    if (password.length < 6) return json({ error: 'password must be at least 6 characters' }, 400);

    const users = await this.users();
    const lower = username.toLowerCase();
    for (const id in users) if (users[id].username.toLowerCase() === lower) return json({ error: 'username taken' }, 409);

    const id = randHex(8);
    const salt = randHex(16);
    let token = newToken();
    while (Object.values(users).some((u) => u.token === token)) token = newToken();

    // required access token: the owner pre-generates invite tokens, people
    // can only make an account with one. The account is created bound to
    // that token, so the exe and the token sign-in work with it immediately.
    {
      const t = String(b.token || '').trim().toLowerCase();
      const invites = await this.invites();
      const inv = t ? invites.find((x) => x.token === t && !x.used) : null;
      if (!inv) return json({ error: 'invalid access token' }, 403);
      if (Object.values(users).some((u) => u.token === t)) return json({ error: 'invalid access token' }, 409);
      token = t;
      inv.used = true;
      inv.usedBy = username;
      await this.state.storage.put('invites', invites);
    }

    const user = {
      id, username, salt, hash: await hashPassword(password, salt),
      display: username, bio: '', av: 0, tag: 'Member',
      role: Object.keys(users).length === 0 ? 'owner' : 'member',
      token, created: Date.now(),
    };
    users[id] = user;
    await this.saveUsers(users);
    await this.state.storage.put('cfg:' + id, { rev: 1, features: {}, params: {} });
    this.broadcastAll({ type: 'users' });
    return json({ token, user: publicUser(user, true) });
  }

  async login(b) {
    const username = sanitize(b.username, 20).toLowerCase();
    const password = String(b.password || '');
    const users = await this.users();
    let found = null;
    for (const id in users) if (users[id].username.toLowerCase() === username) { found = users[id]; break; }
    if (!found) return json({ error: 'invalid credentials' }, 401);
    const hash = await hashPassword(password, found.salt);
    if (hash !== found.hash) return json({ error: 'invalid credentials' }, 401);
    return json({ token: found.token, user: publicUser(found, true) });
  }

  /* ------------------------------------------------------- config ops */

  async setConfig(me, b) {
    const cfg = await this.userCfg(me.id);
    let changed = false;
    if (b && typeof b.features === 'object' && b.features) {
      for (const [k, v] of Object.entries(b.features)) {
        const val = !!v;
        if (cfg.features[k] !== val) { cfg.features[k] = val; changed = true; }
      }
    }
    if (b && typeof b.params === 'object' && b.params) {
      for (const [k, v] of Object.entries(b.params)) {
        const val = (typeof v === 'number' && Number.isFinite(v)) ? v : sanitize(v, 64);
        if (cfg.params[k] !== val) { cfg.params[k] = val; changed = true; }
      }
    }
    if (changed) {
      cfg.rev += 1;
      await this.state.storage.put('cfg:' + me.id, cfg);
      this.sendTo(me.id, { type: 'config', ...cfg });
    }
    return json(cfg);
  }

  /* ------------------------------------------------ commands / players */

  async pushCmd(me, cmd) {
    const c = sanitize(cmd, 40);
    if (!c) return;
    const key = 'cmd:' + me.id;
    const list = (await this.state.storage.get(key)) || [];
    const n = (list.length ? list[list.length - 1].n : 0) + 1;
    list.push({ n, cmd: c });
    while (list.length > 50) list.shift();
    await this.state.storage.put(key, list);
    this.sendTo(me.id, { type: 'command', n, cmd: c });
  }

  async cmds(uid) { return (await this.state.storage.get('cmd:' + uid)) || []; }

  async players(uid) { return (await this.state.storage.get('players:' + uid)) || []; }

  async plocal(uid) { return (await this.state.storage.get('plocal:' + uid)) || null; }

  async setPlayers(me, list, local) {
    const clean = Array.isArray(list) ? list.slice(0, 100).map((p) => ({
      uid: sanitize(p && p.uid, 32), name: sanitize(p && p.name, 24),
      dn: sanitize(p && p.dn, 24), acc: sanitize(p && p.acc, 24),
      health: Number(p && p.health) || 0, mh: Number(p && p.mh) || 0,
      team: !!(p && p.team), dist: Number(p && p.dist) || 0,
      x: Number(p && p.x) || 0, y: Number(p && p.y) || 0, z: Number(p && p.z) || 0,
    })) : [];
    await this.state.storage.put('players:' + me.id, clean);
    if (local && typeof local === 'object')
      await this.state.storage.put('plocal:' + me.id, { x: Number(local.x) || 0, y: Number(local.y) || 0, z: Number(local.z) || 0 });
    this.sendTo(me.id, { type: 'players', list: clean, local: await this.plocal(me.id) });
  }

  async cfgs(uid) { return (await this.state.storage.get('cfgs:' + uid)) || { list: [], auto: '' }; }

  async setCfgs(me, obj) {
    const list = obj && obj.list;
    const clean = Array.isArray(list) ? list.slice(0, 100).map((n) => sanitize(n, 40)).filter(Boolean) : [];
    const auto = sanitize(obj && obj.auto, 40);
    const payload = { list: clean, auto: clean.includes(auto) ? auto : '' };
    await this.state.storage.put('cfgs:' + me.id, payload);
    this.sendTo(me.id, { type: 'cfgs', ...payload });
  }

  async logs(uid) { return (await this.state.storage.get('logs:' + uid)) || ''; }

  async setLogs(me, text) {
    const t = String(text == null ? '' : text).slice(0, 24000);
    await this.state.storage.put('logs:' + me.id, t);
    this.sendTo(me.id, { type: 'logs', text: t });
  }

  async history() { return (await this.state.storage.get('messages')) || []; }

  /* ------------------------------------------------------ direct messages */

  dmKey(a, b) { const [x, y] = [a, b].sort(); return 'dmth:' + x + '_' + y; }

  async dmThread(a, b) { return (await this.state.storage.get(this.dmKey(a, b))) || null; }

  async dmOverview(me) {
    const reqs = (await this.state.storage.get('dmreq:' + me.id)) || [];
    const users = await this.users();
    const requests = reqs.map((r) => ({
      from: r.from, ts: r.ts,
      display: (users[r.from] || {}).display || (users[r.from] || {}).username || 'unknown',
    }));
    const threads = [];
    for (const id in users) {
      if (id === me.id) continue;
      const th = await this.dmThread(me.id, id);
      if (th && th.length) threads.push({ uid: id, display: (users[id] || {}).display || id, count: th.length, ts: th[th.length - 1].ts });
    }
    return json({ requests, threads });
  }

  async dmSend(me, b) {
    const to = sanitize(b && b.to, 32);
    const body = sanitize(b && b.body, MSG_LEN);
    if (!to || !body) return json({ error: 'bad request' }, 400);
    if (to === me.id) return json({ error: "you can't message yourself" }, 400);
    const users = await this.users();
    if (!users[to]) return json({ error: 'no such user' }, 404);

    const th = await this.dmThread(me.id, to);
    if (th) {
      const msg = { uid: me.id, body, ts: Date.now() };
      th.push(msg);
      while (th.length > 200) th.shift();
      await this.state.storage.put(this.dmKey(me.id, to), th);
      this.sendTo(to, { type: 'dm', from: me.id, body, ts: msg.ts });
      this.sendTo(me.id, { type: 'dm', from: me.id, body, ts: msg.ts });
      return json({ ok: true, accepted: true });
    }

    // no accepted thread yet: park a request. The recipient sees nothing at
    // all until they accept - not even that a message exists.
    const reqs = (await this.state.storage.get('dmreq:' + to)) || [];
    if (!reqs.some((r) => r.from === me.id)) {
      reqs.push({ from: me.id, ts: Date.now() });
      while (reqs.length > 50) reqs.shift();
      await this.state.storage.put('dmreq:' + to, reqs);
      this.sendTo(to, { type: 'dmreq', from: me.id });
    }
    return json({ ok: true, accepted: false });
  }

  async dmAccept(me, b) {
    const from = sanitize(b && b.from, 32);
    if (!from) return json({ error: 'bad request' }, 400);
    const reqs = (await this.state.storage.get('dmreq:' + me.id)) || [];
    const idx = reqs.findIndex((r) => r.from === from);
    if (idx < 0) return json({ error: 'no such request' }, 404);
    reqs.splice(idx, 1);
    await this.state.storage.put('dmreq:' + me.id, reqs);
    if (!(await this.dmThread(me.id, from)))
      await this.state.storage.put(this.dmKey(me.id, from), []);
    this.sendTo(from, { type: 'dmopen', with: me.id });
    return json({ ok: true });
  }

  async dmDecline(me, b) {
    const from = sanitize(b && b.from, 32);
    const reqs = (await this.state.storage.get('dmreq:' + me.id)) || [];
    const idx = reqs.findIndex((r) => r.from === from);
    if (idx >= 0) {
      reqs.splice(idx, 1);
      await this.state.storage.put('dmreq:' + me.id, reqs);
    }
    return json({ ok: true });
  }

  async addMessage(me, body) {
    const text = sanitize(body, MSG_LEN);
    if (!text) return;
    const msgs = await this.history();
    const msg = { id: randHex(6), uid: me.id, username: me.username, display: me.display || me.username, tag: me.tag || 'Member', av: me.av || 0, body: text, ts: Date.now() };
    msgs.push(msg);
    while (msgs.length > MAX_MSG) msgs.shift();
    await this.state.storage.put('messages', msgs);
    this.broadcastAll({ type: 'chat', message: msg });
  }

  /* ----------------------------------------------------- ws handlers */

  async webSocketMessage(ws, raw) {
    let a = null;
    try { a = ws.deserializeAttachment(); } catch { /* ignore */ }
    if (!a || !a.uid) return;
    let obj;
    try { obj = JSON.parse(raw); } catch { return; }
    const me = (await this.users())[a.uid];
    if (!me) { try { ws.close(); } catch { /* ignore */ } return; }

    if (obj.type === 'set') await this.setConfig(me, obj);
    else if (obj.type === 'chat') await this.addMessage(me, obj.body);
    else if (obj.type === 'cmd') await this.pushCmd(me, obj.cmd);
    else if (obj.type === 'players') await this.setPlayers(me, obj.list, obj.local);
    else if (obj.type === 'cfgs') await this.setCfgs(me, obj);
    else if (obj.type === 'logs') await this.setLogs(me, obj.text);
  }

  async webSocketClose(ws) {
    try { ws.close(); } catch { /* already closed */ }
    this.broadcastAll({ type: 'presence' });
  }

  async webSocketError() {}
}
