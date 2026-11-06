/* In-browser stand-in for the hub, used by `index.html?demo`.
 *
 * Speaks the same WebSocket message protocol as src/hub (messages.cpp) and
 * runs a JavaScript port of lib/game/game_engine.cpp, so the whole phone UI
 * can be built and tested on a laptop before any hardware exists.
 * Not deployed to the hub (tools/build_web.py skips it).
 */
(() => {
  'use strict';

  const ZONES = 5;
  const LANES = 2;
  const DIFF_KEYS = ['easy', 'normal', 'hard'];
  const now = () => performance.now();
  const rand = (lo, hi) => lo + Math.floor(Math.random() * (hi - lo + 1));

  function create({ toast }) {
    const settings = {
      schema: 1,
      wifi: { ssid: 'TARGET-RANGE', password: 'targetrange', channel: 1 },
      game: {
        zone_points: [5, 2, 3, 4, 4],
        wrong_penalty: 0,
        grace_ms: 150,
        countdown_ms: 5000,
        difficulty: [
          { active_ms: 4000, gap_min_ms: 1500, gap_max_ms: 2500 },
          { active_ms: 2500, gap_min_ms: 1000, gap_max_ms: 1800 },
          { active_ms: 1500, gap_min_ms: 1000, gap_max_ms: 1400 },
        ],
      },
      hardware: { lockout_ms: 60, arbiter_ms: 8, brightness: 255, color_order: 'RGB', led_khz: 800, volume: 25, hit_tracks: 5 },
      nodes: [
        { threshold: [300, 300, 300, 300, 300], zones_enabled: 31 },
        { threshold: [300, 300, 300, 300, 300], zones_enabled: 31 },
      ],
    };
    const timing = { countdown: 5000, feedbackHit: 300, feedbackMiss: 150 };
    const online = [true, true];
    const tuning = [false, false];
    let pin = '1234';
    let admin = false;
    let listener = () => {};
    let bot = false;
    let rev = 1;
    let lastSent = 0;
    let lastSentRev = 0;
    const history = sampleHistory();

    // ---------------------------------------------------------------- engine (port of GameEngine)

    const g = {
      state: 'idle', mode: 1, difficulty: 1, duration: 60,
      countdownEndsAt: 0, endsAt: 0, pausedAt: 0,
      players: [], lanes: [], last: null,
    };

    const emit = (m) => setTimeout(() => listener(m), 0);
    const diff = () => settings.game.difficulty[g.difficulty];
    const newPlayer = (name) => ({
      name, score: 0, hits: 0, misses: 0, wrong: 0, rtSum: 0, rtBest: 0,
      zoneHits: [0, 0, 0, 0, 0], zonePoints: [0, 0, 0, 0, 0],
    });

    function playerJson(p) {
      const shots = p.hits + p.wrong;
      return {
        name: p.name, score: p.score, hits: p.hits, misses: p.misses, wrong: p.wrong,
        accuracy_pct: shots ? Math.round((p.hits / shots) * 100) : 0,
        rt_avg_ms: p.hits ? Math.round(p.rtSum / p.hits) : 0,
        rt_best_ms: p.rtBest,
        zone_hits: [...p.zoneHits], zone_points: [...p.zonePoints],
      };
    }

    function start(cmd) {
      if (['countdown', 'running', 'paused'].includes(g.state)) return error('busy', 'มีเกมกำลังเล่นอยู่');
      if (!online.every(Boolean)) return error('node_offline', `หุ่น ${online.indexOf(false) + 1} ยังไม่ออนไลน์`);
      g.mode = cmd.mode === 2 ? 2 : 1;
      g.difficulty = Math.min(2, Math.max(0, cmd.difficulty ?? 1));
      g.duration = cmd.duration_s ?? 60;
      const names = cmd.players ?? [];
      const count = g.mode === 2 ? 2 : 1;
      g.players = Array.from({ length: count }, (_, i) => newPlayer((names[i] || '').trim().slice(0, 16) || `ผู้เล่น ${i + 1}`));
      g.lanes = Array.from({ length: LANES }, (_, l) => ({
        player: g.mode === 2 ? l : 0, phase: 'inactive', target: -1, litAt: 0, phaseEndsAt: 0, prev: null,
        mask: settings.nodes[l].zones_enabled, botAt: 0, botZone: -1,
      }));
      // Duel fairness: both lanes draw from the same pre-made sequence.
      g.sequence = [];
      g.state = 'countdown';
      g.countdownEndsAt = now() + timing.countdown;
      g.endsAt = 0;
      rev++;
    }

    function nextZone(lane, index) {
      const prev = lane.prev?.zone;
      const pick = () => {
        const options = [];
        for (let z = 0; z < ZONES; z++) if (lane.mask & (1 << z) && z !== prev) options.push(z);
        if (!options.length) for (let z = 0; z < ZONES; z++) if (lane.mask & (1 << z)) options.push(z);
        return options[Math.floor(Math.random() * options.length)];
      };
      if (g.mode !== 2) return pick();
      while (g.sequence.length <= index) g.sequence.push(null);
      if (g.sequence[index] == null || g.sequence[index] === prev) g.sequence[index] = pick();
      return g.sequence[index];
    }

    function enterGap(lane, t) {
      lane.phase = 'gap';
      lane.target = -1;
      lane.phaseEndsAt = t + rand(diff().gap_min_ms, diff().gap_max_ms);
      rev++;
    }

    function enterActive(lane, t) {
      lane.count = (lane.count ?? 0) + 1;
      lane.target = nextZone(lane, lane.count);
      lane.phase = 'active';
      lane.litAt = t;
      lane.phaseEndsAt = t + diff().active_ms;
      if (bot) scheduleBot(lane, t);
      rev++;
    }

    function scoreHit(laneIdx, zone, rt, late) {
      const lane = g.lanes[laneIdx];
      const p = g.players[lane.player];
      const pts = settings.game.zone_points[zone];
      p.score += pts;
      p.hits++;
      p.zoneHits[zone]++;
      p.zonePoints[zone] += pts;
      p.rtSum += rt;
      if (p.hits === 1 || rt < p.rtBest) p.rtBest = rt;
      emit({ t: 'event', kind: 'hit', lane: laneIdx, zone, player: lane.player, points: pts, rt_ms: rt, late });
      speaker(laneIdx);
    }

    function onHit(laneIdx, zone, t) {
      if (g.state !== 'running') return;
      const lane = g.lanes[laneIdx];
      if (lane.phase === 'active' && zone === lane.target) {
        scoreHit(laneIdx, zone, Math.max(0, Math.round(t - lane.litAt)), false);
        lane.prev = { zone, litAt: lane.litAt, changedAt: t, wasMiss: false };
        lane.phase = 'feedback_hit';
        lane.target = -1;
        lane.phaseEndsAt = t + timing.feedbackHit;
        rev++;
        return;
      }
      const prev = lane.prev;
      if (prev && zone === prev.zone) {
        if (prev.wasMiss && t - prev.changedAt <= settings.game.grace_ms) {
          g.players[lane.player].misses--;
          scoreHit(laneIdx, zone, Math.round(t - prev.litAt), true);
          prev.wasMiss = false;
          rev++;
          return;
        }
        if (!prev.wasMiss && t - prev.changedAt <= timing.feedbackHit + settings.game.grace_ms) return;
      }
      const p = g.players[lane.player];
      p.wrong++;
      p.score -= settings.game.wrong_penalty;
      emit({ t: 'event', kind: 'wrong', lane: laneIdx, zone, player: lane.player, points: -settings.game.wrong_penalty, rt_ms: 0, late: false });
      rev++;
    }

    function finish(reason) {
      g.lanes.forEach((l) => {
        l.phase = 'inactive';
        l.target = -1;
      });
      let winner = -1;
      if (g.mode === 2) {
        const [a, b] = g.players;
        if (a.score !== b.score) winner = a.score > b.score ? 0 : 1;
        else if (a.hits !== b.hits) winner = a.hits > b.hits ? 0 : 1;
      }
      g.state = 'finished';
      const id = (history.at(-1)?.id ?? 0) + 1;
      g.last = {
        id, ts: Date.now(), mode: g.mode === 2 ? 'duel' : 'solo', difficulty: DIFF_KEYS[g.difficulty],
        duration_s: g.duration, ended: reason, winner, players: g.players.map(playerJson),
      };
      history.push(g.last);
      rev++;
      emit({ t: 'result', game: g.last });
    }

    function tick() {
      const t = now();
      if (g.state === 'countdown' && t >= g.countdownEndsAt) {
        g.state = 'running';
        g.endsAt = t + g.duration * 1000;
        g.lanes.forEach((l) => enterGap(l, t));
        rev++;
      } else if (g.state === 'running') {
        if (t >= g.endsAt) return finish('finished');
        g.lanes.forEach((lane, idx) => {
          if (bot && lane.phase === 'active' && lane.botAt && t >= lane.botAt) {
            lane.botAt = 0;
            onHit(idx, lane.botZone, t);
          }
          if (t < lane.phaseEndsAt) return;
          if (lane.phase === 'gap') enterActive(lane, t);
          else if (lane.phase === 'active') {
            g.players[lane.player].misses++;
            lane.prev = { zone: lane.target, litAt: lane.litAt, changedAt: t, wasMiss: true };
            emit({ t: 'event', kind: 'miss', lane: idx, zone: lane.target, player: lane.player, points: 0, rt_ms: 0, late: false });
            lane.phase = 'feedback_miss';
            lane.target = -1;
            lane.phaseEndsAt = t + timing.feedbackMiss;
            rev++;
          } else if (lane.phase.startsWith('feedback')) enterGap(lane, t);
        });
      }
      if (rev !== lastSentRev || t - lastSent > 1000) sendState();
    }

    function scheduleBot(lane, t) {
      const roll = Math.random();
      if (roll < 0.12) return; // too slow this time
      lane.botAt = t + rand(450, Math.min(diff().active_ms - 100, 1800));
      lane.botZone = roll < 0.88 ? lane.target : (lane.target + rand(1, 4)) % ZONES;
    }

    function snapshot() {
      const t = now();
      let remaining = 0;
      if (g.state === 'running') remaining = Math.max(0, g.endsAt - t);
      else if (g.state === 'paused') remaining = g.endsAt ? g.endsAt - g.pausedAt : g.duration * 1000;
      else if (g.state === 'countdown') remaining = g.duration * 1000;
      return {
        t: 'state', rev, now: t, busy: xt.active ? 'tuning' : null,
        game: {
          state: g.state, mode: g.mode, difficulty: g.difficulty, duration_s: g.duration,
          countdown_ends_at: g.countdownEndsAt, ends_at: g.endsAt, remaining_ms: remaining,
          players: g.players.map(playerJson),
          lanes: (g.lanes.length ? g.lanes : [{}, {}]).map((l) => ({ phase: l.phase ?? 'inactive', target: l.target ?? -1, player: l.player ?? 0 })),
        },
        nodes: online.map((on, i) => ({ id: i + 1, online: on, rssi: -44 - i * 6 })),
      };
    }

    function sendState() {
      lastSent = now();
      lastSentRev = rev;
      emit(snapshot());
    }

    function pause() {
      if (g.state !== 'running' && g.state !== 'countdown') return;
      g.pausedAt = now();
      g.state = 'paused';
      rev++;
    }

    function resume() {
      if (g.state !== 'paused') return;
      if (!online.every(Boolean)) return error('node_offline', 'รอให้หุ่นทั้งสองออนไลน์ก่อน');
      const t = now();
      if (!g.endsAt) {
        g.state = 'countdown';
        g.countdownEndsAt = t + timing.countdown;
      } else {
        const shift = t - g.pausedAt;
        g.endsAt += shift;
        g.lanes.forEach((l) => {
          l.phaseEndsAt += shift;
          l.litAt += shift;
          if (l.prev) l.prev.changedAt += shift;
          if (l.botAt) l.botAt += shift;
        });
        g.state = 'running';
      }
      rev++;
    }

    // ---------------------------------------------------------------- admin simulation

    const xt = { active: false, done: false, node: 1, zone: 0, shot: 0, shots: 5, sums: null, timer: 0 };

    function nodesDetail() {
      return {
        t: 'nodes',
        nodes: online.map((on, i) => ({
          id: i + 1, online: on, seen: true, rssi: -44 - i * 6, uptime_s: Math.floor(now() / 1000), hits: 0,
          fw: '1.0.0', tuning: tuning[i], calibrated: true,
          baseline: [12, 9, 15, 11, 8].map((v) => v + rand(0, 2)),
          noise: [6, 5, 7, 6, 5].map((v) => v + rand(0, 2)),
          threshold: settings.nodes[i].threshold, zones_enabled: settings.nodes[i].zones_enabled,
        })),
      };
    }

    function fakePeaks(struck) {
      return Array.from({ length: ZONES }, (_, ch) => (ch === struck ? rand(900, 1700) : rand(40, 260)));
    }

    function crosstalkMsg(withReport) {
      const m = { t: 'crosstalk', node: xt.node, active: xt.active, done: xt.done, zone: xt.zone, shot: xt.shot, shots_per_zone: xt.shots };
      if (withReport) {
        m.matrix = xt.sums.map((row) => row.map((v) => Math.round(v / xt.shots)));
        m.min_hit = xt.minHit;
        m.suggested = m.matrix.map((_, ch) => Math.max(30, Math.round(xt.minHit[ch] * 0.4)));
        m.ratio_x10 = m.matrix.map((row, z) => Math.min(999, Math.round((row[z] * 10) / Math.max(1, ...row.filter((_, c) => c !== z)))));
        xt.report = m;
      }
      return m;
    }

    function crosstalkStep() {
      if (!xt.active) return;
      const peaks = fakePeaks(xt.zone);
      emit({ t: 'peaks', node: xt.node, zone: xt.zone, source: 'tune', peaks });
      peaks.forEach((v, ch) => (xt.sums[xt.zone][ch] += v));
      xt.minHit[xt.zone] = xt.minHit[xt.zone] ? Math.min(xt.minHit[xt.zone], peaks[xt.zone]) : peaks[xt.zone];
      if (++xt.shot >= xt.shots) {
        xt.shot = 0;
        if (++xt.zone >= ZONES) {
          xt.zone = ZONES - 1;
          xt.active = false;
          xt.done = true;
          tuning[xt.node - 1] = false;
          rev++;
          return emit(crosstalkMsg(true));
        }
      }
      emit(crosstalkMsg(false));
      xt.timer = setTimeout(crosstalkStep, 650);
    }

    function storageMsg() {
      const games = Math.min(300, history.length);
      return {
        t: 'storage',
        flash: { used: 48 * 1024 + games * 700, total: 896 * 1024, games, max_games: 300 },
        sd: { mounted: true, last_id: history.at(-1)?.id ?? 0, written: 0, file_bytes: history.length * 700, free_bytes: 7.4 * 2 ** 30, total_bytes: 7.4 * 2 ** 30 },
      };
    }

    // Stands in for each mannequin's DFPlayer. The sounds (sd-card/01/NNN.mp3,
    // served from the project root by `make demo`) are decoded into memory once,
    // so every scoring hit plays at once with no network round trip. Like the
    // real module, a new hit on the same mannequin cuts its previous sound;
    // the two mannequins have separate speakers.
    const speakerState = { ctx: null, buffers: new Map(), playing: [null, null] };

    function speakerContext() {
      try {
        speakerState.ctx ??= new (window.AudioContext || window.webkitAudioContext)();
        if (speakerState.ctx.state === 'suspended') speakerState.ctx.resume();
      } catch {
        speakerState.ctx = null;
      }
      return speakerState.ctx;
    }

    function loadSpeakerSounds() {
      const ctx = speakerContext();
      if (!ctx) return;
      for (let track = 1; track <= settings.hardware.hit_tracks; track++) {
        if (speakerState.buffers.has(track)) continue;
        speakerState.buffers.set(track, null); // loading
        fetch(`../sd-card/01/${String(track).padStart(3, '0')}.mp3`)
          .then((r) => (r.ok ? r.arrayBuffer() : Promise.reject(new Error(r.status))))
          .then((data) => ctx.decodeAudioData(data))
          .then((buf) => speakerState.buffers.set(track, buf))
          .catch(() => speakerState.buffers.delete(track)); // retried on the next unlock
      }
    }

    // Browsers only allow audio after a user gesture: unlock and preload on the first tap.
    ['pointerdown', 'keydown'].forEach((type) =>
      window.addEventListener(type, loadSpeakerSounds, { capture: true, passive: true }),
    );

    function speaker(laneIdx = 0) {
      const ctx = speakerContext();
      if (!ctx) return;
      const ready = [...speakerState.buffers.entries()].filter(([t, b]) => b && t <= settings.hardware.hit_tracks);
      if (ready.length === 0) return loadSpeakerSounds();
      const [, buffer] = ready[rand(0, ready.length - 1)];
      try {
        speakerState.playing[laneIdx]?.stop();
      } catch {
        /* already finished */
      }
      const source = ctx.createBufferSource();
      const gain = ctx.createGain();
      gain.gain.value = Math.min(1, settings.hardware.volume / 30);
      source.buffer = buffer;
      source.connect(gain).connect(ctx.destination);
      source.start();
      speakerState.playing[laneIdx] = source;
    }

    function error(code, msg) {
      emit({ t: 'error', code, msg });
    }
    function ok(what, msg) {
      emit({ t: 'ok', what, msg });
    }

    // ---------------------------------------------------------------- commands

    function handle(cmd) {
      switch (cmd.t) {
        case 'time': return;
        case 'auth':
          admin = cmd.pin === pin;
          emit({ t: 'auth', ok: admin });
          if (admin) {
            emit({ t: 'settings', settings });
            emit(nodesDetail());
            emit(storageMsg());
          }
          return;
        case 'start': return start(cmd);
        case 'stop':
          if (['countdown', 'running', 'paused'].includes(g.state)) finish('stopped');
          else if (g.state === 'finished') { g.state = 'idle'; rev++; }
          return;
        case 'resume': return resume();
        case 'reset':
          if (g.state === 'finished') { g.state = 'idle'; rev++; }
          return;
        case 'sim_hit':
          // The demo lets anyone tap to shoot; on the real hub this needs the admin PIN.
          if (g.state !== 'running') return error('not_running', 'ยังไม่ได้เริ่มเกม');
          return onHit(cmd.node - 1, cmd.zone, now());
      }
      if (!admin) return error('auth_required', 'ต้องใส่ PIN ผู้ดูแลก่อน');
      switch (cmd.t) {
        case 'get_settings': return emit({ t: 'settings', settings });
        case 'led_test': return toast(`(จำลอง) ไฟโซน ${cmd.zone + 1} ของหุ่น ${cmd.node} ติดสีเขียว 2 วินาที`);
        case 'identify': return ok('identify', `(จำลอง) หุ่น ${cmd.node} กระพริบไฟ`);
        case 'sound_test':
          speaker(cmd.node - 1);
          return ok('sound_test', `(จำลอง) หุ่น ${cmd.node} เล่นเสียงยิงโดน`);
        case 'calibrate': return ok('calibrate', `(จำลอง) หุ่น ${cmd.node} calibrate แล้ว`);
        case 'reboot_node': return ok('reboot_node', `(จำลอง) รีสตาร์ทหุ่น ${cmd.node}`);
        case 'tune':
          tuning[cmd.node - 1] = Boolean(cmd.on);
          emit(nodesDetail());
          if (cmd.on) emit({ t: 'peaks', node: cmd.node, zone: 1, source: 'tune', peaks: fakePeaks(1) });
          return;
        case 'crosstalk_start':
          clearTimeout(xt.timer);
          Object.assign(xt, {
            active: true, done: false, node: cmd.node, zone: 0, shot: 0, shots: cmd.shots ?? 5,
            sums: Array.from({ length: ZONES }, () => Array(ZONES).fill(0)), minHit: Array(ZONES).fill(0),
          });
          tuning[cmd.node - 1] = true;
          rev++;
          emit(crosstalkMsg(false));
          xt.timer = setTimeout(crosstalkStep, 900);
          return;
        case 'crosstalk_cancel':
          clearTimeout(xt.timer);
          xt.active = false;
          xt.done = false;
          tuning[xt.node - 1] = false;
          rev++;
          return emit(crosstalkMsg(false));
        case 'apply_suggested':
          if (!xt.report) return error('bad_request', 'ยังไม่มีผลทดสอบของหุ่นนี้');
          settings.nodes[xt.node - 1].threshold = xt.report.suggested;
          emit({ t: 'settings', settings });
          return ok('apply_suggested', 'ใช้ค่า threshold ที่แนะนำแล้ว');
        case 'save_game':
          if (['countdown', 'running', 'paused'].includes(g.state)) return error('busy', 'แก้ค่าเกมได้เฉพาะตอนไม่มีเกม');
          if (cmd.game.difficulty.some((d) => d.gap_min_ms < 1000 || d.gap_max_ms < d.gap_min_ms)) {
            return error('invalid', 'เวลาพักต้องไม่ต่ำกว่า 1000 ms (1 วินาที)');
          }
          settings.game = { ...settings.game, ...cmd.game };
          emit({ t: 'config', game: settings.game });
          emit({ t: 'settings', settings });
          return ok('save_game', 'บันทึกค่าเกมแล้ว');
        case 'save_hardware':
          settings.hardware = cmd.hardware;
          loadSpeakerSounds();
          emit({ t: 'settings', settings });
          return ok('save_hardware', 'บันทึกและส่งค่าไปที่หุ่นแล้ว');
        case 'save_node':
          settings.nodes[cmd.node - 1] = { threshold: cmd.threshold, zones_enabled: cmd.zones_enabled };
          emit({ t: 'settings', settings });
          return ok('save_node', `บันทึก threshold หุ่น ${cmd.node} แล้ว`);
        case 'save_wifi':
          settings.wifi = { ...cmd.wifi };
          emit({ t: 'settings', settings });
          return ok('save_wifi', 'บันทึกแล้ว รีสตาร์ท hub เพื่อใช้ค่า WiFi ใหม่');
        case 'set_pin':
          pin = cmd.pin;
          return ok('set_pin', 'เปลี่ยน PIN แล้ว');
        case 'reboot_hub': return ok('reboot_hub', '(จำลอง) รีสตาร์ท hub');
        default: return error('unknown', `ไม่รู้จักคำสั่ง ${cmd.t}`);
      }
    }

    // ---------------------------------------------------------------- demo control bar

    function mountBar() {
      const bar = document.getElementById('demo-bar');
      bar.hidden = false;
      document.body.classList.add('demo');
      bar.innerHTML = `
        <span class="hint"><b>โหมดทดลอง</b> · แตะโซนบนหุ่นในหน้าเกมเพื่อยิง · PIN ผู้ดูแล 1234</span>
        <button class="btn small" type="button" id="demo-bot">บอทยิง: ปิด</button>
        <button class="btn small" type="button" id="demo-drop">ทำให้หุ่น 2 หลุด</button>`;
      document.getElementById('demo-bot').addEventListener('click', (ev) => {
        bot = !bot;
        ev.target.textContent = `บอทยิง: ${bot ? 'เปิด' : 'ปิด'}`;
        if (bot) g.lanes.forEach((l) => l.phase === 'active' && scheduleBot(l, now()));
      });
      document.getElementById('demo-drop').addEventListener('click', (ev) => {
        online[1] = !online[1];
        ev.target.textContent = online[1] ? 'ทำให้หุ่น 2 หลุด' : 'ให้หุ่น 2 กลับมา';
        if (!online[1] && ['running', 'countdown'].includes(g.state)) {
          pause();
          error('node_offline', 'หุ่น 2 ขาดการเชื่อมต่อ เกมหยุดชั่วคราว');
        }
        rev++;
      });
    }

    return {
      connect(onMessage, onStatus) {
        listener = onMessage;
        mountBar();
        setTimeout(() => {
          onStatus(true);
          emit({ t: 'hello', fw: '1.0.0-demo', admin: false, zones: ZONES, lanes: LANES, game: settings.game });
          sendState();
        }, 150);
        setInterval(tick, 10);
        setInterval(() => admin && emit(nodesDetail()), 1000);
      },
      send(obj) {
        handle(JSON.parse(JSON.stringify(obj)));
        return true;
      },
      async history() {
        return JSON.parse(JSON.stringify(history));
      },
    };
  }

  // Clearly-labelled example games so the ranking and history screens aren't empty in the demo.
  function sampleHistory() {
    const names = ['ตัวอย่าง ต้น', 'ตัวอย่าง ฟ้า', 'ตัวอย่าง บอส', 'ตัวอย่าง มายด์', 'ตัวอย่าง เจ', 'ตัวอย่าง ปลา', 'ตัวอย่าง เก่ง'];
    let seed = 7;
    const rnd = (lo, hi) => {
      seed = (seed * 9301 + 49297) % 233280;
      return lo + Math.floor((seed / 233280) * (hi - lo + 1));
    };
    const player = (name, duration, difficulty) => {
      const targets = Math.round((duration / 60) * [16, 22, 26][difficulty]);
      const hits = rnd(Math.round(targets * 0.45), targets - 1);
      const misses = targets - hits;
      const wrong = rnd(0, 5);
      const zoneHits = [0, 0, 0, 0, 0];
      for (let i = 0; i < hits; i++) zoneHits[rnd(0, 4)]++;
      const pts = [5, 2, 3, 4, 4];
      const zonePoints = zoneHits.map((h, z) => h * pts[z]);
      const rt = rnd(520, 1400);
      return {
        name, score: zonePoints.reduce((a, b) => a + b, 0), hits, misses, wrong,
        accuracy_pct: Math.round((hits / Math.max(1, hits + wrong)) * 100),
        rt_avg_ms: rt, rt_best_ms: Math.round(rt * 0.55), zone_hits: zoneHits, zone_points: zonePoints,
      };
    };
    const day = 24 * 3600 * 1000;
    const games = [];
    for (let id = 1; id <= 18; id++) {
      const duel = id % 4 === 0;
      const difficulty = [1, 1, 0, 2, 1, 2][id % 6];
      const duration = [60, 60, 30, 60, 90, 60][id % 6];
      const a = names[rnd(0, names.length - 1)];
      let b = names[rnd(0, names.length - 1)];
      if (b === a) b = names[(names.indexOf(a) + 1) % names.length];
      const players = duel ? [player(a, duration, difficulty), player(b, duration, difficulty)] : [player(a, duration, difficulty)];
      const winner = duel ? (players[0].score === players[1].score ? -1 : players[0].score > players[1].score ? 0 : 1) : -1;
      games.push({
        id, ts: Date.now() - (19 - id) * 0.4 * day, mode: duel ? 'duel' : 'solo', difficulty: ['easy', 'normal', 'hard'][difficulty],
        duration_s: duration, ended: id === 7 ? 'stopped' : 'finished', winner, players,
      });
    }
    return games;
  }

  window.TargetRangeDemo = { create };
})();
