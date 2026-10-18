// Entry worker. /api/* goes to the single hub Durable Object; everything else
// is served from the static panel. Auth is enforced inside the DO.

import { Room } from './room.js';

export { Room };

// Bump to point at a brand-new, empty Durable Object (used to wipe test data).
const HUB = 'hub-v4';

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === '/healthz') {
      return new Response('{"ok":true}', { headers: { 'content-type': 'application/json' } });
    }

    if (url.pathname.startsWith('/api/')) {
      const stub = env.ROOM.get(env.ROOM.idFromName(HUB));
      return stub.fetch(request);
    }

    return env.ASSETS.fetch(request);
  },
};
