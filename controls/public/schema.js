/* Feature schema. The browser renders every control from this list and pushes
   key/value changes to /api/config; the cheat client maps the same keys.
   row.t:  b=bool  f=float  i=int  c=combo  col=colour  key=keybind  t=text  btn=command
   row.show(st): optional visibility predicate, st = { b(k), n(k), s(k) } */

window.SCHEMA = [
  {
    id: 'client', name: 'Client', icon: 'power',
    groups: [{ id: 'ctl', name: 'Control', dynamic: 'client', rows: [] }],
  },
  {
    id: 'aimbot', name: 'Aimbot', icon: 'crosshair',
    groups: [
      {
        id: 'aim', name: 'Aim',
        rows: [
          { k: 'aim.enabled', l: 'Enable', t: 'b', d: false },
          { k: 'aim.mode', l: 'Aim mode', t: 'c', it: ['humanized', 'normal', 'blatant'], d: 'humanized' },
          { k: 'aim.at', l: 'Aim at', t: 'c', it: ['mouse', 'camera'], d: 'mouse' },
          { k: 'aim.key', l: 'Aim key', t: 'key', d: 2 },
          { k: 'aim.activation', l: 'Activation', t: 'c', it: ['hold', 'toggle', 'always'], d: 'hold' },
          { k: 'aim.use_fov', l: 'Use FOV', t: 'b', d: true },
          { k: 'aim.fov', l: 'FOV', t: 'f', mn: 0, mx: 600, st: 1, d: 120, show: (s) => s.b('aim.use_fov') },
          { k: 'aim.dynamic_fov', l: 'Dynamic FOV', t: 'b', d: false, show: (s) => s.b('aim.use_fov') },
          { k: 'aim.deadzone', l: 'Deadzone', t: 'f', mn: 0, mx: 100, st: 1, d: 0 },
          { k: 'aim.draw_fov', l: 'Draw FOV circle', t: 'b', d: true, show: (s) => s.b('aim.use_fov') },
          { k: 'aim.fov_color', l: 'FOV colour', t: 'col', d: '#a855f7' },
          { k: 'aim.speed', l: 'Aim speed', t: 'f', mn: 0.01, mx: 1, st: 0.01, d: 0.35, show: (s) => s.s('aim.mode') !== 'blatant' },
          { k: 'aim.speed_x', l: 'Aim speed X', t: 'f', mn: 0.01, mx: 1, st: 0.01, d: 0.35, show: (s) => s.s('aim.mode') !== 'blatant' },
          { k: 'aim.speed_y', l: 'Aim speed Y', t: 'f', mn: 0.01, mx: 1, st: 0.01, d: 0.35, show: (s) => s.s('aim.mode') !== 'blatant' },
          { k: 'aim.mouse_sens', l: 'Mouse sensitivity', t: 'f', mn: 0.1, mx: 5, st: 0.1, d: 1 },
          { k: 'aim.cam_smooth', l: 'Camera smoothing', t: 'f', mn: 0.1, mx: 5, st: 0.1, d: 1 },
        ],
      },
      {
        id: 'targeting', name: 'Targeting',
        rows: [
          { k: 'tgt.team', l: 'Team check', t: 'b', d: true },
          { k: 'tgt.knock', l: 'Knock check', t: 'b', d: true },
          { k: 'tgt.ping', l: 'Ping interpolation', t: 'b', d: true },
          { k: 'tgt.sticky', l: 'Sticky aim', t: 'b', d: false },
          { k: 'tgt.disable_kill', l: 'Disable on kill', t: 'b', d: false },
          { k: 'tgt.health_check', l: 'Health check', t: 'b', d: false },
          { k: 'tgt.min_health', l: 'Min health', t: 'f', mn: 0, mx: 100, st: 1, d: 20, show: (s) => s.b('tgt.health_check') },
          { k: 'tgt.part', l: 'Target part', t: 'c', it: ['head', 'neck', 'chest', 'stomach', 'nearest', 'random'], d: 'head' },
          { k: 'tgt.air_part', l: 'Air target part', t: 'c', it: ['head', 'neck', 'chest', 'stomach', 'nearest', 'random'], d: 'head' },
          { k: 'tgt.point_count', l: 'Point amount', t: 'i', mn: 1, mx: 6, st: 1, d: 4 },
        ],
      },
      {
        id: 'triggerbot', name: 'Triggerbot',
        rows: [
          { k: 'tb.enabled', l: 'Enable', t: 'b', d: false },
          { k: 'tb.key', l: 'Trigger key', t: 'key', d: 164 },
          { k: 'tb.activation', l: 'Activation', t: 'c', it: ['hold', 'toggle', 'always'], d: 'hold' },
          { k: 'tb.method', l: 'Method', t: 'c', it: ['normal', 'mesh'], d: 'normal' },
          { k: 'tb.threshold', l: 'Threshold', t: 'f', mn: 1, mx: 100, st: 1, d: 40 },
          { k: 'tb.delay', l: 'Delay', t: 'i', mn: 1, mx: 50, st: 1, d: 8 },
          { k: 'tb.cooldown', l: 'Cooldown (ms)', t: 'i', mn: 0, mx: 1000, st: 10, d: 120 },
          { k: 'tb.team', l: 'Team check', t: 'b', d: true },
          { k: 'tb.knock', l: 'Knock check', t: 'b', d: true },
          { k: 'tb.gun', l: 'Gun check', t: 'b', d: true },
          { k: 'tb.draw_fov', l: 'Draw FOV circle', t: 'b', d: false },
          { k: 'tb.fov_color', l: 'FOV colour', t: 'col', d: '#a855f7' },
          { k: 'tb.fill', l: 'Fill FOV', t: 'b', d: false },
          { k: 'tb.fill_color', l: 'Fill colour', t: 'col', d: '#a855f733' },
        ],
      },
      {
        id: 'recoil', name: 'Recoil',
        rows: [
          { k: 'rec.enabled', l: 'Recoil control', t: 'b', d: false },
          { k: 'rec.strength', l: 'Recoil strength', t: 'f', mn: 0, mx: 3, st: 0.05, d: 1 },
        ],
      },
      {
        id: 'humanize', name: 'Humanize',
        rows: [
          { k: 'hu.enabled', l: 'Enable', t: 'b', d: false },
          { k: 'hu.jitter', l: 'Jitter', t: 'f', mn: 0, mx: 10, st: 0.1, d: 1.2 },
          { k: 'hu.lock_delay', l: 'Lock delay', t: 'i', mn: 1, mx: 50, st: 1, d: 6 },
          { k: 'hu.overshoot', l: 'Overshoot', t: 'f', mn: 0, mx: 30, st: 0.5, d: 4 },
          { k: 'hu.wobble', l: 'Path wobble', t: 'f', mn: 0, mx: 10, st: 0.1, d: 1 },
          { k: 'hu.miss', l: 'Miss chance %', t: 'f', mn: 0, mx: 50, st: 1, d: 5 },
          { k: 'hu.motion', l: 'Aim motion', t: 'b', d: false },
          { k: 'hu.motion_speed', l: 'Motion speed', t: 'f', mn: 0.1, mx: 10, st: 0.1, d: 1, show: (s) => s.b('hu.motion') },
          { k: 'hu.motion_int', l: 'Motion intensity', t: 'f', mn: 0, mx: 20, st: 0.5, d: 3, show: (s) => s.b('hu.motion') },
          { k: 'hu.stick', l: 'Stick assist', t: 'b', d: false },
          { k: 'hu.stick_strength', l: 'Stick strength', t: 'f', mn: 0.01, mx: 1, st: 0.01, d: 0.2, show: (s) => s.b('hu.stick') },
          { k: 'hu.fatigue', l: 'Fatigue', t: 'b', d: false },
          { k: 'hu.fatigue_rate', l: 'Fatigue rate', t: 'f', mn: 0, mx: 1, st: 0.01, d: 0.1, show: (s) => s.b('hu.fatigue') },
          { k: 'hu.fatigue_decay', l: 'Fatigue decay', t: 'f', mn: 0, mx: 2, st: 0.01, d: 0.3, show: (s) => s.b('hu.fatigue') },
          { k: 'hu.fatigue_move', l: 'Fatigue move', t: 'f', mn: 0, mx: 10, st: 0.1, d: 1, show: (s) => s.b('hu.fatigue') },
          { k: 'hu.fatigue_offset', l: 'Fatigue offset', t: 'f', mn: 0, mx: 5, st: 0.1, d: 0.5, show: (s) => s.b('hu.fatigue') },
          { k: 'hu.slowdown', l: 'Assist slowdown', t: 'b', d: false },
          { k: 'hu.slowdown_speed', l: 'Slowdown speed', t: 'f', mn: 50, mx: 600, st: 5, d: 200, show: (s) => s.b('hu.slowdown') },
          { k: 'hu.slowdown_strength', l: 'Slowdown strength', t: 'f', mn: 0, mx: 1, st: 0.01, d: 0.4, show: (s) => s.b('hu.slowdown') },
          { k: 'hu.shake', l: 'Shake', t: 'b', d: false },
          { k: 'hu.shake_val', l: 'Shake value', t: 'f', mn: 0, mx: 15, st: 0.5, d: 2, show: (s) => s.b('hu.shake') },
          { k: 'hu.shake_z', l: 'Shake Z', t: 'f', mn: 0, mx: 15, st: 0.5, d: 2, show: (s) => s.b('hu.shake') },
          { k: 'hu.offset', l: 'Offset', t: 'b', d: false },
          { k: 'hu.offset_x', l: 'Offset X', t: 'f', mn: -50, mx: 50, st: 1, d: 0, show: (s) => s.b('hu.offset') },
          { k: 'hu.offset_y', l: 'Offset Y', t: 'f', mn: -50, mx: 50, st: 1, d: 0, show: (s) => s.b('hu.offset') },
        ],
      },
      {
        id: 'prediction', name: 'Prediction',
        rows: [
          { k: 'pr.enabled', l: 'Enable', t: 'b', d: false },
          { k: 'pr.x', l: 'Prediction X', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') },
          { k: 'pr.y', l: 'Prediction Y', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') },
          { k: 'pr.jump', l: 'Jump prediction', t: 'b', d: false, show: (s) => s.b('pr.enabled') },
          { k: 'pr.jump_val', l: 'Jump value', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') && s.b('pr.jump') },
          { k: 'pr.fall', l: 'Fall prediction', t: 'b', d: false, show: (s) => s.b('pr.enabled') },
          { k: 'pr.fall_val', l: 'Fall value', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') && s.b('pr.fall') },
          { k: 'pr.vel_mult', l: 'Velocity multiplier', t: 'f', mn: 0, mx: 3, st: 0.05, d: 1, show: (s) => s.b('pr.enabled') },
          { k: 'pr.air', l: 'Air prediction', t: 'b', d: false, show: (s) => s.b('pr.enabled') },
          { k: 'pr.air_x', l: 'Air X', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') && s.b('pr.air') },
          { k: 'pr.air_y', l: 'Air Y', t: 'f', mn: 0, mx: 30, st: 0.1, d: 3, show: (s) => s.b('pr.enabled') && s.b('pr.air') },
          { k: 'pr.range', l: 'Prediction range', t: 'f', mn: 50, mx: 1000, st: 10, d: 400, show: (s) => s.b('pr.enabled') },
          { k: 'pr.adaptive', l: 'Adaptive prediction', t: 'b', d: false, show: (s) => s.b('pr.enabled') },
          { k: 'pr.adapt_strength', l: 'Adapt strength', t: 'f', mn: 0, mx: 3, st: 0.05, d: 1, show: (s) => s.b('pr.enabled') && s.b('pr.adaptive') },
          { k: 'pr.adapt_speed', l: 'Adapt speed', t: 'f', mn: 1, mx: 100, st: 1, d: 20, show: (s) => s.b('pr.enabled') && s.b('pr.adaptive') },
          { k: 'pr.dot', l: 'Prediction dot', t: 'b', d: false, show: (s) => s.b('pr.enabled') },
          { k: 'pr.dot_color', l: 'Dot colour', t: 'col', d: '#a855f7', show: (s) => s.b('pr.enabled') && s.b('pr.dot') },
          { k: 'pr.dot_size', l: 'Dot size', t: 'f', mn: 1, mx: 15, st: 0.5, d: 4, show: (s) => s.b('pr.enabled') && s.b('pr.dot') },
        ],
      },
    ],
  },
  {
    id: 'visuals', name: 'Visuals', icon: 'eye',
    groups: [
      {
        id: 'esp', name: 'ESP',
        rows: [
          { k: 'esp.box', l: 'Box', t: 'b', d: true },
          { k: 'esp.box_type', l: 'Box type', t: 'c', it: ['normal', 'corners', 'rounded', 'filled'], d: 'corners', show: (s) => s.b('esp.box') },
          { k: 'esp.box_fill', l: 'Box fill', t: 'b', d: false, show: (s) => s.b('esp.box') },
          { k: 'esp.username', l: 'Username', t: 'b', d: true },
          { k: 'esp.username_type', l: 'Username type', t: 'c', it: ['display', 'account', 'tag'], d: 'display', show: (s) => s.b('esp.username') },
          { k: 'esp.username_pos', l: 'Username position', t: 'c', it: ['top', 'bottom', 'above'], d: 'top', show: (s) => s.b('esp.username') },
          { k: 'esp.name_transform', l: 'Name transform', t: 'c', it: ['none', 'upper', 'lower'], d: 'none', show: (s) => s.b('esp.username') },
          { k: 'esp.distance', l: 'Distance', t: 'b', d: true },
          { k: 'esp.tool', l: 'Held tool', t: 'b', d: false },
          { k: 'esp.healthbar', l: 'Healthbar', t: 'b', d: true },
          { k: 'esp.healthbar_style', l: 'Healthbar style', t: 'c', it: ['left', 'right', 'top', 'bottom'], d: 'left', show: (s) => s.b('esp.healthbar') },
          { k: 'esp.healthbar_text', l: 'Healthbar text', t: 'b', d: false, show: (s) => s.b('esp.healthbar') },
          { k: 'esp.tracers', l: 'Tracers', t: 'b', d: false },
          { k: 'esp.tracer_origin', l: 'Tracer origin', t: 'c', it: ['bottom', 'center', 'top', 'mouse'], d: 'bottom', show: (s) => s.b('esp.tracers') },
          { k: 'esp.tracer_thickness', l: 'Tracer thickness', t: 'f', mn: 0.5, mx: 5, st: 0.1, d: 1, show: (s) => s.b('esp.tracers') },
          { k: 'esp.skeleton', l: 'Skeleton', t: 'b', d: false },
          { k: 'esp.head_dot', l: 'Head dot', t: 'b', d: false },
          { k: 'esp.head_type', l: 'Head type', t: 'c', it: ['dot', 'circle', 'cross'], d: 'circle', show: (s) => s.b('esp.head_dot') },
          { k: 'esp.head_size', l: 'Head size', t: 'f', mn: 1, mx: 20, st: 0.5, d: 4, show: (s) => s.b('esp.head_dot') },
          { k: 'esp.arrows', l: 'Offscreen arrows', t: 'b', d: false },
          { k: 'esp.arrow_size', l: 'Arrow size', t: 'f', mn: 5, mx: 40, st: 1, d: 15, show: (s) => s.b('esp.arrows') },
          { k: 'esp.arrow_radius', l: 'Arrow radius', t: 'f', mn: 20, mx: 400, st: 5, d: 120, show: (s) => s.b('esp.arrows') },
          { k: 'esp.corpse', l: 'Corpse', t: 'b', d: false },
        ],
      },
      {
        id: 'overlays', name: 'Overlays',
        rows: [
          { k: 'ov.box_grad', l: 'Box gradient', t: 'b', d: false, show: (s) => s.b('esp.box') },
          { k: 'ov.grad_type', l: 'Gradient type', t: 'c', it: ['vertical', 'horizontal', 'diagonal'], d: 'vertical', show: (s) => s.b('ov.box_grad') },
          { k: 'ov.grad_pulse', l: 'Gradient pulse', t: 'b', d: false, show: (s) => s.b('ov.box_grad') },
          { k: 'ov.fill_grad', l: 'Fill gradient', t: 'b', d: false },
          { k: 'ov.fill_pulse', l: 'Fill pulse', t: 'b', d: false, show: (s) => s.b('ov.fill_grad') },
          { k: 'ov.fill_color', l: 'Fill colour', t: 'col', d: '#a855f766' },
          { k: 'ov.fill_mid', l: 'Fill mid', t: 'col', d: '#a855f744' },
          { k: 'ov.fill_low', l: 'Fill low', t: 'col', d: '#a855f722' },
          { k: 'ov.user_grad', l: 'Username gradient', t: 'b', d: false, show: (s) => s.b('esp.username') },
          { k: 'ov.user_grad_type', l: 'Username gradient type', t: 'c', it: ['vertical', 'horizontal', 'rainbow'], d: 'horizontal', show: (s) => s.b('ov.user_grad') },
          { k: 'ov.user_grad_speed', l: 'Username gradient speed', t: 'f', mn: 0.1, mx: 10, st: 0.1, d: 1, show: (s) => s.b('ov.user_grad') },
          { k: 'ov.user_color2', l: 'Username 2nd colour', t: 'col', d: '#22d3ee', show: (s) => s.b('ov.user_grad') },
          { k: 'ov.hb_grad', l: 'Healthbar gradient', t: 'b', d: false, show: (s) => s.b('esp.healthbar') },
          { k: 'ov.hb_pulse', l: 'Healthbar pulse', t: 'b', d: false, show: (s) => s.b('ov.hb_grad') },
          { k: 'ov.chams', l: 'Chams', t: 'b', d: false },
          { k: 'ov.chams_fade', l: 'Chams fade', t: 'b', d: false, show: (s) => s.b('ov.chams') },
          { k: 'ov.fade_speed', l: 'Fade speed', t: 'f', mn: 0.1, mx: 5, st: 0.1, d: 1, show: (s) => s.b('ov.chams') && s.b('ov.chams_fade') },
          { k: 'ov.chams_corpse', l: 'Chams corpse', t: 'b', d: false, show: (s) => s.b('ov.chams') },
          { k: 'ov.corpse_names', l: 'Corpse names', t: 'b', d: false },
          { k: 'ov.trails', l: 'Movement trails', t: 'b', d: false },
          { k: 'ov.trails_thickness', l: 'Trail thickness', t: 'f', mn: 0.5, mx: 10, st: 0.1, d: 2, show: (s) => s.b('ov.trails') },
          { k: 'ov.trails_length', l: 'Trail length', t: 'f', mn: 1, mx: 50, st: 1, d: 10, show: (s) => s.b('ov.trails') },
          { k: 'ov.va_lines', l: 'View angle lines', t: 'b', d: false },
          { k: 'ov.lines_thickness', l: 'Line thickness', t: 'f', mn: 0.5, mx: 10, st: 0.1, d: 1, show: (s) => s.b('ov.va_lines') },
          { k: 'ov.lines_length', l: 'Line length', t: 'f', mn: 1, mx: 100, st: 1, d: 20, show: (s) => s.b('ov.va_lines') },
        ],
      },
      {
        id: 'filters', name: 'Filters',
        rows: [
          { k: 'fl.team', l: 'Team check', t: 'b', d: true },
          { k: 'fl.knock', l: 'Knock check', t: 'b', d: true },
          { k: 'fl.alive', l: 'Alive check', t: 'b', d: true },
          { k: 'fl.ragdoll', l: 'Ragdoll check', t: 'b', d: false },
          { k: 'fl.dist_limit', l: 'Distance limit', t: 'b', d: false },
          { k: 'fl.max_dist', l: 'Max distance', t: 'f', mn: 10, mx: 5000, st: 10, d: 500, show: (s) => s.b('fl.dist_limit') },
          { k: 'fl.font_size', l: 'ESP font size', t: 'f', mn: 8, mx: 28, st: 1, d: 14 },
        ],
      },
      {
        id: 'highlights', name: 'Highlights',
        rows: [
          { k: 'hl.enemy', l: 'Enemy highlight', t: 'b', d: false },
          { k: 'hl.enemy_color', l: 'Enemy colour', t: 'col', d: '#ef4444', show: (s) => s.b('hl.enemy') },
          { k: 'hl.friendly', l: 'Friendly highlight', t: 'b', d: false },
          { k: 'hl.friendly_color', l: 'Friendly colour', t: 'col', d: '#22c55e', show: (s) => s.b('hl.friendly') },
        ],
      },
    ],
  },
  {
    id: 'world', name: 'World', icon: 'globe',
    groups: [{ id: 'players', name: 'Players', dynamic: 'players', rows: [] }],
  },
  {
    id: 'exploits', name: 'Exploits', icon: 'bolt',
    groups: [
      {
        id: 'misc', name: 'Misc',
        rows: [
          { k: 'ex.freecam', l: 'Freecam', t: 'b', d: false },
          { k: 'ex.freecam_speed', l: 'Freecam speed', t: 'f', mn: 1, mx: 50, st: 0.5, d: 8, show: (s) => s.b('ex.freecam') },
          { k: 'ex.typing', l: 'Typing check', t: 'b', d: true },
          { k: 'ex.no_spread', l: 'No spread', t: 'b', d: false },
          { k: 'ex.no_recoil', l: 'No recoil', t: 'b', d: false },
          { k: 'ex.fullbright', l: 'Fullbright', t: 'b', d: false },
          { k: 'ex.fps_unlock', l: 'FPS unlock', t: 'b', d: false },
        ],
      },
    ],
  },
  {
    id: 'config', name: 'Config', icon: 'sliders',
    groups: [{ id: 'cfgmgr', name: 'Configs', dynamic: 'cfgs', rows: [] }],
  },
  {
    id: 'colors', name: 'Colours', icon: 'palette',
    groups: [
      {
        id: 'colors', name: 'Esp colours',
        rows: [
          { k: 'col.enemy_box', l: 'Enemy box', t: 'col', d: '#a855f7' },
          { k: 'col.enemy_visible', l: 'Enemy visible', t: 'col', d: '#f472b6' },
          { k: 'col.enemy_fill', l: 'Enemy fill', t: 'col', d: '#a855f733' },
          { k: 'col.friendly_box', l: 'Friendly box', t: 'col', d: '#22d3ee' },
          { k: 'col.friendly_fill', l: 'Friendly fill', t: 'col', d: '#22d3ee33' },
          { k: 'col.skeleton', l: 'Skeleton', t: 'col', d: '#e2e8f0' },
          { k: 'col.tracer', l: 'Tracer', t: 'col', d: '#a855f7' },
          { k: 'col.username', l: 'Username', t: 'col', d: '#f8fafc' },
          { k: 'col.distance', l: 'Distance', t: 'col', d: '#94a3b8' },
          { k: 'col.health_high', l: 'Health high', t: 'col', d: '#22c55e' },
          { k: 'col.health_mid', l: 'Health mid', t: 'col', d: '#eab308' },
          { k: 'col.health_low', l: 'Health low', t: 'col', d: '#ef4444' },
          { k: 'col.chams', l: 'Chams', t: 'col', d: '#a855f7' },
          { k: 'col.trail', l: 'Trail', t: 'col', d: '#a855f7aa' },
          { k: 'col.arrow', l: 'Offscreen arrow', t: 'col', d: '#a855f7' },
          { k: 'col.corpse', l: 'Corpse', t: 'col', d: '#64748b' },
        ],
      },
    ],
  },
  {
    id: 'settings', name: 'Settings', icon: 'gear',
    groups: [
      {
        id: 'interface', name: 'Interface',
        rows: [
          { k: 'ui.vsync', l: 'V-Sync', t: 'b', d: false },
          { k: 'ui.streamproof', l: 'Streamproof', t: 'b', d: false },
          { k: 'ui.cursor', l: 'Cursor in recordings', t: 'b', d: false },
        ],
      },
      { id: 'appearance', name: 'Appearance', dynamic: 'theme', rows: [] },
      {
        id: 'tools', name: 'Tools',
        rows: [
          { k: 'ui.panic_key', l: 'Panic key', t: 'key', d: 46 },
        ],
      },
    ],
  },
];

/* Full keyboard + mouse table. Value stored in config = Windows virtual-key code. */
window.KEYS = (function () {
  const range = (a, b, f) => Array.from({ length: b - a + 1 }, (_, i) => f(a + i));
  const mouse = [['Left Mouse', 1], ['Right Mouse', 2], ['Middle Mouse', 4], ['Mouse 4 (side)', 5], ['Mouse 5 (side)', 6]];
  const letters = range(65, 90, (v) => [String.fromCharCode(v), v]);
  const digits = range(48, 57, (v) => [String.fromCharCode(v), v]);
  const fn = range(112, 135, (v) => ['F' + (v - 111), v]);
  const numpad = range(96, 105, (v) => ['Numpad ' + (v - 96), v]).concat([['Numpad *', 106], ['Numpad +', 107], ['Numpad -', 109], ['Numpad .', 110], ['Numpad /', 111], ['Numpad Enter', 13]]);
  const editing = [['Backspace', 8], ['Tab', 9], ['Enter', 13], ['Space', 32], ['Caps Lock', 20]];
  const nav = [['Insert', 45], ['Delete', 46], ['Home', 36], ['End', 35], ['Page Up', 33], ['Page Down', 34], ['Up Arrow', 38], ['Down Arrow', 40], ['Left Arrow', 37], ['Right Arrow', 39]];
  const mods = [['Left Shift', 160], ['Right Shift', 161], ['Left Ctrl', 162], ['Right Ctrl', 163], ['Left Alt', 164], ['Right Alt', 165], ['Left Win', 91], ['Right Win', 92]];
  const punct = [['; :', 186], ['= +', 187], [', <', 188], ['- _', 189], ['. >', 190], ['/ ?', 191], ['` ~', 192], ['[ {', 219], ['\\ |', 220], ['] }', 221], ["' \"", 222]];
  const sys = [['Escape', 27], ['Print Screen', 44], ['Scroll Lock', 145], ['Pause', 19], ['Num Lock', 144], ['Menu', 93]];
  const media = [['Volume Mute', 173], ['Volume Down', 174], ['Volume Up', 175], ['Next Track', 176], ['Prev Track', 177], ['Stop Media', 178], ['Play / Pause', 179]];
  return [
    { g: 'Mouse', k: mouse }, { g: 'Letters', k: letters }, { g: 'Numbers', k: digits },
    { g: 'Function', k: fn }, { g: 'Numpad', k: numpad }, { g: 'Editing', k: editing },
    { g: 'Navigation', k: nav }, { g: 'Modifiers', k: mods }, { g: 'Punctuation', k: punct },
    { g: 'System', k: sys }, { g: 'Media', k: media },
  ];
})();
