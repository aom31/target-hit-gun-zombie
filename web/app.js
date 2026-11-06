/* Target Range phone UI.
 *
 * Talks to the hub over one WebSocket (/ws). The hub owns all game state and
 * pushes it here; this file only renders it and sends commands. Message
 * formats are defined in src/hub/messages.cpp — keep both sides in sync.
 *
 * Open with ?demo to run against an in-browser simulated hub (web/demo.js).
 */
(() => {
  'use strict';

  // ------------------------------------------------------------------ constants

  const ZONES = ['หัว', 'อก', 'ท้อง', 'แขนซ้าย', 'แขนขวา'];
  const DIFFICULTY = ['ง่าย', 'ปานกลาง', 'ยาก'];
  const DIFFICULTY_KEYS = ['easy', 'normal', 'hard'];
  const DURATIONS = [30, 60, 90, 120];
  const LANES = 2;
  // GitHub Pages has no hub behind it, so the public copy always runs the in-browser demo.
  const DEMO = new URLSearchParams(location.search).has('demo') || location.hostname.endsWith('github.io');
  const FEED_LENGTH = 6;
  const FLASH_MS = { hit: 350, wrong: 250 };
  const HOLD_TO_STOP_MS = 900;
  const COLOR_ORDERS = ['RGB', 'GRB', 'BRG', 'RBG', 'GBR', 'BGR'];
  const MIN_GAP_MS = 1000; // lib/game/game_types.h kMinGapMs

  // ------------------------------------------------------------------ helpers

  const { t, tHub, playerName } = window.I18N;
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
  const esc = (v) =>
    String(v ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const fmtClock = (ms) => {
    const s = Math.max(0, Math.ceil(ms / 1000));
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
  };
  const fmtSeconds = (ms) => (ms ? t('{s} วิ', { s: (ms / 1000).toFixed(2) }) : '–');
  const fmtUptime = (s) => (s >= 3600 ? `${Math.floor(s / 3600)} ชม. ${Math.floor((s % 3600) / 60)} น.` : `${Math.floor(s / 60)} น. ${s % 60} วิ`);
  const fmtDate = (ts) =>
    ts ? new Date(ts).toLocaleString(window.I18N.locale(), { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }) : '';
  const modeName = (m) => (m === 2 || m === 'duel' ? t('เล่น 2 คน') : t('เล่นคนเดียว'));
  const difficultyName = (d) => {
    const th = DIFFICULTY[typeof d === 'number' ? d : DIFFICULTY_KEYS.indexOf(d)];
    return th ? t(th) : '';
  };
  const zoneName = (z) => t(ZONES[z] ?? '');
  const nameOf = (p) => esc(playerName(p?.name ?? ''));

  const saved = {
    get(key, fallback) {
      try {
        const v = localStorage.getItem(`tr.${key}`);
        return v == null ? fallback : JSON.parse(v);
      } catch {
        return fallback;
      }
    },
    set(key, value) {
      try {
        localStorage.setItem(`tr.${key}`, JSON.stringify(value));
      } catch {
        /* private mode: settings just won't be remembered */
      }
    },
  };

  function toast(text, kind = '') {
    if (!text) return;
    const el = document.createElement('div');
    el.className = `toast ${kind}`;
    el.textContent = text;
    $('#toasts').append(el);
    setTimeout(() => el.remove(), kind === 'bad' ? 4500 : 2800);
  }

  // ------------------------------------------------------------------ sound, vibration, wake lock

  const Sound = {
    ctx: null,
    unlock() {
      try {
        this.ctx ??= new (window.AudioContext || window.webkitAudioContext)();
        if (this.ctx.state === 'suspended') this.ctx.resume();
      } catch {
        this.ctx = null;
      }
    },
    beep(freq, ms, volume = 0.25) {
      if (!this.ctx) return;
      const t = this.ctx.currentTime;
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.frequency.value = freq;
      osc.type = 'square';
      gain.gain.setValueAtTime(volume, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + ms / 1000);
      osc.connect(gain).connect(this.ctx.destination);
      osc.start(t);
      osc.stop(t + ms / 1000);
    },
    tick() { this.beep(660, 120); },
    go() { this.beep(990, 380); },
    hit() { this.beep(1320, 90, 0.18); },
    wrong() { this.beep(200, 180, 0.22); },
    end() {
      this.beep(880, 160);
      setTimeout(() => this.beep(880, 160), 220);
      setTimeout(() => this.beep(1320, 420), 440);
    },
  };

  const vibrate = (ms) => {
    try {
      navigator.vibrate?.(ms);
    } catch {
      /* not supported */
    }
  };

  let wakeLock = null;
  async function keepAwake(on) {
    try {
      if (on && !wakeLock && navigator.wakeLock) {
        wakeLock = await navigator.wakeLock.request('screen');
        wakeLock.addEventListener('release', () => (wakeLock = null));
      } else if (!on && wakeLock) {
        await wakeLock.release();
        wakeLock = null;
      }
    } catch {
      wakeLock = null;
    }
  }

  // The countdown sound file plays only on the phone that pressed START. Browsers
  // only allow audio started from a tap, so the START tap "primes" the element and
  // playback begins once the hub confirms the countdown, seeked to stay in sync.
  // If the file can't play, the synthesized beeps in Sound take over.
  const Countdown = {
    el: null,
    primed: null,
    ready: false,
    prime() {
      this.el ??= $('#countdown-audio');
      this.ready = false;
      const el = this.el;
      if (!el) {
        this.primed = Promise.resolve(false);
        return;
      }
      el.muted = true;
      this.primed = Promise.resolve(el.play())
        .then(() => {
          el.pause();
          el.currentTime = 0;
          el.muted = false;
          return (this.ready = true);
        })
        .catch(() => {
          el.muted = false;
          return (this.ready = false);
        });
    },
    async start(offsetMs) {
      if (!(await (this.primed ?? Promise.resolve(false)))) return;
      try {
        this.el.currentTime = Math.max(0, offsetMs / 1000);
        await this.el.play();
      } catch {
        this.ready = false;
      }
    },
    stop() {
      this.ready = false;
      try {
        this.el?.pause();
        if (this.el) this.el.currentTime = 0;
      } catch {
        /* nothing playing */
      }
    },
  };

  // ------------------------------------------------------------------ transport

  class HubSocket {
    constructor(url) {
      this.url = url;
      this.ws = null;
      this.delay = 500;
    }
    connect(onMessage, onStatus) {
      const open = () => {
        const ws = new WebSocket(this.url);
        this.ws = ws;
        ws.onopen = () => {
          this.delay = 500;
          onStatus(true);
        };
        ws.onmessage = (ev) => {
          try {
            onMessage(JSON.parse(ev.data));
          } catch (err) {
            console.warn('bad message', err);
          }
        };
        ws.onclose = () => {
          onStatus(false);
          setTimeout(open, this.delay);
          this.delay = Math.min(this.delay * 2, 4000);
        };
        ws.onerror = () => ws.close();
      };
      open();
    }
    send(obj) {
      if (this.ws?.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify(obj));
        return true;
      }
      toast(t('ยังไม่ได้เชื่อมต่อ hub'), 'bad');
      return false;
    }
    async history() {
      const res = await fetch('/history.jsonl', { cache: 'no-store' });
      if (!res.ok) throw new Error(res.statusText);
      return (await res.text())
        .split('\n')
        .filter((l) => l.trim())
        .map((l) => JSON.parse(l));
    }
  }

  // ------------------------------------------------------------------ state

  const S = {
    connected: false,
    hello: null,
    game: null,
    nodes: [],
    busy: null,
    offset: 0,
    result: null,
    admin: false,
    pin: null,
    settings: null,
    nodesDetail: [],
    storage: null,      // flash + SD card status (admin only)
    crosstalk: null,
    peaks: {},
    feed: [],
    flashes: {},
    combos: [],         // per player: { streak, best } for the game being watched
    simTap: false,
    startedHere: false, // this phone pressed START: it plays the countdown sound
    tuneNode: 1,
    history: null,      // finished games from the hub (history.jsonl), oldest first
    historyLoading: null,
    challenge: null,    // { score, name, category } the player is trying to beat
    ranking: saved.get('ranking', null),
    setup: {
      mode: 1,
      names: saved.get('names', ['', '']),
      duration: saved.get('duration', 60),
      difficulty: saved.get('difficulty', 1),
    },
  };

  let hub = null;
  const hubNow = () => performance.now() + S.offset;
  const allOnline = () => S.nodes.length === LANES && S.nodes.every((n) => n.online);
  const anySim = () => S.nodes.some((n) => n.sim);
  // Tap-to-shoot on a lane: demo, admin with the toggle, or anyone while that mannequin is simulated.
  const canTap = (lane) => DEMO || (S.admin && S.simTap) || !!S.nodes.find((n) => n.id === lane + 1)?.sim;
  const gameActive = () => ['countdown', 'running', 'paused'].includes(S.game?.state);
  const send = (obj) => hub?.send(obj);

  // ------------------------------------------------------------------ rankings and stats

  // Games only compete with games played under the same rules.
  const Stats = {
    modeKey: (m) => (m === 2 || m === 'duel' ? 'duel' : 'solo'),
    diffKey: (d) => (typeof d === 'number' ? DIFFICULTY_KEYS[d] : d),
    category(mode, difficulty, duration) {
      return `${this.modeKey(mode)}|${this.diffKey(difficulty)}|${duration}`;
    },
    categoryOf(g) {
      return this.category(g.mode, g.difficulty, g.duration_s);
    },
    categoryLabel(key) {
      const [mode, diff, dur] = key.split('|');
      return `${modeName(mode)} · ${difficultyName(diff)} · ${t('{s} วิ', { s: dur })}`;
    },
    // One row per player per finished game.
    entries(history = S.history ?? []) {
      const rows = [];
      for (const g of history) {
        if (g.ended !== 'finished') continue;
        g.players.forEach((p, i) =>
          rows.push({ ...p, id: g.id, ts: g.ts, player: i, category: this.categoryOf(g), won: g.winner === i }),
        );
      }
      return rows;
    },
    board(category, history) {
      return this.entries(history)
        .filter((r) => r.category === category)
        .sort((a, b) => b.score - a.score || b.accuracy_pct - a.accuracy_pct || a.rt_avg_ms - b.rt_avg_ms || a.id - b.id);
    },
    // Where a score would land on a board (1-based); ties rank together.
    rankFor(board, score, exceptId) {
      return 1 + board.filter((r) => r.score > score && r.id !== exceptId).length;
    },
    grade(p) {
      const targets = p.hits + p.misses;
      const hitRate = targets ? p.hits / targets : 0;
      const value = 0.6 * hitRate + 0.4 * (p.accuracy_pct / 100);
      if (p.hits === 0) return 'D';
      return value >= 0.9 ? 'S' : value >= 0.78 ? 'A' : value >= 0.62 ? 'B' : value >= 0.45 ? 'C' : 'D';
    },
    personalBest(name, category, exceptId) {
      const mine = this.board(category).filter((r) => r.name === name && r.id !== exceptId);
      return mine[0] ?? null;
    },
    players() {
      const by = new Map();
      for (const r of this.entries()) {
        const p = by.get(r.name) ?? { name: r.name, games: 0, best: null, hits: 0, shots: 0, wins: 0, last: 0 };
        p.games += 1;
        p.hits += r.hits;
        p.shots += r.hits + r.wrong;
        p.wins += r.won ? 1 : 0;
        p.last = Math.max(p.last, r.ts || r.id);
        if (!p.best || r.score > p.best.score) p.best = r;
        by.set(r.name, p);
      }
      return [...by.values()].sort((a, b) => b.best.score - a.best.score);
    },
    categories() {
      const counts = new Map();
      for (const r of this.entries()) counts.set(r.category, (counts.get(r.category) ?? 0) + 1);
      return [...counts.entries()].sort((a, b) => b[1] - a[1]).map(([key]) => key);
    },
    async load(force = false) {
      if (S.history && !force) return S.history;
      S.historyLoading ??= hub
        .history()
        .then((h) => (S.history = h))
        .catch(() => (S.history ??= []))
        .finally(() => (S.historyLoading = null));
      return S.historyLoading;
    },
  };

  // ------------------------------------------------------------------ messages from the hub

  function onMessage(m) {
    switch (m.t) {
      case 'hello':
        S.hello = m;
        send({ t: 'time', epoch_ms: Date.now() });
        if (S.pin) send({ t: 'auth', pin: S.pin });
        Stats.load();
        break;
      case 'config':
        if (S.hello) S.hello.game = m.game;
        if (route() === 'setup') render();
        break;
      case 'state':
        onState(m);
        break;
      case 'event':
        onEvent(m);
        break;
      case 'result':
        onResult(m.game);
        break;
      case 'nodes':
        S.nodesDetail = m.nodes;
        if (route() === 'admin') Admin.updateNodes();
        break;
      case 'peaks':
        S.peaks[m.node] = { ...m, at: performance.now() };
        if (route() === 'admin') Admin.updateBars();
        break;
      case 'crosstalk':
        S.crosstalk = m;
        if (route() === 'admin') Admin.updateCrosstalk();
        break;
      case 'storage':
        S.storage = m;
        if (route() === 'admin') Admin.updateStorage();
        break;
      case 'settings':
        S.settings = m.settings;
        if (route() === 'poster') render();
        if (route() === 'admin') Admin.fillForms();
        break;
      case 'auth':
        S.admin = m.ok;
        if (!m.ok) {
          S.pin = null;
          toast(t('PIN ไม่ถูกต้อง'), 'bad');
        }
        if (route() === 'admin' || route() === 'poster') render();
        break;
      case 'error':
        if (S.startedHere && S.game?.state !== 'countdown') {
          // START was refused (e.g. a mannequin offline): silence the primed countdown.
          S.startedHere = false;
          Countdown.stop();
        }
        toast(tHub(m.msg) || m.code, 'bad');
        break;
      case 'ok':
        if (m.msg) toast(tHub(m.msg), 'ok');
        break;
      default:
        break;
    }
  }

  async function onResult(game) {
    const watching = route() === 'play' || route() === 'result';
    S.result = game;
    keepAwake(false);
    if (watching && game.ended === 'finished') Sound.end();
    // The hub has already appended this game to history.jsonl: reload for fresh ranks.
    await Stats.load(true);
    if (!S.history.some((g) => g.id === game.id)) S.history.push(game);
    if (watching) go('result');
  }

  function onState(m) {
    // Clock sync: the hub's millis() against our performance.now().
    S.offset = m.now - performance.now();
    const prev = S.game?.state;
    S.game = m.game;
    S.nodes = m.nodes;
    S.busy = m.busy;
    updateChrome();

    const now = m.game.state;
    if (now !== prev) {
      if (now === 'countdown' || now === 'running' || now === 'paused') {
        if (prev == null || prev === 'idle' || prev === 'finished') {
          S.feed = [];
          S.combos = (m.game.players ?? []).map(() => ({ streak: 0, best: 0 }));
          Stats.load();
        }
        keepAwake(true);
        if (route() !== 'play') go('play');
      }
      if (now === 'idle' && route() === 'play') go('home');
      if (now === 'countdown' && S.startedHere) {
        const total = S.hello?.game?.countdown_ms ?? 5000;
        Countdown.start(total - (m.game.countdown_ends_at - hubNow()));
      }
      if (now === 'running' && prev === 'countdown' && S.startedHere) {
        if (!Countdown.ready) Sound.go();
        S.startedHere = false; // the file keeps playing its start beep
      }
      if (now !== 'countdown' && now !== 'running' && prev === 'countdown') {
        Countdown.stop();
        S.startedHere = false;
      }
    }
    if (route() === 'play') Play.update();
    else if (route() === 'setup' || route() === 'home') updateStartAvailability();
  }

  function onEvent(e) {
    const names = S.game?.players ?? [];
    const who = S.game?.mode === 2 ? `${playerName(names[e.player]?.name ?? '')} · ` : '';
    const combo = (S.combos[e.player] ??= { streak: 0, best: 0 });
    let text = '';
    if (e.kind === 'hit') {
      combo.streak += 1;
      combo.best = Math.max(combo.best, combo.streak);
      text = `+${e.points} ${zoneName(e.zone)}`;
      Sound.hit();
      vibrate(40);
    } else if (e.kind === 'wrong') {
      combo.streak = 0;
      text = e.points < 0 ? t('{p} ผิดจุด ({z})', { p: e.points, z: zoneName(e.zone) }) : t('ผิดจุด ({z})', { z: zoneName(e.zone) });
      Sound.wrong();
    } else {
      combo.streak = 0;
      text = t('หลุดเป้า ({z})', { z: zoneName(e.zone) });
    }
    S.feed.unshift({ kind: e.kind, text: who + text, rt: e.kind === 'hit' ? e.rt_ms : null, lane: e.lane });
    S.feed.length = Math.min(S.feed.length, FEED_LENGTH);
    if (e.kind !== 'miss') {
      S.flashes[`${e.lane}:${e.zone}`] = { kind: e.kind, until: performance.now() + FLASH_MS[e.kind] };
    }
    if (route() === 'play') Play.onEvent(e);
  }

  // ------------------------------------------------------------------ chrome (top bar, banner, tab bar)

  function updateChrome() {
    const pill = $('#conn-pill');
    if (DEMO) {
      pill.textContent = t('โหมดทดลอง');
      pill.className = 'pill ok';
    } else {
      pill.textContent = S.connected ? t('ออนไลน์') : t('ขาดการเชื่อมต่อ');
      pill.className = `pill ${S.connected ? 'ok' : 'bad'}`;
    }
    for (let id = 1; id <= LANES; id++) {
      const node = S.nodes.find((n) => n.id === id);
      const dot = $(`#node-${id}`);
      dot.className = `node-dot ${node?.sim ? 'sim' : node?.online ? 'on' : 'off'}`;
      dot.title = t('หุ่น {n}: {s}', { n: id, s: t(node?.sim ? 'จำลอง (โหมดทดสอบ)' : node?.online ? 'ออนไลน์' : 'ไม่ออนไลน์') });
    }
    const banner = $('#banner');
    if (!S.connected) {
      banner.textContent = t('กำลังเชื่อมต่อ hub… ตรวจว่ามือถือต่อ WiFi ของสนามอยู่');
      banner.className = 'banner bad';
      banner.hidden = false;
    } else if (S.busy === 'tuning') {
      banner.textContent = t('กำลังจูนเซนเซอร์ ยังเริ่มเกมไม่ได้');
      banner.className = 'banner';
      banner.hidden = false;
    } else if (anySim()) {
      const ids = S.nodes.filter((n) => n.sim).map((n) => n.id).join(', ');
      banner.textContent = t('โหมดทดสอบ: หุ่น {ids} เป็นหุ่นจำลอง แตะโซนบนรูปหุ่นเพื่อยิง', { ids });
      banner.className = 'banner';
      banner.hidden = false;
    } else {
      banner.hidden = true;
    }
  }

  function onStatus(connected) {
    S.connected = connected;
    if (!connected) S.admin = false;
    updateChrome();
    if (connected && route() === 'admin') render();
  }

  // ------------------------------------------------------------------ router

  // Routes look like #ranking or #game/12; the part after "/" is the parameter.
  const hashParts = () => (location.hash || '#home').slice(1).split('?')[0].split('/');
  const route = () => hashParts()[0] || 'home';
  const routeParam = () => decodeURIComponent(hashParts().slice(1).join('/'));
  const go = (name) => {
    if (location.hash === `#${name}`) render();
    else location.hash = `#${name}`;
  };

  const TABS = { home: 'home', setup: 'home', result: 'home', ranking: 'ranking', challenge: 'challenge', history: 'history', players: 'history', game: 'history', card: 'history' };

  const Views = {};
  function render() {
    const name = route();
    const view = Views[name] ?? Views.home;
    const main = $('#view');
    main.innerHTML = view.html();
    view.mount?.(main);
    window.scrollTo(0, 0);
    if (name !== 'play') $('#countdown').hidden = true;
    document.body.dataset.route = name;
    $$('#tabbar a').forEach((a) => a.classList.toggle('on', a.dataset.tab === TABS[name]));
  }

  // ------------------------------------------------------------------ shared widgets

  // Line icons (24x24, stroke = currentColor), shapes adapted from Lucide (ISC licence).
  const ICON_PATHS = {
    target: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="1" fill="currentColor"/><path d="M12 1v4M12 19v4M1 12h4M19 12h4"/>',
    swords: '<path d="M14.5 17.5 3 6V3h3l11.5 11.5M13 19l6-6M16 16l4 4M19 21l2-2M9.5 6.5 21 18v3h-3l-3.5-3.5M11 5 5 11M8 8 4 4M5 3 3 5"/>',
    trophy: '<path d="M8 21h8M12 17v4M7 4h10v5a5 5 0 0 1-10 0z"/><path d="M17 5h3v2a3 3 0 0 1-3 3M7 5H4v2a3 3 0 0 0 3 3"/>',
    flame: '<path d="M8.5 14.5A2.5 2.5 0 0 0 11 12c0-1.4-.5-2-1-3-1.1-2.1-.2-4.1 2-6 .5 2.5 2 4.9 4 6.5s3 3.5 3 5.5a7 7 0 1 1-14 0c0-1.2.4-2.3 1-3a2.5 2.5 0 0 0 2.5 2.5z"/>',
    history: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5M12 7v5l3.5 2"/>',
    chevron: '<path d="m9 6 6 6-6 6"/>',
    up: '<path d="m6 15 6-6 6 6"/>',
    bolt: '<path d="M13 2 4 14h7l-1 8 9-12h-7z"/>',
    crown: '<path d="m3 8 4.5 4L12 5l4.5 7L21 8l-2 11H5z"/>',
    skull: '<path d="M12 3a8 8 0 0 0-8 8c0 2.6 1.3 4.6 3 6v3h10v-3c1.7-1.4 3-3.4 3-6a8 8 0 0 0-8-8z"/><circle cx="9" cy="12" r="1.6" fill="currentColor"/><circle cx="15" cy="12" r="1.6" fill="currentColor"/><path d="M10 20v-2M14 20v-2"/>',
    x: '<path d="M18 6 6 18M6 6l12 12"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    download: '<path d="M12 3v12M7 10l5 5 5-5M5 21h14"/>',
    share: '<circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><path d="m8.6 13.5 6.8 4M15.4 6.5l-6.8 4"/>',
    replay: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/><path d="m10 9 5 3-5 3z" fill="currentColor"/>',
  };
  const icon = (name, cls = '') =>
    `<svg class="ico ${cls}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICON_PATHS[name]}</svg>`;

  // Front view, as the shooter sees it (matches the hardware wiring sheet).
  const ZONE_SHAPES = [
    { x: 76, y: 10, w: 48, h: 54, rx: 22, lx: 100, ly: 41 },
    { x: 56, y: 74, w: 88, h: 70, rx: 8, lx: 100, ly: 113 },
    { x: 56, y: 151, w: 88, h: 62, rx: 8, lx: 100, ly: 186 },
    { x: 18, y: 74, w: 32, h: 124, rx: 12, lx: 34, ly: 140 },
    { x: 150, y: 74, w: 32, h: 124, rx: 12, lx: 166, ly: 140 },
  ];

  const zoneCenter = (z) => ({ x: ZONE_SHAPES[z].x + ZONE_SHAPES[z].w / 2, y: ZONE_SHAPES[z].y + ZONE_SHAPES[z].h / 2 });

  function figureSvg(lane) {
    const zones = ZONE_SHAPES.map(
      (z, i) =>
        `<rect class="zone" data-lane="${lane}" data-zone="${i}" x="${z.x}" y="${z.y}" width="${z.w}" height="${z.h}" rx="${z.rx}"/>` +
        `<text class="zl" x="${z.lx}" y="${z.ly}" text-anchor="middle">${i + 1}</text>`,
    ).join('');
    // Plywood silhouette with the five floating plates on top, plus a lock-on reticle.
    return `<svg viewBox="0 0 200 262" role="img" aria-label="${t('หุ่น {n}', { n: lane + 1 })}">
      <defs><linearGradient id="wood-${lane}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4a3f2c"/><stop offset="1" stop-color="#2b241a"/></linearGradient></defs>
      <path class="wood" fill="url(#wood-${lane})" d="M100 2c19 0 34 15 34 34 0 11-5 21-13 27h23c11 0 20 3 28 9l12 6c5 3 8 8 8 14v108c0 6-4 10-10 10h-24v12H42v-12H18c-6 0-10-4-10-10V92c0-6 3-11 8-14l12-6c8-6 17-9 28-9h23c-8-6-13-16-13-27C66 17 81 2 100 2z"/>
      <rect class="wood" x="88" y="222" width="24" height="38" fill="url(#wood-${lane})"/>
      ${zones}
      <g class="reticle" id="ret-${lane}"><circle r="19"/><circle r="3" class="dot"/><path d="M0-27v10M0 17v10M-27 0h10M17 0h10"/></g>
    </svg>`;
  }

  // Plain SVG QR code, generated offline by vendor/qrcode.js.
  function qrSvg(text, size = 200) {
    if (typeof window.qrcode !== 'function') return '';
    const qr = window.qrcode(0, 'M');
    qr.addData(text);
    qr.make();
    const n = qr.getModuleCount();
    const quiet = 2;
    let d = '';
    for (let r = 0; r < n; r++) {
      for (let c = 0; c < n; c++) if (qr.isDark(r, c)) d += `M${c + quiet} ${r + quiet}h1v1h-1z`;
    }
    const total = n + quiet * 2;
    return `<svg class="qr" viewBox="0 0 ${total} ${total}" width="${size}" height="${size}" shape-rendering="crispEdges" role="img" aria-label="QR code"><rect width="${total}" height="${total}" fill="#fff"/><path d="${d}" fill="#000"/></svg>`;
  }

  const cardUrl = (id) => `${location.href.split('#')[0]}#card/${id}`;
  const gradeBadge = (g, size = '') => `<span class="grade grade-${g} ${size}">${g}</span>`;
  const medallion = (g) => `<span class="medallion m-${g}">${gradeBadge(g, 'lg')}</span>`;
  const confetti = () =>
    `<div class="confetti" aria-hidden="true">${Array.from({ length: 28 }, (_, i) => {
      const colors = ['#7cff4f', '#ffd34f', '#4fd8ff', '#ff3d3d', '#ffffff'];
      return `<i style="left:${(i * 37) % 100}%;background:${colors[i % 5]};animation-delay:${(i % 7) * 0.12}s;animation-duration:${2.2 + (i % 5) * 0.35}s"></i>`;
    }).join('')}</div>`;
  const rankChip = (rank) => `<span class="rank-chip ${rank <= 3 ? `top${rank}` : ''}">#${rank}</span>`;
  const loading = () => `<div class="panel"><p class="muted">${t('กำลังโหลด…')}</p></div>`;

  function currentCategory() {
    return Stats.category(S.setup.mode, S.setup.difficulty, S.setup.duration);
  }

  // Board, rank and records for one player of one game.
  function standing(game, playerIndex) {
    const p = game.players[playerIndex];
    const category = Stats.categoryOf(game);
    const board = Stats.board(category);
    const rank = Stats.rankFor(board, p.score, game.id);
    const others = board.filter((r) => r.id !== game.id || r.player !== playerIndex);
    const pb = Stats.personalBest(p.name, category, game.id);
    return { p, category, rank, total: others.length + 1, record: rank === 1 && others.length > 0, pb, grade: Stats.grade(p) };
  }

  function setChallenge(row) {
    const [mode, diff, duration] = row.category.split('|');
    S.challenge = { score: row.score, name: row.name, category: row.category };
    S.setup.mode = mode === 'duel' ? 2 : 1;
    S.setup.difficulty = DIFFICULTY_KEYS.indexOf(diff);
    S.setup.duration = Number(duration);
    go('setup');
  }

  // ------------------------------------------------------------------ views: home

  Views.home = {
    html() {
      const active = gameActive();
      return `
        <section class="hero">
          <svg class="hero-reticle" viewBox="0 0 200 200" aria-hidden="true">
            <circle cx="100" cy="100" r="92" class="r1"/><circle cx="100" cy="100" r="64" class="r2"/><circle cx="100" cy="100" r="34" class="r3"/>
            <path d="M100 0v40M100 160v40M0 100h40M160 100h40" class="r2"/><circle cx="100" cy="100" r="6" class="dot"/>
          </svg>
          <span class="eyebrow">${icon('skull')} ZOMBIE TARGET CHALLENGE</span>
          <h1 class="title">TARGET<br><em>RANGE</em></h1>
          <p class="tagline">${t('ไฟขึ้นตรงไหน ยิงตรงนั้น · ยิ่งไว ยิ่งแม่น ยิ่งขึ้นอันดับ')}</p>
        </section>
        ${active ? `<a class="btn primary huge block pulse" href="#play">${t('กลับไปที่เกม')}</a>` : ''}
        <div class="mode-grid">
          <button class="mode" data-mode="1" type="button">
            <span class="mode-badge">${icon('target')}</span>
            <span class="mode-text"><small>SOLO</small><b>${t('เล่นคนเดียว')}</b><span>${t('ยิงหุ่นทั้ง 2 ตัว เป้าขึ้นพร้อมกันได้ 2 จุด')}</span></span>
            ${icon('chevron', 'go')}
          </button>
          <button class="mode duel" data-mode="2" type="button">
            <span class="mode-badge">${icon('swords')}</span>
            <span class="mode-text"><small>VERSUS</small><b>${t('ดวล 2 คน')}</b><span>${t('คนละหุ่น เป้าลำดับเดียวกัน ใครแต้มสูงกว่าชนะ')}</span></span>
            ${icon('chevron', 'go')}
          </button>
        </div>
        <p class="hint" id="node-hint"></p>
        <div id="home-record">${loading()}</div>
        <div class="links"><a class="btn ghost small" href="#admin">${t('ผู้ดูแลระบบ')}</a></div>`;
    },
    async mount(root) {
      $$('.mode', root).forEach((b) =>
        b.addEventListener('click', () => {
          S.setup.mode = Number(b.dataset.mode);
          S.challenge = null;
          go('setup');
        }),
      );
      updateStartAvailability();
      await Stats.load();
      if (route() !== 'home') return;
      const cats = Stats.categories();
      const category = cats.includes(currentCategory()) ? currentCategory() : cats[0];
      const box = $('#home-record', root);
      if (!category) {
        box.innerHTML = `<div class="panel record-panel"><b>${t('ยังไม่มีสถิติ')}</b><p class="muted">${t('เล่นเกมแรกแล้วชื่อคุณจะขึ้นเป็นอันดับ 1')}</p></div>`;
        return;
      }
      const top = Stats.board(category).slice(0, 3);
      box.innerHTML = `
        <div class="panel record-panel">
          <div class="row"><span class="eyebrow">TOP 3</span><span class="spacer"></span><span class="muted small">${Stats.categoryLabel(category)}</span></div>
          <ol class="mini-board">${top
            .map((r, i) => `<li><a href="#game/${r.id}">${rankChip(i + 1)}<span class="nm">${nameOf(r)}</span><b class="num">${r.score}</b></a></li>`)
            .join('')}</ol>
          <div class="row">
            <button class="btn primary" type="button" id="beat-top">${icon('flame')} ${t('ท้าชิงอันดับ 1')}</button>
            <a class="btn ghost" href="#ranking">${t('ดูอันดับทั้งหมด')}</a>
          </div>
        </div>`;
      $('#beat-top', root).addEventListener('click', () => setChallenge(top[0]));
    },
  };

  function updateStartAvailability() {
    const hint = $('#node-hint');
    const reason = !S.connected
      ? t('ยังไม่ได้เชื่อมต่อ hub')
      : S.busy === 'tuning'
        ? t('กำลังจูนเซนเซอร์อยู่')
        : !allOnline()
          ? t('รอหุ่น {ids} เชื่อมต่อ…', { ids: S.nodes.filter((n) => !n.online).map((n) => n.id).join(', ') || '1, 2' })
          : gameActive()
            ? t('มีเกมกำลังเล่นอยู่')
            : '';
    if (hint) {
      hint.textContent = reason || t('หุ่นทั้ง 2 ตัวพร้อมแล้ว');
      hint.classList.toggle('ok', !reason);
    }
    const start = $('#start-btn');
    if (start) {
      start.disabled = Boolean(reason);
      $('#start-why').textContent = reason;
    }
  }

  // ------------------------------------------------------------------ views: setup

  Views.setup = {
    html() {
      const st = S.setup;
      const diff = S.hello?.game?.difficulty ?? [];
      const nameField = (i) => `
        <label class="field">${st.mode === 2 ? t('ผู้เล่น {n} · หุ่น {n}', { n: i + 1 }) : t('ชื่อผู้เล่น (ขึ้นในตารางอันดับ)')}
          <input type="text" id="name-${i}" maxlength="16" autocomplete="off" value="${esc(st.names[i] ?? '')}" placeholder="${t('ผู้เล่น {n}', { n: i + 1 })}">
        </label>`;
      return `
        <h1 class="page-title">${st.mode === 2 ? t('ดวล 2 คน') : t('เล่นคนเดียว')}</h1>
        <div class="stack">
          <div id="challenge-box"></div>
          <div class="panel stack">
            ${nameField(0)}
            ${st.mode === 2 ? nameField(1) : ''}
          </div>
          <div class="panel stack">
            <h2>${t('เวลาเกม')}</h2>
            <div class="chips" id="durations">
              ${DURATIONS.map((d) => `<button class="chip" type="button" data-v="${d}" aria-pressed="${d === st.duration}">${t('{s} วิ', { s: d })}</button>`).join('')}
            </div>
            <h2>${t('ความยาก')}</h2>
            <div class="chips" id="difficulty">
              ${DIFFICULTY.map(
                (label, i) =>
                  `<button class="chip diff-${i}" type="button" data-v="${i}" aria-pressed="${i === st.difficulty}">${t(label)}${
                    diff[i] ? ` <small>${t('ไฟ {s} วิ', { s: (diff[i].active_ms / 1000).toFixed(1) })}</small>` : ''
                  }</button>`,
              ).join('')}
            </div>
            <p class="record-line" id="record-line"></p>
          </div>
          <button class="btn primary huge block" id="start-btn" type="button">START</button>
          <p class="hint" id="start-why" style="text-align:center"></p>
          <a class="btn ghost block" href="#home">${t('กลับ')}</a>
        </div>`;
    },
    mount(root) {
      const refresh = () => this.refreshRecord(root);
      const pick = (groupId, key) =>
        $(`#${groupId}`, root).addEventListener('click', (ev) => {
          const chip = ev.target.closest('.chip');
          if (!chip) return;
          S.setup[key] = Number(chip.dataset.v);
          $$(`#${groupId} .chip`, root).forEach((c) => c.setAttribute('aria-pressed', String(c === chip)));
          refresh();
        });
      pick('durations', 'duration');
      pick('difficulty', 'difficulty');
      $('#start-btn', root).addEventListener('click', startGame);
      $('#challenge-box', root).addEventListener('click', (ev) => {
        if (ev.target.id !== 'drop-challenge') return;
        S.challenge = null;
        refresh();
      });
      updateStartAvailability();
      Stats.load().then(refresh);
    },
    refreshRecord(root) {
      if (route() !== 'setup') return;
      const category = currentCategory();
      const top = Stats.board(category)[0];
      $('#record-line', root).innerHTML = top
        ? t('🏆 สถิติโหมดนี้: <b>{score}</b> แต้ม โดย {name}', { score: top.score, name: nameOf(top) })
        : t('ยังไม่มีใครเล่นโหมดนี้ เล่นก่อนได้อันดับ 1 ทันที');
      const c = S.challenge;
      $('#challenge-box', root).innerHTML =
        c && c.category === category
          ? `<div class="panel challenge-panel"><span class="eyebrow">${icon('flame')} CHALLENGE</span>
               <b>${t('ทำให้ได้มากกว่า {score} แต้ม', { score: c.score })}</b><span class="muted">${t('สถิติของ {name}', { name: nameOf(c) })}</span>
               <button class="btn ghost small" type="button" id="drop-challenge">${t('ยกเลิกการท้าทาย')}</button></div>`
          : '';
    },
  };

  function startGame() {
    const st = S.setup;
    for (let i = 0; i < 2; i++) {
      const input = $(`#name-${i}`);
      if (input) st.names[i] = input.value.trim();
    }
    saved.set('names', st.names);
    saved.set('duration', st.duration);
    saved.set('difficulty', st.difficulty);
    Sound.unlock();
    Countdown.prime();
    S.startedHere = true;
    keepAwake(true);
    send({
      t: 'start',
      mode: st.mode,
      difficulty: st.difficulty,
      duration_s: st.duration,
      players: st.mode === 2 ? [st.names[0], st.names[1]] : [st.names[0]],
    });
  }

  // ------------------------------------------------------------------ views: play

  const Play = {
    lastCountdown: null,
    raf: 0,
    html() {
      return `
        <div class="hud">
          <div class="clock num" id="clock">0:00</div>
          <div class="time-bar"><i id="time-fill"></i></div>
        </div>
        <div class="panel paused-panel stack" id="paused" hidden>
          <b>${t('เกมหยุดชั่วคราว')}</b>
          <span class="muted small" id="paused-why"></span>
          <button class="btn primary block" id="resume-btn" type="button">${t('เล่นต่อ')}</button>
        </div>
        <div id="challenge-bar"></div>
        <div class="scoreboard" id="scoreboard"></div>
        <div class="figures" id="figures">
          ${[0, 1].map((l) => `<div class="panel figure" data-lane="${l}"><div class="label" id="fig-label-${l}">${t('หุ่น {n}', { n: l + 1 })}</div>${figureSvg(l)}</div>`).join('')}
        </div>
        <ul class="feed" id="feed"></ul>
        <button class="btn danger block hold" id="stop-btn" type="button"><span class="fill"></span>${t('กดค้างเพื่อหยุดเกม')}</button>
        <div class="combo-pop" id="combo-pop" aria-hidden="true"></div>`;
    },
    mount(root) {
      $('#resume-btn', root).addEventListener('click', () => send({ t: 'resume' }));
      this.bindHold($('#stop-btn', root), () => send({ t: 'stop' }));
      $('#figures', root).addEventListener('click', (ev) => {
        const zone = ev.target.closest('.zone');
        if (!zone || !canTap(Number(zone.dataset.lane))) return;
        send({ t: 'sim_hit', node: Number(zone.dataset.lane) + 1, zone: Number(zone.dataset.zone) });
      });
      this.update();
      cancelAnimationFrame(this.raf);
      const loop = () => {
        if (route() !== 'play') return;
        this.frame();
        this.raf = requestAnimationFrame(loop);
      };
      this.raf = requestAnimationFrame(loop);
    },
    bindHold(btn, action) {
      let timer = 0;
      const fill = $('.fill', btn);
      const cancel = () => {
        clearTimeout(timer);
        fill.style.transition = 'none';
        fill.style.width = '0';
      };
      btn.addEventListener('pointerdown', () => {
        fill.style.transition = `width ${HOLD_TO_STOP_MS}ms linear`;
        fill.style.width = '100%';
        timer = setTimeout(() => {
          cancel();
          action();
        }, HOLD_TO_STOP_MS);
      });
      ['pointerup', 'pointerleave', 'pointercancel'].forEach((e) => btn.addEventListener(e, cancel));
    },
    // Live position of a score on this category's board, and what it takes to climb.
    climb(score) {
      const g = S.game;
      const board = Stats.board(Stats.category(g.mode, g.difficulty, g.duration_s));
      if (!board.length) return { rank: 1, text: t('เกมแรกของโหมดนี้') };
      const rank = Stats.rankFor(board, score);
      if (rank === 1) return { rank, text: score > board[0].score ? t('ทำลายสถิติ!') : t('เสมออันดับ 1') };
      const above = board[rank - 2];
      return { rank, text: t('อีก {n} แต้ม แซง {name}', { n: above.score - score + 1, name: nameOf(above) }) };
    },
    update() {
      const g = S.game;
      if (!g || !$('#scoreboard')) return;
      const duel = g.mode === 2;

      $('#scoreboard').innerHTML = (g.players ?? [])
        .map((p, i) => {
          const c = this.climb(p.score);
          const combo = S.combos[i]?.streak ?? 0;
          return `
          <div class="panel score-card ${duel ? `lane-${i}` : ''}" id="score-${i}">
            <div class="row"><span class="name">${nameOf(p)}</span><span class="spacer"></span>${rankChip(c.rank)}</div>
            <div class="score num">${p.score}</div>
            <div class="climb">${c.rank > 1 ? icon('up') : icon('crown')}${c.text}</div>
            <div class="stats"><span>${t('โดน {n}', { n: p.hits })}</span><span>${t('หลุด {n}', { n: p.misses })}</span><span>${t('ผิด {n}', { n: p.wrong })}</span>${combo >= 2 ? `<span class="combo">COMBO ×${combo}</span>` : ''}</div>
          </div>`;
        })
        .join('');

      const c = S.challenge;
      const bar = $('#challenge-bar');
      if (c && c.category === Stats.category(g.mode, g.difficulty, g.duration_s)) {
        const best = Math.max(0, ...(g.players ?? []).map((p) => p.score));
        const pct = clamp((best / (c.score + 1)) * 100, 0, 100);
        const beaten = best > c.score;
        bar.innerHTML = `<div class="panel challenge-run ${beaten ? 'won' : ''}">
          <div class="row"><b>${beaten ? t('ทำลายสถิติแล้ว! 🔥') : t('ท้าชิง {name} · {score} แต้ม', { name: nameOf(c), score: c.score })}</b><span class="spacer"></span><span class="num">${beaten ? `+${best - c.score}` : t('อีก {n}', { n: c.score + 1 - best })}</span></div>
          <div class="progress"><i style="width:${pct}%"></i></div></div>`;
      } else {
        bar.innerHTML = '';
      }

      for (let l = 0; l < LANES; l++) {
        const label = $(`#fig-label-${l}`);
        if (label) label.textContent = duel ? `${playerName(g.players?.[l]?.name ?? '')} · ${t('หุ่น {n}', { n: l + 1 })}` : t('หุ่น {n}', { n: l + 1 });
      }
      $$('.figure').forEach((f) => f.classList.toggle('tappable', canTap(Number(f.dataset.lane))));

      const paused = g.state === 'paused';
      $('#paused').hidden = !paused;
      if (paused) {
        $('#paused-why').textContent = allOnline() ? t('กดเล่นต่อเมื่อพร้อม') : t('รอหุ่นกลับมาออนไลน์…');
        $('#resume-btn').disabled = !allOnline();
      }
      $('#feed').innerHTML = S.feed
        .map((f) => `<li class="${f.kind}">${icon(f.kind === 'hit' ? 'bolt' : f.kind === 'wrong' ? 'x' : 'clock')}<b>${esc(f.text)}</b><span class="num">${f.rt != null ? fmtSeconds(f.rt) : ''}</span></li>`)
        .join('');
      this.frame();
    },
    onEvent(e) {
      this.update();
      if (e.kind !== 'hit') return;
      const card = $(`#score-${e.player}`);
      card?.classList.remove('bump');
      void card?.offsetWidth;
      card?.classList.add('bump');
      const streak = S.combos[e.player]?.streak ?? 0;
      if (streak >= 3) {
        const pop = $('#combo-pop');
        pop.textContent = streak >= 10 ? `UNSTOPPABLE ×${streak}` : streak >= 5 ? `ON FIRE ×${streak}` : `COMBO ×${streak}`;
        pop.classList.remove('show');
        void pop.offsetWidth;
        pop.classList.add('show');
      }
    },
    frame() {
      const g = S.game;
      if (!g) return;
      const now = hubNow();

      let remaining = g.remaining_ms;
      if (g.state === 'running') remaining = g.ends_at - now;
      const clock = $('#clock');
      if (clock) {
        clock.textContent = fmtClock(remaining);
        clock.classList.toggle('low', g.state === 'running' && remaining < 10000);
        $('#time-fill').style.width = `${clamp((remaining / (g.duration_s * 1000)) * 100, 0, 100)}%`;
      }

      const overlay = $('#countdown');
      if (g.state === 'countdown') {
        const n = Math.max(1, Math.ceil((g.countdown_ends_at - now) / 1000));
        overlay.hidden = false;
        if (n !== this.lastCountdown) {
          this.lastCountdown = n;
          const num = $('#countdown-num');
          num.textContent = n;
          num.style.animation = 'none';
          void num.offsetWidth;
          num.style.animation = '';
          if (S.startedHere && !Countdown.ready) Sound.tick();
        }
      } else {
        overlay.hidden = true;
        this.lastCountdown = null;
      }

      const t = performance.now();
      $$('#figures .zone').forEach((el) => {
        const lane = Number(el.dataset.lane);
        const zone = Number(el.dataset.zone);
        const flash = S.flashes[`${lane}:${zone}`];
        const lit = g.state === 'running' && g.lanes?.[lane]?.phase === 'active' && g.lanes[lane].target === zone;
        const flashing = flash && flash.until > t ? flash.kind : null;
        el.classList.toggle('lit', lit && !flashing);
        el.classList.toggle('hit', flashing === 'hit');
        el.classList.toggle('wrong', flashing === 'wrong');
        const enabled = S.settings?.nodes?.[lane]?.zones_enabled;
        el.classList.toggle('disabled', enabled != null && !(enabled & (1 << zone)));
      });
      for (let lane = 0; lane < LANES; lane++) {
        const ret = $(`#ret-${lane}`);
        if (!ret) continue;
        const l = g.lanes?.[lane];
        const on = g.state === 'running' && l?.phase === 'active' && l.target >= 0;
        if (on) {
          const c = zoneCenter(l.target);
          ret.setAttribute('transform', `translate(${c.x} ${c.y})`);
        }
        ret.classList.toggle('on', on);
      }
    },
  };
  Views.play = Play;

  // ------------------------------------------------------------------ game detail (result, history entry)

  function gameDetailHtml(g, live) {
    const duel = g.mode === 'duel';
    const meta = `${Stats.categoryLabel(Stats.categoryOf(g))}${g.ts ? ` · ${fmtDate(g.ts)}` : ''}`;
    const winner =
      duel && g.ended === 'finished'
        ? `<div class="winner-banner">${icon('crown', 'crown')}<span class="eyebrow">WINNER</span><b>${g.winner >= 0 ? nameOf(g.players[g.winner]) : t('เสมอ!')}</b></div>`
        : '';

    let challenge = '';
    const c = live ? S.challenge : null;
    if (c && c.category === Stats.categoryOf(g) && g.ended === 'finished') {
      const best = Math.max(...g.players.map((p) => p.score));
      challenge =
        best > c.score
          ? `<div class="panel challenge-panel won"><span class="eyebrow">${icon('trophy')} CHALLENGE CLEAR</span><b>${t('ชนะสถิติของ {name} แล้ว! (+{n})', { name: nameOf(c), n: best - c.score })}</b></div>`
          : `<div class="panel challenge-panel"><span class="eyebrow">${icon('flame')} CHALLENGE</span><b>${t('ขาดอีก {n} แต้ม จะชนะ {name}', { n: c.score + 1 - best, name: nameOf(c) })}</b></div>`;
    }

    let celebrate = false;
    const cards = g.players
      .map((p, i) => {
        const s = standing(g, i);
        const maxPts = Math.max(1, ...p.zone_points.map((v) => Math.abs(v)));
        const combo = live ? S.combos[i]?.best : null;
        let badge = '';
        if (g.ended !== 'finished') badge = `<span class="tag">${t('ไม่นับอันดับ (หยุดก่อนหมดเวลา)')}</span>`;
        else if (s.record) {
          badge = `<span class="tag hot">${icon('flame')} ${t('สถิติใหม่ อันดับ 1!')}</span>`;
          celebrate = live;
        }
        else if (s.pb && p.score > s.pb.score) badge = `<span class="tag good">${t('ทำลายสถิติตัวเอง +{n}', { n: p.score - s.pb.score })}</span>`;
        else if (s.pb) badge = `<span class="tag">${t('สถิติตัวเอง {n}', { n: s.pb.score })}</span>`;
        return `
          <div class="panel result-card ${duel ? `lane-${i}` : ''} ${g.winner === i ? 'is-winner' : ''}">
            <div class="row">
              <div><div class="name">${nameOf(p)}</div>${badge}</div>
              <span class="spacer"></span>${medallion(s.grade)}
            </div>
            <div class="row baseline">
              <div class="big-score num">${p.score}</div><span class="muted">${t('แต้ม')}</span>
              <span class="spacer"></span>
              ${g.ended === 'finished' ? `<div class="rank-big">${rankChip(s.rank)}<span class="muted small">${t('จาก {n}', { n: s.total })}</span></div>` : ''}
            </div>
            <div class="kv">
              <div><b>${p.hits}</b><span>${t('ยิงโดน')}</span></div>
              <div><b>${p.misses}</b><span>${t('หลุดเป้า')}</span></div>
              <div><b>${p.wrong}</b><span>${t('ผิดจุด')}</span></div>
              <div><b>${p.accuracy_pct}%</b><span>${t('ความแม่น')}</span></div>
              <div><b>${fmtSeconds(p.rt_avg_ms)}</b><span>${t('เวลาเฉลี่ย')}</span></div>
              <div><b>${combo ? `×${combo}` : fmtSeconds(p.rt_best_ms)}</b><span>${combo ? t('คอมโบสูงสุด') : t('เร็วที่สุด')}</span></div>
            </div>
            <div class="zone-bars">
              ${ZONES.map(
                (z, zi) => `<div class="zone-bar"><span>${t(z)}</span><span class="track"><i style="width:${(Math.max(0, p.zone_points[zi]) / maxPts) * 100}%"></i></span><span class="v num">${p.zone_hits[zi]} · ${p.zone_points[zi]}</span></div>`,
              ).join('')}
            </div>
          </div>`;
      })
      .join('');

    const actions = live
      ? `<button class="btn primary huge block" id="again-btn" type="button">${icon('replay')} ${t('เล่นซ้ำ')}</button>
         <div class="row two-btn">
           <button class="btn" type="button" id="chase-btn">${icon('flame')} ${t('ท้าชิงอันดับ 1')}</button>
           <a class="btn" href="#ranking">${t('ดูอันดับ')}</a>
         </div>
         <button class="btn ghost block" id="home-btn" type="button">${t('หน้าแรก')}</button>`
      : `<div class="row two-btn"><a class="btn" href="#history">${t('ประวัติทั้งหมด')}</a><a class="btn" href="#ranking">${t('อันดับ')}</a></div>`;

    return `
      <h1 class="page-title">${live ? t('สรุปผล') : t('เกม #{id}', { id: g.id })}</h1>
      <p class="muted">${meta}</p>
      ${celebrate ? confetti() : ''}
      <div class="stack">
        ${winner}${challenge}
        <div class="result-grid">${cards}</div>
        <div class="panel qr-panel">
          ${qrSvg(cardUrl(g.id), 168)}
          <div class="stack-sm">
            <b>${t('สแกนรับการ์ดผลการเล่น')}</b>
            <span class="muted small">${t('ใช้มือถือที่ต่อ WiFi สนามสแกน QR นี้ จะได้รูปสรุปผลเกมนี้ไว้เก็บหรือแชร์')}</span>
            <a class="btn small" href="#card/${g.id}">${t('เปิดการ์ดในเครื่องนี้')}</a>
          </div>
        </div>
        ${actions}
      </div>`;
  }

  Views.result = {
    html() {
      return S.result ? gameDetailHtml(S.result, true) : `<h1 class="page-title">${t('ยังไม่มีผลการเล่น')}</h1><a class="btn primary" href="#home">${t('หน้าแรก')}</a>`;
    },
    mount(root) {
      const g = S.result;
      if (!g) return;
      $('#again-btn', root)?.addEventListener('click', () => {
        // Same players, same rules, straight back into the countdown.
        S.setup.mode = g.mode === 'duel' ? 2 : 1;
        S.setup.names = g.players.map((p) => p.name);
        S.setup.difficulty = DIFFICULTY_KEYS.indexOf(g.difficulty);
        S.setup.duration = g.duration_s;
        startGame();
      });
      $('#chase-btn', root)?.addEventListener('click', () => {
        const top = Stats.board(Stats.categoryOf(g))[0];
        if (top) setChallenge(top);
      });
      $('#home-btn', root)?.addEventListener('click', () => {
        send({ t: 'reset' });
        S.challenge = null;
        go('home');
      });
    },
  };

  Views.game = {
    html: loading,
    async mount(root) {
      await Stats.load(true);
      if (route() !== 'game') return;
      const g = S.history.find((x) => String(x.id) === routeParam());
      root.innerHTML = g ? gameDetailHtml(g, false) : `<h1 class="page-title">${t('ไม่พบเกมนี้')}</h1><a class="btn" href="#history">${t('ประวัติ')}</a>`;
    },
  };

  // ------------------------------------------------------------------ views: ranking

  Views.ranking = {
    filter() {
      const f = S.ranking ?? { mode: S.setup.mode, difficulty: S.setup.difficulty, duration: S.setup.duration };
      return { ...f };
    },
    html() {
      const f = this.filter();
      const chips = (group, items, value) =>
        `<div class="chips compact" data-group="${group}">${items
          .map(([v, label]) => `<button class="chip" type="button" data-v="${v}" aria-pressed="${v === value}">${label}</button>`)
          .join('')}</div>`;
      return `
        <h1 class="page-title">${t('อันดับ')}</h1>
        <div class="panel stack-sm" id="rank-filters">
          ${chips('mode', [[1, t('เล่นคนเดียว')], [2, t('ดวล 2 คน')]], f.mode)}
          ${chips('difficulty', DIFFICULTY.map((d, i) => [i, t(d)]), f.difficulty)}
          ${chips('duration', DURATIONS.map((d) => [d, t('{s} วิ', { s: d })]), f.duration)}
        </div>
        <div id="board">${loading()}</div>`;
    },
    async mount(root) {
      $('#rank-filters', root).addEventListener('click', (ev) => {
        const chip = ev.target.closest('.chip');
        if (!chip) return;
        const f = this.filter();
        f[chip.closest('[data-group]').dataset.group] = Number(chip.dataset.v);
        S.ranking = f;
        saved.set('ranking', f);
        render();
      });
      await Stats.load();
      if (route() !== 'ranking') return;
      const f = this.filter();
      const board = Stats.board(Stats.category(f.mode, f.difficulty, f.duration));
      const box = $('#board', root);
      if (!board.length) {
        box.innerHTML = `<div class="panel"><b>${t('ยังไม่มีใครเล่นโหมดนี้')}</b><p class="muted">${t('ไปเล่นเป็นคนแรก แล้วชื่อคุณจะอยู่บนสุด')}</p><a class="btn primary" href="#home">${t('เล่นเลย')}</a></div>`;
        return;
      }
      const podium = [board[1], board[0], board[2]]
        .map((r, i) =>
          r
            ? `<a class="podium-step step-${[2, 1, 3][i]}" href="#game/${r.id}">
                 <span class="medal m${[2, 1, 3][i]}">${icon(i === 1 ? 'crown' : 'trophy')}<em>${[2, 1, 3][i]}</em></span><b class="nm">${nameOf(r)}</b>
                 <span class="pts num">${r.score}</span><span class="muted small">${r.accuracy_pct}%</span></a>`
            : '<span class="podium-step empty"></span>',
        )
        .join('');
      box.innerHTML = `
        <div class="podium">${podium}</div>
        <button class="btn primary block" type="button" id="beat-1">${icon('flame')} ${t('ท้าชิงอันดับ 1 · {score} แต้ม', { score: board[0].score })}</button>
        <ol class="board-list">${board
          .slice(0, 50)
          .map(
            (r, i) => `<li><a href="#game/${r.id}">${rankChip(i + 1)}<span class="nm">${nameOf(r)}<small>${fmtDate(r.ts) || `#${r.id}`}</small></span>
              ${gradeBadge(Stats.grade(r), 'sm')}<span class="num pts">${r.score}</span></a></li>`,
          )
          .join('')}</ol>`;
      $('#beat-1', root).addEventListener('click', () => setChallenge(board[0]));
    },
  };

  // ------------------------------------------------------------------ views: challenge

  Views.challenge = {
    html() {
      return `
        <h1 class="page-title">${t('ท้าทาย')}</h1>
        <p class="muted">${t('เลือกสถิติที่อยากล้ม แล้วระบบจะตั้งกติกาให้ตรงกัน ระหว่างเล่นจะมีแถบบอกว่าเหลืออีกกี่แต้ม')}</p>
        <div id="challenges" class="stack">${loading()}</div>`;
    },
    async mount(root) {
      await Stats.load();
      if (route() !== 'challenge') return;
      const cats = Stats.categories();
      const box = $('#challenges', root);
      if (!cats.length) {
        box.innerHTML = `<div class="panel"><b>${t('ยังไม่มีสถิติให้ท้าทาย')}</b><p class="muted">${t('เล่นเกมแรกเพื่อสร้างสถิติ')}</p><a class="btn primary" href="#home">${t('เล่นเลย')}</a></div>`;
        return;
      }
      const targets = [];
      box.innerHTML = cats
        .map((cat) => {
          const board = Stats.board(cat);
          const picks = [0, 2, 9].filter((i) => board[i]);
          return `
            <div class="panel challenge-cat">
              <div class="row"><b>${Stats.categoryLabel(cat)}</b><span class="spacer"></span><span class="muted small">${t('{n} ครั้ง', { n: board.length })}</span></div>
              <div class="record-holder">${rankChip(1)}<span class="nm">${esc(board[0].name)}</span><span class="pts num">${board[0].score}</span>${gradeBadge(Stats.grade(board[0]), 'sm')}</div>
              <div class="row">${picks
                .map((i) => {
                  targets.push(board[i]);
                  return `<button class="btn ${i === 0 ? 'primary' : ''} small" type="button" data-target="${targets.length - 1}">${t('ล้มอันดับ {n} ({score})', { n: i + 1, score: board[i].score })}</button>`;
                })
                .join('')}</div>
            </div>`;
        })
        .join('');
      box.addEventListener('click', (ev) => {
        const idx = ev.target.closest('[data-target]')?.dataset.target;
        if (idx != null) setChallenge(targets[Number(idx)]);
      });
    },
  };

  // ------------------------------------------------------------------ views: history and players

  const historyTabs = (active) => `
    <div class="chips" style="margin-bottom:14px">
      <a class="chip" href="#history" aria-pressed="${active === 'history'}">${t('เกมล่าสุด')}</a>
      <a class="chip" href="#players" aria-pressed="${active === 'players'}">${t('ผู้เล่นทั้งหมด')}</a>
    </div>`;

  Views.history = {
    html() {
      return `<h1 class="page-title">${t('ประวัติการเล่น')}</h1>${historyTabs('history')}<div id="history-body">${loading()}</div>
        ${DEMO ? '' : `<div class="links"><a class="btn ghost small" href="/export.csv" download>${t('ดาวน์โหลด CSV')}</a></div>`}`;
    },
    async mount(root) {
      await Stats.load(true);
      if (route() !== 'history') return;
      const games = [...S.history].reverse();
      $('#history-body', root).innerHTML = games.length
        ? `<div class="game-list">${games
            .map(
              (g) => `<a class="panel game-row" href="#game/${g.id}">
                <div class="row"><b>${Stats.categoryLabel(Stats.categoryOf(g))}</b><span class="spacer"></span><span class="muted small">${fmtDate(g.ts) || `#${g.id}`}</span></div>
                <div class="players">${g.players
                  .map((p, i) => `<span>${g.winner === i ? '👑 ' : ''}${nameOf(p)} <b class="num">${p.score}</b> ${gradeBadge(Stats.grade(p), 'sm')}</span>`)
                  .join('')}</div>
                ${g.ended === 'stopped' ? `<span class="tag">${t('หยุดก่อนหมดเวลา')}</span>` : ''}
              </a>`,
            )
            .join('')}</div>`
        : `<div class="panel"><p class="muted">${t('ยังไม่มีเกมที่เล่นจบ')}</p></div>`;
    },
  };

  Views.players = {
    html() {
      return `<h1 class="page-title">${t('ผู้เล่น')}</h1>${historyTabs('players')}<div id="players-body">${loading()}</div>`;
    },
    async mount(root) {
      await Stats.load();
      if (route() !== 'players') return;
      const players = Stats.players();
      $('#players-body', root).innerHTML = players.length
        ? `<ol class="board-list">${players
            .map(
              (p, i) => `<li><a href="#game/${p.best.id}">${rankChip(i + 1)}
                <span class="nm">${nameOf(p)}<small>${t('{n} เกม', { n: p.games })} · ${t('แม่น {n}%', { n: p.shots ? Math.round((p.hits / p.shots) * 100) : 0 })}${p.wins ? ` · ${t('ชนะ {n}', { n: p.wins })}` : ''}</small></span>
                <span class="num pts">${p.best.score}</span></a></li>`,
            )
            .join('')}</ol>
           <p class="muted small">${t('เรียงตามคะแนนสูงสุดของแต่ละคน (ทุกโหมดรวมกัน) แตะชื่อเพื่อดูเกมที่ดีที่สุด')}</p>`
        : `<div class="panel"><p class="muted">${t('ยังไม่มีผู้เล่น')}</p></div>`;
    },
  };

  // ------------------------------------------------------------------ result card image

  const CARD = { w: 1080, h: 1350 };
  const CARD_FONT = "'Kanit', 'Noto Sans Thai', sans-serif";
  const GRADE_COLORS = { S: '#ffd34f', A: '#7cff4f', B: '#4fd8ff', C: '#c9d3da', D: '#ff6b5c' };

  async function renderCard(g) {
    try {
      await Promise.all([
        document.fonts.load(`italic 800 100px ${CARD_FONT}`, 'TARGET 0123 ก'),
        document.fonts.load(`600 40px ${CARD_FONT}`, 'ยิงโดน ABC'),
      ]);
    } catch {
      /* fall back to system fonts */
    }
    const canvas = document.createElement('canvas');
    canvas.width = CARD.w;
    canvas.height = CARD.h;
    const ctx = canvas.getContext('2d');
    const font = (style) => `${style} ${CARD_FONT}`;
    const box = (x, y, w, h, r, fill, stroke) => {
      ctx.beginPath();
      ctx.roundRect(x, y, w, h, r);
      if (fill) {
        ctx.fillStyle = fill;
        ctx.fill();
      }
      if (stroke) {
        ctx.strokeStyle = stroke;
        ctx.lineWidth = 2;
        ctx.stroke();
      }
    };
    const text = (s, x, y, style, color, align = 'left') => {
      ctx.font = font(style);
      ctx.fillStyle = color;
      ctx.textAlign = align;
      ctx.fillText(s, x, y);
    };

    // Background: dark range, toxic glow, grid and target rings.
    const bg = ctx.createLinearGradient(0, 0, 0, CARD.h);
    bg.addColorStop(0, '#0d1712');
    bg.addColorStop(1, '#050706');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, CARD.w, CARD.h);
    for (const [x, y, r, c] of [[140, 120, 520, 'rgba(124,255,79,0.22)'], [980, 1250, 560, 'rgba(255,61,61,0.18)']]) {
      const glow = ctx.createRadialGradient(x, y, 0, x, y, r);
      glow.addColorStop(0, c);
      glow.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = glow;
      ctx.fillRect(0, 0, CARD.w, CARD.h);
    }
    ctx.strokeStyle = 'rgba(124,255,79,0.06)';
    ctx.lineWidth = 1;
    for (let x = 0; x < CARD.w; x += 54) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, CARD.h);
      ctx.stroke();
    }
    for (let y = 0; y < CARD.h; y += 54) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(CARD.w, y);
      ctx.stroke();
    }
    ctx.strokeStyle = 'rgba(124,255,79,0.10)';
    ctx.lineWidth = 6;
    for (const r of [90, 170, 250]) {
      ctx.beginPath();
      ctx.arc(930, 170, r, 0, Math.PI * 2);
      ctx.stroke();
    }

    // Header.
    ctx.shadowColor = 'rgba(124,255,79,0.6)';
    ctx.shadowBlur = 30;
    text('TARGET RANGE', 70, 140, 'italic 800 92px', '#7cff4f');
    ctx.shadowBlur = 0;
    text(`${Stats.categoryLabel(Stats.categoryOf(g))}${g.ts ? `  ·  ${fmtDate(g.ts)}` : ''}`, 72, 200, '600 34px', '#9fb3a6');

    const finished = g.ended === 'finished';
    if (g.players.length === 1) {
      const s = standing(g, 0);
      const p = s.p;
      text(playerName(p.name), 72, 310, '600 64px', '#ffffff');
      // Grade medallion.
      ctx.beginPath();
      ctx.arc(900, 420, 120, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(0,0,0,0.45)';
      ctx.fill();
      ctx.lineWidth = 10;
      ctx.strokeStyle = GRADE_COLORS[s.grade];
      ctx.stroke();
      text(s.grade, 900, 475, 'italic 800 160px', GRADE_COLORS[s.grade], 'center');

      text(t('คะแนน'), 72, 400, '600 36px', '#9fb3a6');
      ctx.shadowColor = 'rgba(255,255,255,0.25)';
      ctx.shadowBlur = 24;
      text(String(p.score), 64, 600, 'italic 800 230px', '#ffffff');
      ctx.shadowBlur = 0;
      if (finished) {
        const line = s.record ? t('🔥 สถิติใหม่ อันดับ 1!') : t('อันดับ #{rank} จาก {total}', { rank: s.rank, total: s.total });
        box(72, 640, 520, 76, 38, s.record ? 'rgba(255,211,79,0.18)' : 'rgba(124,255,79,0.12)', s.record ? '#ffd34f' : '#7cff4f');
        text(line, 332, 692, '600 40px', s.record ? '#ffd34f' : '#7cff4f', 'center');
      }

      const stats = [
        [t('ยิงโดน'), p.hits], [t('หลุดเป้า'), p.misses], [t('ผิดจุด'), p.wrong],
        [t('ความแม่น'), `${p.accuracy_pct}%`], [t('เฉลี่ย'), fmtSeconds(p.rt_avg_ms)], [t('เร็วสุด'), fmtSeconds(p.rt_best_ms)],
      ];
      stats.forEach(([label, value], i) => {
        const x = 72 + (i % 3) * 318;
        const y = 760 + Math.floor(i / 3) * 150;
        box(x, y, 296, 130, 22, 'rgba(255,255,255,0.06)', 'rgba(124,255,79,0.18)');
        text(String(value), x + 148, y + 72, 'italic 800 58px', '#ffffff', 'center');
        text(label, x + 148, y + 112, '600 28px', '#9fb3a6', 'center');
      });

      const maxPts = Math.max(1, ...p.zone_points.map((v) => Math.abs(v)));
      ZONES.forEach((z, zi) => {
        const y = 1090 + zi * 40;
        text(t(z), 72, y + 22, '600 28px', '#c9d3da');
        box(230, y, 640, 22, 11, 'rgba(255,255,255,0.08)');
        const w = (Math.max(0, p.zone_points[zi]) / maxPts) * 640;
        if (w > 0) box(230, y, w, 22, 11, '#7cff4f');
        text(`${p.zone_hits[zi]} · ${p.zone_points[zi]}`, 1008, y + 22, '600 28px', '#ffffff', 'right');
      });
    } else {
      g.players.forEach((p, i) => {
        const s = standing(g, i);
        const x = 60 + i * 490;
        const isWinner = finished && g.winner === i;
        box(x, 260, 470, 880, 32, isWinner ? 'rgba(255,211,79,0.10)' : 'rgba(255,255,255,0.05)', isWinner ? '#ffd34f' : 'rgba(124,255,79,0.25)');
        text(isWinner ? '👑 WINNER' : t('หุ่น {n}', { n: i + 1 }), x + 235, 330, '600 36px', isWinner ? '#ffd34f' : '#9fb3a6', 'center');
        text(playerName(p.name), x + 235, 410, '600 52px', '#ffffff', 'center');
        text(String(p.score), x + 235, 600, 'italic 800 190px', '#ffffff', 'center');
        text(t('แต้ม'), x + 235, 650, '600 32px', '#9fb3a6', 'center');
        text(s.grade, x + 235, 800, 'italic 800 130px', GRADE_COLORS[s.grade], 'center');
        const lines = [
          t('โดน {h} · หลุด {m} · ผิด {w}', { h: p.hits, m: p.misses, w: p.wrong }),
          t('แม่น {a}% · เฉลี่ย {s}', { a: p.accuracy_pct, s: fmtSeconds(p.rt_avg_ms) }),
        ];
        if (finished) lines.push(s.record ? t('🔥 สถิติใหม่!') : t('อันดับ #{rank} จาก {total}', { rank: s.rank, total: s.total }));
        lines.forEach((l, li) => text(l, x + 235, 900 + li * 56, '600 32px', li === 2 ? '#7cff4f' : '#e8eee9', 'center'));
      });
      if (finished && g.winner < 0) text(t('เสมอ!'), CARD.w / 2, 1200, 'italic 800 80px', '#ffd34f', 'center');
    }

    text(t('ยิงให้ไว · ยิงให้แม่น · ขึ้นอันดับ 1'), 72, 1310, '600 30px', '#7cff4f');
    text(`#${g.id}`, 1008, 1310, '600 30px', '#5f7468', 'right');
    return canvas;
  }

  // ------------------------------------------------------------------ views: join poster (print)

  const HUB_URL = 'http://192.168.4.1/'; // fixed soft-AP address set in hub firmware
  // Standard Wi-Fi QR payload; iOS and Android cameras both offer to join from it.
  const wifiQrText = (ssid, pass) => {
    const q = (v) => String(v).replace(/([\\;,:"])/g, '\\$1');
    return `WIFI:T:WPA;S:${q(ssid)};P:${q(pass)};;`;
  };

  Views.poster = {
    html() {
      if (!S.admin) {
        return `
          <h1>${t('ป้าย QR เข้าร่วมเกม')}</h1>
          <div class="card stack">
            <p>${t('ป้ายนี้มีรหัส WiFi จึงต้องเข้าระบบผู้ดูแลก่อน')}</p>
            <a class="btn primary block" href="#admin">${t('ไปหน้าผู้ดูแล')}</a>
          </div>`;
      }
      const w = S.settings?.wifi;
      if (!w?.password) return `<h1>${t('ป้าย QR เข้าร่วมเกม')}</h1><div class="card"><p class="muted">${t('กำลังโหลดค่า WiFi…')}</p></div>`;
      return `
        <div class="poster-tools no-print">
          <h1>${t('ป้าย QR เข้าร่วมเกม')}</h1>
          <p class="muted small">${t('พิมพ์ขนาด A4 แนวตั้ง ติดที่สนามหรือหน้าเคาน์เตอร์ ถ้าเปลี่ยนชื่อหรือรหัส WiFi ต้องพิมพ์ใหม่')}</p>
          <div class="row">
            <button class="btn primary" type="button" id="print-poster">${t('พิมพ์ / บันทึก PDF')}</button>
            <a class="btn ghost" href="#admin">${t('กลับ')}</a>
          </div>
        </div>
        <article class="poster" aria-label="${t('ป้ายวิธีเข้าร่วมเกม')}">
          <header class="poster-head">
            <span class="poster-mark">${icon('target')}</span>
            <div>
              <h2>TARGET RANGE</h2>
              <p>${t('สแกน 2 ขั้น แล้วยิงได้เลย')}</p>
            </div>
          </header>

          <div class="poster-steps">
            <section class="poster-step">
              <span class="step-no">1</span>
              <h3>${t('สแกนต่อ WiFi')}</h3>
              ${qrSvg(wifiQrText(w.ssid, w.password), 260)}
              <dl class="poster-wifi">
                <dt>WiFi</dt><dd>${esc(w.ssid)}</dd>
                <dt>${t('รหัส')}</dt><dd>${esc(w.password)}</dd>
              </dl>
            </section>
            <section class="poster-step">
              <span class="step-no">2</span>
              <h3>${t('สแกนเปิดเกม')}</h3>
              ${qrSvg(HUB_URL, 260)}
              <p class="poster-url">${HUB_URL.replace(/\/$/, '')}</p>
              <p class="poster-hint">${t('ส่วนใหญ่หน้าเกมจะเด้งขึ้นเองหลังต่อ WiFi')}</p>
            </section>
          </div>

          <section class="poster-play">
            <span class="step-no">3</span>
            <div>
              <h3>${t('เลือกโหมด ใส่ชื่อ กด START')}</h3>
              <p>${t('เล่นคนเดียว หรือดวล 2 คน · ยิงเฉพาะจุดที่ไฟติด · ติดอันดับบนจอทันที')}</p>
            </div>
          </section>

          <ul class="poster-notes">
            <li><b>Android:</b> ${t('ถ้าขึ้นว่า "ไม่มีอินเทอร์เน็ต" ให้กด <b>ใช้ต่อไป / เชื่อมต่อต่อไป</b>')}</li>
            <li><b>iPhone:</b> ${t('ถ้าหน้าเกมไม่ขึ้น ให้สแกนขั้นที่ 2 หรือพิมพ์ {url} ใน Safari', { url: HUB_URL.replace(/^http:\/\/|\/$/g, '') })}</li>
            <li>${t('WiFi นี้ใช้เล่นเกมเท่านั้น ไม่มีอินเทอร์เน็ต · สวมแว่นป้องกันตาทุกครั้ง')}</li>
          </ul>
        </article>`;
    },
    mount(root) {
      if (S.admin && !S.settings?.wifi?.password) send({ t: 'get_settings' });
      $('#print-poster', root)?.addEventListener('click', () => window.print());
    },
  };

  Views.card = {
    html: loading,
    async mount(root) {
      await Stats.load(true);
      if (route() !== 'card') return;
      const g = S.history.find((x) => String(x.id) === routeParam());
      if (!g) {
        root.innerHTML = `<h1 class="page-title">${t('ไม่พบเกมนี้')}</h1><p class="muted">${t('มือถือต้องต่อ WiFi ของสนามจึงจะเปิดการ์ดได้')}</p>`;
        return;
      }
      const canvas = await renderCard(g);
      const url = canvas.toDataURL('image/png');
      const file = `target-range-${g.id}.png`;
      root.innerHTML = `
        <h1 class="page-title">${t('การ์ดผลการเล่น')}</h1>
        <img class="card-img" src="${url}" alt="${t('สรุปผลเกม #{id}', { id: g.id })}">
        <p class="muted small" style="text-align:center">${t('กดค้างที่รูปเพื่อบันทึกลงเครื่อง หรือใช้ปุ่มด้านล่าง')}</p>
        <div class="row two-btn">
          <a class="btn primary" href="${url}" download="${file}">${icon('download')} ${t('บันทึกรูป')}</a>
          <button class="btn" type="button" id="share-btn" hidden>${icon('share')} ${t('แชร์')}</button>
        </div>
        <a class="btn ghost block" href="#game/${g.id}">${t('ดูรายละเอียดเกม')}</a>`;
      canvas.toBlob((blob) => {
        if (!blob) return;
        const shareFile = new File([blob], file, { type: 'image/png' });
        if (!navigator.canShare?.({ files: [shareFile] })) return;
        const btn = $('#share-btn', root);
        btn.hidden = false;
        btn.addEventListener('click', () => navigator.share({ files: [shareFile], title: 'Target Range' }).catch(() => {}));
      });
    },
  };

  // ------------------------------------------------------------------ backup / restore

  const fmtBytes = (n) =>
    n >= 1 << 30 ? `${(n / (1 << 30)).toFixed(1)} GB` : n >= 1 << 20 ? `${(n / (1 << 20)).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`;

  const DataIO = {
    stamp() {
      const d = new Date();
      const p = (v) => String(v).padStart(2, '0');
      return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}`;
    },

    save(blob, name) {
      const url = URL.createObjectURL(blob);
      const a = Object.assign(document.createElement('a'), { href: url, download: name });
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 10000);
    },

    async errorText(res) {
      try {
        return (await res.json()).msg || `HTTP ${res.status}`;
      } catch {
        return `HTTP ${res.status}`;
      }
    },

    async backup() {
      if (DEMO) return toast('(จำลอง) ดาวน์โหลดไฟล์สำรอง', 'ok');
      try {
        const res = await fetch(`/api/backup?pin=${encodeURIComponent(S.pin ?? '')}`, { cache: 'no-store' });
        if (!res.ok) return toast(await this.errorText(res), 'bad');
        this.save(await res.blob(), `target-range-backup-${this.stamp()}.jsonl`);
        toast('ดาวน์โหลดไฟล์สำรองแล้ว', 'ok');
      } catch {
        toast('ดาวน์โหลดไม่สำเร็จ ตรวจว่ายังต่อ WiFi ของ hub อยู่', 'bad');
      }
    },

    async restore(file) {
      if (!file) return;
      // Check the header here so an obviously wrong file never reaches the hub.
      const head = await file.slice(0, 4096).text();
      let header = null;
      try {
        header = JSON.parse(head.split('\n')[0]);
      } catch {
        /* handled below */
      }
      if (header?.t !== 'target-range-backup') return toast('ไฟล์นี้ไม่ใช่ไฟล์สำรองของ Target Range', 'bad');
      const games = Math.max(0, (await file.text()).split('\n').filter((l) => l.trim()).length - 1);
      if (window.confirm?.(`กู้คืนประวัติ ${games} เกมและค่าตั้งทั้งหมดจากไฟล์นี้? ข้อมูลปัจจุบันในบอร์ดจะถูกเขียนทับ`) === false) return;
      if (DEMO) return toast(`(จำลอง) กู้คืน ${games} เกมแล้ว`, 'ok');
      const btn = $('#restore-btn');
      if (btn) btn.disabled = true;
      try {
        const res = await fetch(`/api/restore?pin=${encodeURIComponent(S.pin ?? '')}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: file,
        });
        // 202: uploaded; the hub reports the outcome over the WebSocket, then reboots.
        toast(res.ok ? 'อัปโหลดแล้ว กำลังตรวจสอบไฟล์…' : await this.errorText(res), res.ok ? '' : 'bad');
      } catch {
        toast('อัปโหลดไม่สำเร็จ ตรวจว่ายังต่อ WiFi ของ hub อยู่', 'bad');
      } finally {
        if (btn) btn.disabled = false;
      }
    },
  };

  // ------------------------------------------------------------------ views: admin

  const Admin = {
    html() {
      if (!S.admin) {
        return `
          <h1>ผู้ดูแลระบบ</h1>
          <form class="card stack" id="pin-form">
            <label class="field">PIN ผู้ดูแล
              <input type="password" id="pin" inputmode="numeric" autocomplete="off" maxlength="8" required>
            </label>
            <button class="btn primary block" type="submit">เข้าสู่ระบบ</button>
            <p class="muted small">PIN เริ่มต้นคือ 1234 ควรเปลี่ยนหลังติดตั้ง</p>
          </form>
          <div class="links"><a class="btn ghost" href="#home">หน้าแรก</a></div>`;
      }
      const nodeChips = [1, 2]
        .map((id) => `<button class="chip" type="button" data-node="${id}" aria-pressed="${S.tuneNode === id}">หุ่น ${id}</button>`)
        .join('');
      return `
        <h1>ผู้ดูแลระบบ</h1>
        <div class="stack" id="admin-root">
          <a class="card poster-link" href="#poster">
            <span class="poster-link-icon">${icon('target')}</span>
            <span><b>ป้าย QR เข้าร่วมเกม</b><span class="muted small">พิมพ์ติดสนาม: สแกนต่อ WiFi แล้วเปิดเกม</span></span>
            ${icon('chevron')}
          </a>

          <details class="card" open>
            <summary>สถานะหุ่น</summary>
            <div class="stack" id="nodes-detail"></div>
          </details>

          <details class="card" open>
            <summary>จูนเซนเซอร์</summary>
            <div class="stack">
              <div class="chips" id="tune-node">${nodeChips}</div>
              <p class="muted small">ยิงหรือเคาะแผ่นโซน แท่งจะแสดงสัญญาณล่าสุดทั้ง 5 ช่อง เส้นแดงคือ threshold แท่งสีเขียวคือโซนที่ระบบตัดสินว่าโดน</p>
              <div class="bars" id="bars"></div>
              <label class="toggle"><input type="checkbox" id="tune-live"> โหมดจูน (ไวขึ้น แสดงสัญญาณอ่อน ๆ ด้วย)</label>
              <form id="thr-form" class="stack">
                <div>
                  <div class="grid-5">${ZONES.map((z, i) => `<label><span class="mini-label">${z}</span><input type="number" min="10" max="4095" id="thr-${i}" required></label>`).join('')}</div>
                </div>
                <div class="grid-5">${ZONES.map((z, i) => `<label class="toggle small" style="justify-content:center"><input type="checkbox" id="zen-${i}"> ใช้</label>`).join('')}</div>
                <button class="btn primary" type="submit">บันทึก threshold</button>
              </form>
              <div class="card stack" style="background:var(--surface-2)">
                <h3>ทดสอบ crosstalk (แนะนำทำทุกครั้งหลังประกอบ)</h3>
                <p class="muted small">ระบบจะเปิดไฟสีฟ้าทีละโซน ยิงโซนที่ไฟติดตามจำนวนนัด ครบ 5 โซนแล้วระบบจะเสนอ threshold ให้</p>
                <div class="row">
                  <select id="xt-shots" style="max-width:140px">${[3, 5, 8, 10].map((n) => `<option value="${n}" ${n === 5 ? 'selected' : ''}>${n} นัด/โซน</option>`).join('')}</select>
                  <button class="btn primary small" type="button" id="xt-start">เริ่มทดสอบ</button>
                  <button class="btn small" type="button" id="xt-cancel">ยกเลิก</button>
                </div>
                <div id="xt-status" class="small"></div>
                <div id="xt-report"></div>
              </div>
            </div>
          </details>

          <details class="card">
            <summary>ทดสอบไฟและยิงจำลอง</summary>
            <div class="stack">
              ${[1, 2]
                .map(
                  (id) => `<div><b>หุ่น ${id}</b><div class="chips" style="margin-top:6px">${ZONES.map(
                    (z, i) => `<button class="chip" type="button" data-led="${id}:${i}">${i + 1} ${z}</button>`,
                  ).join('')}</div></div>`,
                )
                .join('')}
              <label class="toggle"><input type="checkbox" id="sim-tap" ${S.simTap ? 'checked' : ''}> แตะโซนบนหน้าเกมเพื่อจำลองการยิง (ใช้ทดสอบโดยไม่ต้องยิงจริง)</label>
              <label class="toggle"><input type="checkbox" id="sim-nodes" ${anySim() ? 'checked' : ''}> โหมดทดสอบ: จำลองหุ่นที่ยังไม่ได้ต่อ (เริ่มเกมได้ และทุกเครื่องแตะโซนเพื่อยิงได้ ปิดเองเมื่อรีสตาร์ท hub)</label>
            </div>
          </details>

          <details class="card">
            <summary>กติกาเกม</summary>
            <form id="game-form" class="stack">
              <div><span class="mini-label" style="text-align:left">คะแนนต่อโซน</span>
                <div class="grid-5">${ZONES.map((z, i) => `<label><span class="mini-label">${z}</span><input type="number" min="0" max="100" id="pts-${i}" required></label>`).join('')}</div>
              </div>
              <label class="field">คะแนนที่หักเมื่อยิงผิดจุด<input type="number" min="0" max="100" id="penalty" required></label>
              <label class="field">ช่วงผ่อนผันหลังไฟดับ (ms)<input type="number" min="0" max="1000" id="grace" required></label>
              ${DIFFICULTY.map(
                (d, i) => `<div><span class="mini-label" style="text-align:left">${d} (ms): ไฟติด · พักต่ำสุด · พักสูงสุด</span>
                  <div class="grid-3">
                    <input type="number" min="300" max="20000" id="act-${i}" required>
                    <input type="number" min="${MIN_GAP_MS}" max="10000" id="gmin-${i}" required>
                    <input type="number" min="${MIN_GAP_MS}" max="10000" id="gmax-${i}" required>
                  </div></div>`,
              ).join('')}
              <p class="muted small">เวลาพักระหว่างเป้าต้องไม่ต่ำกว่า ${MIN_GAP_MS / 1000} วินาที เพื่อให้ผู้เล่นเล็งเป้าถัดไปทัน</p>
              <button class="btn primary" type="submit">บันทึกกติกา</button>
            </form>
          </details>

          <details class="card">
            <summary>ฮาร์ดแวร์ไฟและเซนเซอร์</summary>
            <form id="hw-form" class="stack">
              <label class="field">Lockout หลังยิงโดน (ms) <input type="number" min="20" max="1000" id="hw-lock" required></label>
              <label class="field">หน้าต่างตัดสินโซน (ms) <input type="number" min="2" max="30" id="hw-arb" required></label>
              <label class="field">ความสว่างไฟ (10–255) <input type="number" min="10" max="255" id="hw-bri" required></label>
              <label class="field">ลำดับสีของโมดูลไฟ
                <select id="hw-order">${COLOR_ORDERS.map((o) => `<option>${o}</option>`).join('')}</select>
              </label>
              <label class="field">ความเร็วสัญญาณไฟ
                <select id="hw-khz"><option value="800">800 kHz (ปกติ)</option><option value="400">400 kHz (รุ่นเก่า)</option></select>
              </label>
              <label class="field">ความดังลำโพงที่หุ่น (0–30) <input type="number" min="0" max="30" id="hw-vol" required></label>
              <label class="field">จำนวนไฟล์เสียงยิงโดนใน microSD (สุ่มเล่น) <input type="number" min="1" max="20" id="hw-tracks" required></label>
              <button class="btn primary" type="submit">บันทึกและส่งไปที่หุ่น</button>
            </form>
          </details>

          <details class="card">
            <summary>ข้อมูล สำรอง และ SD card</summary>
            <div class="stack">
              <div id="storage-status" class="stack"></div>
              <div class="card stack" style="background:var(--surface-2)">
                <h3>สำรองข้อมูล</h3>
                <p class="muted small">ไฟล์เดียวมีประวัติเกมในบอร์ดทั้งหมดและค่าตั้งทุกอย่าง (รวมรหัส WiFi และ PIN) ควรสำรองหลังจูนเสร็จ และก่อนเปลี่ยนบอร์ด เก็บไฟล์ไว้ในที่ปลอดภัย</p>
                <div class="row">
                  <button class="btn primary small" type="button" id="backup-btn">${icon('download')} ดาวน์โหลดไฟล์สำรอง</button>
                  ${DEMO ? '' : '<a class="btn small" href="/export.csv" download>ประวัติ CSV</a>'}
                </div>
              </div>
              <div class="card stack" style="background:var(--surface-2)">
                <h3>กู้คืนจากไฟล์สำรอง</h3>
                <p class="muted small">ข้อมูลประวัติและค่าตั้งปัจจุบันจะถูกเขียนทับ แล้ว hub จะรีสตาร์ทเอง ทำได้เฉพาะตอนไม่มีเกม</p>
                <input type="file" id="restore-file" accept=".jsonl,.json,.txt,application/x-ndjson,application/json,text/plain">
                <button class="btn danger small" type="button" id="restore-btn" disabled>กู้คืน</button>
              </div>
              <div class="card stack" style="background:var(--surface-2)">
                <h3>คลังถาวรใน SD card</h3>
                <p class="muted small">ทุกเกมถูกบันทึกลงการ์ดด้วยและไม่ถูกลบ (ในบอร์ดเก็บแค่ล่าสุด) ถ้าถอดการ์ดไป เกมที่เล่นระหว่างนั้นจะถูกคัดลอกตามไปเมื่อใส่การ์ดกลับ</p>
                <div id="sd-status" class="stack"></div>
                ${DEMO ? '' : `<div class="row">
                  <a class="btn small" href="/api/archive.csv" download>คลังทั้งหมด CSV</a>
                  <a class="btn small ghost" href="/api/archive.jsonl" download>คลังทั้งหมด JSONL</a>
                </div>`}
              </div>
            </div>
          </details>

          <details class="card">
            <summary>WiFi, PIN และระบบ</summary>
            <div class="stack">
              <form id="wifi-form" class="stack">
                <label class="field">ชื่อ WiFi<input type="text" id="wifi-ssid" maxlength="31" required></label>
                <label class="field">รหัส WiFi (8 ตัวขึ้นไป)<input type="password" id="wifi-pass" minlength="8" maxlength="63" required></label>
                <label class="field">ช่อง (1–13)<input type="number" min="1" max="13" id="wifi-ch" required></label>
                <button class="btn" type="submit">บันทึก WiFi</button>
              </form>
              <form id="pin-change" class="row">
                <input type="password" id="new-pin" inputmode="numeric" minlength="4" maxlength="8" placeholder="PIN ใหม่ 4–8 หลัก" style="flex:1" required>
                <button class="btn" type="submit">เปลี่ยน PIN</button>
              </form>
              <button class="btn danger" type="button" id="reboot-hub">รีสตาร์ท hub</button>
            </div>
          </details>
        </div>
        <div class="links"><a class="btn ghost" href="#home">หน้าแรก</a></div>`;
    },

    mount(root) {
      if (!S.admin) {
        $('#pin-form', root).addEventListener('submit', (ev) => {
          ev.preventDefault();
          S.pin = $('#pin', root).value;
          send({ t: 'auth', pin: S.pin });
        });
        setTimeout(() => $('#pin', root)?.focus(), 50);
        return;
      }
      send({ t: 'get_settings' });

      $('#tune-node', root).addEventListener('click', (ev) => {
        const chip = ev.target.closest('.chip');
        if (!chip) return;
        S.tuneNode = Number(chip.dataset.node);
        $$('#tune-node .chip', root).forEach((c) => c.setAttribute('aria-pressed', String(c === chip)));
        this.fillForms();
        this.updateBars();
        this.updateCrosstalk();
      });
      $('#tune-live', root).addEventListener('change', (ev) => send({ t: 'tune', node: S.tuneNode, on: ev.target.checked }));
      $('#thr-form', root).addEventListener('submit', (ev) => {
        ev.preventDefault();
        let mask = 0;
        ZONES.forEach((_, i) => ($(`#zen-${i}`).checked ? (mask |= 1 << i) : 0));
        send({
          t: 'save_node',
          node: S.tuneNode,
          threshold: ZONES.map((_, i) => Number($(`#thr-${i}`).value)),
          zones_enabled: mask,
        });
      });
      $('#xt-start', root).addEventListener('click', () =>
        send({ t: 'crosstalk_start', node: S.tuneNode, shots: Number($('#xt-shots').value) }),
      );
      $('#xt-cancel', root).addEventListener('click', () => send({ t: 'crosstalk_cancel' }));
      $('#xt-report', root).addEventListener('click', (ev) => {
        if (ev.target.id === 'xt-apply') send({ t: 'apply_suggested', node: S.crosstalk.node });
      });

      $('#admin-root', root).addEventListener('click', (ev) => {
        const led = ev.target.closest('[data-led]');
        if (led) {
          const [node, zone] = led.dataset.led.split(':').map(Number);
          send({ t: 'led_test', node, zone });
        }
        const action = ev.target.closest('[data-node-action]');
        if (action) {
          const [cmd, node] = action.dataset.nodeAction.split(':');
          if (cmd === 'reboot_node' && !window.confirm?.('รีสตาร์ทหุ่นนี้?')) return;
          send({ t: cmd, node: Number(node) });
        }
      });
      $('#sim-tap', root).addEventListener('change', (ev) => (S.simTap = ev.target.checked));
      $('#sim-nodes', root).addEventListener('change', (ev) => send({ t: 'sim_nodes', on: ev.target.checked }));

      $('#game-form', root).addEventListener('submit', (ev) => {
        ev.preventDefault();
        send({
          t: 'save_game',
          game: {
            zone_points: ZONES.map((_, i) => Number($(`#pts-${i}`).value)),
            wrong_penalty: Number($('#penalty').value),
            grace_ms: Number($('#grace').value),
            difficulty: DIFFICULTY.map((_, i) => ({
              active_ms: Number($(`#act-${i}`).value),
              gap_min_ms: Number($(`#gmin-${i}`).value),
              gap_max_ms: Number($(`#gmax-${i}`).value),
            })),
          },
        });
      });
      $('#hw-form', root).addEventListener('submit', (ev) => {
        ev.preventDefault();
        send({
          t: 'save_hardware',
          hardware: {
            lockout_ms: Number($('#hw-lock').value),
            arbiter_ms: Number($('#hw-arb').value),
            brightness: Number($('#hw-bri').value),
            color_order: $('#hw-order').value,
            led_khz: Number($('#hw-khz').value),
            volume: Number($('#hw-vol').value),
            hit_tracks: Number($('#hw-tracks').value),
          },
        });
      });
      $('#wifi-form', root).addEventListener('submit', (ev) => {
        ev.preventDefault();
        send({
          t: 'save_wifi',
          wifi: { ssid: $('#wifi-ssid').value.trim(), password: $('#wifi-pass').value, channel: Number($('#wifi-ch').value) },
        });
      });
      $('#pin-change', root).addEventListener('submit', (ev) => {
        ev.preventDefault();
        const pin = $('#new-pin').value;
        if (send({ t: 'set_pin', pin })) S.pin = pin;
      });
      $('#backup-btn', root).addEventListener('click', () => DataIO.backup());
      $('#restore-file', root).addEventListener('change', (ev) => ($('#restore-btn').disabled = !ev.target.files?.length));
      $('#restore-btn', root).addEventListener('click', () => DataIO.restore($('#restore-file').files?.[0]));
      $('#reboot-hub', root).addEventListener('click', () => {
        if (window.confirm?.('รีสตาร์ท hub? มือถือทุกเครื่องจะหลุดชั่วคราว') !== false) send({ t: 'reboot_hub' });
      });

      this.fillForms();
      this.updateNodes();
      this.updateBars();
      this.updateCrosstalk();
      this.updateStorage();
    },

    updateStorage() {
      const box = $('#storage-status');
      const sdBox = $('#sd-status');
      if (!box) return;
      const st = S.storage;
      if (!st) {
        box.innerHTML = '<span class="muted small">กำลังอ่านสถานะ…</span>';
        sdBox.innerHTML = '';
        return;
      }
      const f = st.flash;
      const pct = f.total ? Math.round((f.used / f.total) * 100) : 0;
      box.innerHTML = `
        <div class="node-row">
          <div class="node-head"><b>หน่วยความจำในบอร์ด</b><span class="tag ${f.total ? (pct > 85 ? 'warn' : 'ok') : 'bad'}">${f.total ? `ใช้ ${pct}%` : 'ใช้งานไม่ได้'}</span></div>
          <div class="meter" role="img" aria-label="ใช้ไป ${pct}%"><i style="width:${pct}%"></i></div>
          <span class="muted small num">${fmtBytes(f.used)} / ${fmtBytes(f.total)} · ประวัติ ${f.games} จาก ${f.max_games} เกมล่าสุด</span>
        </div>`;
      const sd = st.sd;
      if (!sd.mounted) {
        sdBox.innerHTML = `<div class="node-row">
          <div class="node-head"><b>SD card</b><span class="tag bad">ไม่พร้อม</span></div>
          <span class="muted small">${esc(sd.error || 'ไม่พบการ์ด')} · เกมยังเล่นและบันทึกในบอร์ดได้ตามปกติ ใส่การ์ด (FAT32) แล้วระบบจะเริ่มใช้เองภายใน 5 วินาที</span>
          ${sd.last_id ? `<span class="muted small">บันทึกล่าสุดบนการ์ดถึงเกม #${sd.last_id}</span>` : ''}
        </div>`;
        return;
      }
      const sdPct = sd.total_bytes ? Math.round(((sd.total_bytes - sd.free_bytes) / sd.total_bytes) * 100) : 0;
      sdBox.innerHTML = `<div class="node-row">
        <div class="node-head"><b>SD card</b><span class="tag ok">พร้อมใช้</span>${sd.error ? `<span class="tag warn">${esc(sd.error)}</span>` : ''}</div>
        <div class="meter" role="img" aria-label="ใช้ไป ${sdPct}%"><i style="width:${sdPct}%"></i></div>
        <span class="muted small num">คลัง ${fmtBytes(sd.file_bytes)} · ว่าง ${fmtBytes(sd.free_bytes)} จาก ${fmtBytes(sd.total_bytes)}</span>
        <span class="muted small">บันทึกถึงเกม #${sd.last_id || '-'} · รอบนี้บันทึกไป ${sd.written} เกม</span>
      </div>`;
    },

    fillForms() {
      const s = S.settings;
      if (!s || !$('#game-form')) return;
      const set = (id, v) => {
        const el = $(`#${id}`);
        if (el && document.activeElement !== el) el.value = v;
      };
      s.game.zone_points.forEach((v, i) => set(`pts-${i}`, v));
      set('penalty', s.game.wrong_penalty);
      set('grace', s.game.grace_ms);
      s.game.difficulty.forEach((d, i) => {
        set(`act-${i}`, d.active_ms);
        set(`gmin-${i}`, d.gap_min_ms);
        set(`gmax-${i}`, d.gap_max_ms);
      });
      set('hw-lock', s.hardware.lockout_ms);
      set('hw-arb', s.hardware.arbiter_ms);
      set('hw-bri', s.hardware.brightness);
      set('hw-order', s.hardware.color_order);
      set('hw-khz', String(s.hardware.led_khz));
      set('hw-vol', s.hardware.volume ?? 25);
      set('hw-tracks', s.hardware.hit_tracks ?? 1);
      set('wifi-ssid', s.wifi.ssid);
      set('wifi-ch', s.wifi.channel);
      const node = s.nodes[S.tuneNode - 1];
      if (node) {
        node.threshold.forEach((v, i) => set(`thr-${i}`, v));
        ZONES.forEach((_, i) => {
          const cb = $(`#zen-${i}`);
          if (cb) cb.checked = Boolean(node.zones_enabled & (1 << i));
        });
      }
    },

    updateNodes() {
      const box = $('#nodes-detail');
      if (!box) return;
      const simBox = $('#sim-nodes');
      if (simBox) simBox.checked = S.nodesDetail.some((x) => x.sim);
      box.innerHTML = [1, 2]
        .map((id) => {
          const n = S.nodesDetail.find((x) => x.id === id);
          if (n?.sim) {
            return `<div class="node-row"><div class="node-head"><b>หุ่น ${id}</b><span class="tag warn">จำลอง (โหมดทดสอบ)</span></div>
              <span class="muted small">ไม่มีบอร์ดจริง เมื่อเสียบหุ่นจริง hub จะใช้หุ่นจริงแทนเอง</span></div>`;
          }
          if (!n || !n.seen) {
            return `<div class="node-row"><div class="node-head"><b>หุ่น ${id}</b><span class="tag bad">ยังไม่เคยเชื่อมต่อ</span></div>
              <span class="muted small">เสียบไฟหุ่น แล้วดูว่าไฟโซน 1 กระพริบสีฟ้า (กำลังหา hub)</span></div>`;
          }
          const signal = n.rssi > -60 ? 'ok' : n.rssi > -75 ? 'warn' : 'bad';
          return `<div class="node-row">
            <div class="node-head">
              <b>หุ่น ${id}</b>
              <span class="tag ${n.online ? 'ok' : 'bad'}">${n.online ? 'ออนไลน์' : 'ขาดการเชื่อมต่อ'}</span>
              <span class="tag ${signal}">สัญญาณ ${n.rssi} dBm</span>
              ${n.calibrated ? '' : '<span class="tag warn">ยังไม่ calibrate</span>'}
              ${n.tuning ? '<span class="tag warn">โหมดจูน</span>' : ''}
            </div>
            <span class="muted small">เปิดมา ${fmtUptime(n.uptime_s)} · firmware ${esc(n.fw)} · ตรวจจับได้ ${n.hits} ครั้ง</span>
            <span class="muted small num">noise: ${n.noise.join(' / ')} · baseline: ${n.baseline.join(' / ')}</span>
            <div class="row">
              <button class="btn small" type="button" data-node-action="identify:${id}">กระพริบไฟ</button>
              <button class="btn small" type="button" data-node-action="calibrate:${id}">Calibrate</button>
              <button class="btn small" type="button" data-node-action="sound_test:${id}">ทดสอบเสียง</button>
              <button class="btn small ghost" type="button" data-node-action="reboot_node:${id}">รีสตาร์ท</button>
            </div>
          </div>`;
        })
        .join('');
      const live = $('#tune-live');
      const n = S.nodesDetail.find((x) => x.id === S.tuneNode);
      if (live && n) live.checked = n.tuning;
    },

    updateBars() {
      const box = $('#bars');
      if (!box) return;
      const p = S.peaks[S.tuneNode];
      const node = S.nodesDetail.find((x) => x.id === S.tuneNode);
      const thr = S.settings?.nodes?.[S.tuneNode - 1]?.threshold ?? [300, 300, 300, 300, 300];
      const noise = node?.noise ?? [0, 0, 0, 0, 0];
      const peaks = p?.peaks ?? [0, 0, 0, 0, 0];
      const scale = Math.max(600, ...peaks, ...thr.map((t) => t * 1.6));
      box.innerHTML = ZONES.map((z, i) => {
        const h = clamp((peaks[i] / scale) * 100, 0, 100);
        return `<div class="bar">
          <div class="col">
            <span class="noise" style="height:${(noise[i] / scale) * 100}%"></span>
            <i class="${p && p.zone === i ? 'win' : ''}" style="height:${h}%"></i>
            <span class="thr" style="bottom:${clamp((thr[i] / scale) * 100, 0, 100)}%"></span>
          </div>
          <b class="num">${peaks[i]}</b><span class="muted">${z}</span>
        </div>`;
      }).join('');
    },

    updateCrosstalk() {
      const status = $('#xt-status');
      const report = $('#xt-report');
      if (!status) return;
      const x = S.crosstalk;
      if (!x || x.node !== S.tuneNode) {
        status.textContent = '';
        report.innerHTML = '';
        return;
      }
      if (x.active) {
        status.innerHTML = `<b>ยิงโซน ${x.zone + 1} (${ZONES[x.zone]})</b> นัดที่ ${x.shot + 1} จาก ${x.shots_per_zone} · ไฟสีฟ้าบนหุ่นคือโซนที่ต้องยิง`;
        report.innerHTML = '';
      } else if (x.done && x.matrix) {
        status.innerHTML = '<b>ทดสอบเสร็จแล้ว</b>';
        report.innerHTML = `
          <div class="table-wrap"><table class="matrix">
            <thead><tr><th>ยิงโซน ↓ / ช่อง →</th>${ZONES.map((z) => `<th class="n">${z}</th>`).join('')}<th class="n">อัตราส่วน</th></tr></thead>
            <tbody>${x.matrix
              .map(
                (row, zi) => `<tr><th>${ZONES[zi]}</th>${row
                  .map((v, ci) => `<td class="${ci === zi ? 'diag' : ''}">${v}</td>`)
                  .join('')}<td class="${x.ratio_x10[zi] < 30 ? 'badratio' : ''}">${(x.ratio_x10[zi] / 10).toFixed(1)}×</td></tr>`,
              )
              .join('')}
              <tr><th>แนะนำ threshold</th>${x.suggested.map((v) => `<td><b>${v}</b></td>`).join('')}<td></td></tr>
            </tbody></table></div>
          <p class="muted small">อัตราส่วน = สัญญาณโซนที่ยิง ÷ สัญญาณสูงสุดของโซนข้าง ๆ ควร ≥ 3× ถ้าต่ำกว่านั้น (สีแดง) ให้ตรวจยางกันสั่นและขอบแผ่นที่อาจชนกัน</p>
          <button class="btn primary" type="button" id="xt-apply">ใช้ค่า threshold ที่แนะนำ</button>`;
      } else {
        status.textContent = 'ยกเลิกการทดสอบแล้ว';
        report.innerHTML = '';
      }
    },
  };
  Views.admin = Admin;

  // ------------------------------------------------------------------ boot

  function updateLangButton() {
    const btn = $('#lang-btn');
    if (!btn) return;
    // Shows the language you switch TO.
    btn.textContent = window.I18N.lang === 'th' ? 'EN' : 'TH';
    btn.setAttribute('aria-label', window.I18N.lang === 'th' ? 'Switch to English' : 'เปลี่ยนเป็นภาษาไทย');
  }

  function boot() {
    window.I18N.apply();
    updateLangButton();
    $('#lang-btn')?.addEventListener('click', () => {
      window.I18N.set(window.I18N.lang === 'th' ? 'en' : 'th');
      window.I18N.apply();
      updateLangButton();
      updateChrome();
      render();
    });
    window.addEventListener('hashchange', render);
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible' && gameActive()) keepAwake(true);
    });
    document.addEventListener('pointerdown', () => Sound.unlock(), { once: true });
    updateChrome();
    render();
    hub.connect(onMessage, onStatus);
  }

  if (DEMO) {
    const script = document.createElement('script');
    script.src = 'demo.js';
    script.onload = () => {
      hub = window.TargetRangeDemo.create({ toast });
      boot();
    };
    script.onerror = () => toast('โหลด demo.js ไม่ได้', 'bad');
    document.head.append(script);
  } else {
    hub = new HubSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
    boot();
  }
})();
