/*
 * Target Range: UI language (Thai / English).
 *
 * The Thai text in app.js is the key: t('ข้อความ {n}', { n: 3 }) returns the
 * English entry when English is selected, otherwise the Thai text itself.
 * Anything missing from EN falls back to Thai, so an untranslated string is
 * never blank. The admin pages are staff-only and stay Thai.
 *
 * Messages from the hub arrive as Thai text; tHub() translates them by
 * replacing each number with {n} and looking that pattern up here, so the
 * firmware does not need to change.
 *
 * To add a language: add a dictionary like EN, list it in LANGS, and extend pick().
 */
(() => {
  'use strict';

  const EN = {
    // ---------------------------------------------------------------- shared words
    'หัว': 'Head',
    'อก': 'Chest',
    'ท้อง': 'Belly',
    'แขนซ้าย': 'Left arm',
    'แขนขวา': 'Right arm',
    'ง่าย': 'Easy',
    'ปานกลาง': 'Normal',
    'ยาก': 'Hard',
    '{s} วิ': '{s}s',
    'เล่น 2 คน': '2 players',
    'เล่นคนเดียว': 'Solo',
    'ดวล 2 คน': 'Versus',
    'กำลังโหลด…': 'Loading…',
    'กลับ': 'Back',
    'หน้าแรก': 'Home',
    'อันดับ': 'Ranking',
    'ประวัติ': 'History',
    'แต้ม': 'pts',
    'คะแนน': 'Score',
    'หุ่น {n}': 'Target {n}',
    'ผู้เล่น {n}': 'Player {n}',
    'เล่นเลย': 'Play now',

    // ---------------------------------------------------------------- index.html
    'เล่น': 'Play',
    'ท้าทาย': 'Challenge',
    'เมนูหลัก': 'Main menu',
    'กำลังเชื่อมต่อ…': 'Connecting…',
    'เตรียมพร้อม': 'Get ready',

    // ---------------------------------------------------------------- top bar and banners
    'โหมดทดลอง': 'Demo',
    'ออนไลน์': 'Online',
    'ขาดการเชื่อมต่อ': 'Offline',
    'ไม่ออนไลน์': 'not online',
    'จำลอง (โหมดทดสอบ)': 'simulated (test mode)',
    'หุ่น {n}: {s}': 'Target {n}: {s}',
    'กำลังเชื่อมต่อ hub… ตรวจว่ามือถือต่อ WiFi ของสนามอยู่': 'Connecting to the hub… check that your phone is on the range Wi-Fi',
    'กำลังจูนเซนเซอร์ ยังเริ่มเกมไม่ได้': 'Sensors are being tuned. Games can start when tuning ends.',
    'โหมดทดสอบ: หุ่น {ids} เป็นหุ่นจำลอง แตะโซนบนรูปหุ่นเพื่อยิง': 'Test mode: target {ids} is simulated. Tap a zone on the picture to shoot.',
    'ยังไม่ได้เชื่อมต่อ hub': 'Not connected to the hub',
    'PIN ไม่ถูกต้อง': 'Wrong PIN',

    // ---------------------------------------------------------------- live feed
    'ผิดจุด ({z})': 'Wrong zone ({z})',
    '{p} ผิดจุด ({z})': '{p} wrong zone ({z})',
    'หลุดเป้า ({z})': 'Missed ({z})',

    // ---------------------------------------------------------------- home
    'ไฟขึ้นตรงไหน ยิงตรงนั้น · ยิ่งไว ยิ่งแม่น ยิ่งขึ้นอันดับ': 'Shoot where the light is · Faster and sharper means a higher rank',
    'กลับไปที่เกม': 'Back to the game',
    'ยิงหุ่นทั้ง 2 ตัว เป้าขึ้นพร้อมกันได้ 2 จุด': 'Shoot both targets. Up to 2 zones light at once.',
    'คนละหุ่น เป้าลำดับเดียวกัน ใครแต้มสูงกว่าชนะ': 'One target each, same sequence. Higher score wins.',
    'ผู้ดูแลระบบ': 'Staff',
    'ยังไม่มีสถิติ': 'No records yet',
    'เล่นเกมแรกแล้วชื่อคุณจะขึ้นเป็นอันดับ 1': 'Play the first game and your name goes straight to #1',
    'ท้าชิงอันดับ 1': 'Beat #1',
    'ดูอันดับทั้งหมด': 'Full ranking',
    'กำลังจูนเซนเซอร์อยู่': 'Sensors are being tuned',
    'รอหุ่น {ids} เชื่อมต่อ…': 'Waiting for target {ids}…',
    'มีเกมกำลังเล่นอยู่': 'A game is already running',
    'หุ่นทั้ง 2 ตัวพร้อมแล้ว': 'Both targets are ready',

    // ---------------------------------------------------------------- setup
    'ผู้เล่น {n} · หุ่น {n}': 'Player {n} · Target {n}',
    'ชื่อผู้เล่น (ขึ้นในตารางอันดับ)': 'Your name (shown on the ranking)',
    'เวลาเกม': 'Game time',
    'ความยาก': 'Difficulty',
    'ไฟ {s} วิ': 'light {s}s',
    '🏆 สถิติโหมดนี้: <b>{score}</b> แต้ม โดย {name}': '🏆 Record for this mode: <b>{score}</b> pts by {name}',
    'ยังไม่มีใครเล่นโหมดนี้ เล่นก่อนได้อันดับ 1 ทันที': 'Nobody has played this mode yet. Play now and take #1.',
    'ทำให้ได้มากกว่า {score} แต้ม': 'Score more than {score} pts',
    'สถิติของ {name}': "{name}'s record",
    'ยกเลิกการท้าทาย': 'Cancel challenge',

    // ---------------------------------------------------------------- play
    'เกมหยุดชั่วคราว': 'Game paused',
    'เล่นต่อ': 'Resume',
    'กดค้างเพื่อหยุดเกม': 'Hold to stop the game',
    'เกมแรกของโหมดนี้': 'First game in this mode',
    'ทำลายสถิติ!': 'New record!',
    'เสมออันดับ 1': 'Tied for #1',
    'อีก {n} แต้ม แซง {name}': '{n} pts to pass {name}',
    'โดน {n}': 'Hit {n}',
    'หลุด {n}': 'Miss {n}',
    'ผิด {n}': 'Wrong {n}',
    'ทำลายสถิติแล้ว! 🔥': 'Record broken! 🔥',
    'ท้าชิง {name} · {score} แต้ม': 'Beat {name} · {score} pts',
    'อีก {n}': '{n} to go',
    'กดเล่นต่อเมื่อพร้อม': 'Press Resume when ready',
    'รอหุ่นกลับมาออนไลน์…': 'Waiting for the target to come back online…',

    // ---------------------------------------------------------------- result
    'เสมอ!': 'Draw!',
    'ชนะสถิติของ {name} แล้ว! (+{n})': "You beat {name}'s record! (+{n})",
    'ขาดอีก {n} แต้ม จะชนะ {name}': '{n} pts short of beating {name}',
    'ไม่นับอันดับ (หยุดก่อนหมดเวลา)': 'Not ranked (stopped early)',
    'สถิติใหม่ อันดับ 1!': 'New record, #1!',
    'ทำลายสถิติตัวเอง +{n}': 'Personal best +{n}',
    'สถิติตัวเอง {n}': 'Personal best {n}',
    'จาก {n}': 'of {n}',
    'ยิงโดน': 'Hits',
    'หลุดเป้า': 'Misses',
    'ผิดจุด': 'Wrong',
    'ความแม่น': 'Accuracy',
    'เวลาเฉลี่ย': 'Avg time',
    'คอมโบสูงสุด': 'Best combo',
    'เร็วที่สุด': 'Fastest',
    'เล่นซ้ำ': 'Play again',
    'ดูอันดับ': 'Ranking',
    'ประวัติทั้งหมด': 'All history',
    'สรุปผล': 'Results',
    'เกม #{id}': 'Game #{id}',
    'สแกนรับการ์ดผลการเล่น': 'Scan to get your result card',
    'ใช้มือถือที่ต่อ WiFi สนามสแกน QR นี้ จะได้รูปสรุปผลเกมนี้ไว้เก็บหรือแชร์': 'Scan this QR with a phone on the range Wi-Fi to get a picture of this game to save or share.',
    'เปิดการ์ดในเครื่องนี้': 'Open the card here',
    'ยังไม่มีผลการเล่น': 'No results yet',
    'ไม่พบเกมนี้': 'Game not found',

    // ---------------------------------------------------------------- ranking, challenge, history
    'ยังไม่มีใครเล่นโหมดนี้': 'Nobody has played this mode yet',
    'ไปเล่นเป็นคนแรก แล้วชื่อคุณจะอยู่บนสุด': 'Be the first and your name goes to the top',
    'ท้าชิงอันดับ 1 · {score} แต้ม': 'Beat #1 · {score} pts',
    'เลือกสถิติที่อยากล้ม แล้วระบบจะตั้งกติกาให้ตรงกัน ระหว่างเล่นจะมีแถบบอกว่าเหลืออีกกี่แต้ม': 'Pick a record to beat and the game uses the same rules. A bar shows how many points you still need.',
    'ยังไม่มีสถิติให้ท้าทาย': 'No records to challenge yet',
    'เล่นเกมแรกเพื่อสร้างสถิติ': 'Play the first game to set a record',
    '{n} ครั้ง': '{n} games',
    'ล้มอันดับ {n} ({score})': 'Beat #{n} ({score})',
    'เกมล่าสุด': 'Recent games',
    'ผู้เล่นทั้งหมด': 'All players',
    'ประวัติการเล่น': 'Game history',
    'ดาวน์โหลด CSV': 'Download CSV',
    'หยุดก่อนหมดเวลา': 'Stopped early',
    'ยังไม่มีเกมที่เล่นจบ': 'No finished games yet',
    'ผู้เล่น': 'Players',
    '{n} เกม': '{n} games',
    'แม่น {n}%': 'accuracy {n}%',
    'ชนะ {n}': 'wins {n}',
    'เรียงตามคะแนนสูงสุดของแต่ละคน (ทุกโหมดรวมกัน) แตะชื่อเพื่อดูเกมที่ดีที่สุด': "Sorted by each player's best score (all modes). Tap a name to see their best game.",
    'ยังไม่มีผู้เล่น': 'No players yet',

    // ---------------------------------------------------------------- result card image
    'อันดับ #{rank} จาก {total}': 'Rank #{rank} of {total}',
    '🔥 สถิติใหม่ อันดับ 1!': '🔥 New record, #1!',
    '🔥 สถิติใหม่!': '🔥 New record!',
    'เฉลี่ย': 'Avg',
    'เร็วสุด': 'Fastest',
    'โดน {h} · หลุด {m} · ผิด {w}': 'Hit {h} · Miss {m} · Wrong {w}',
    'แม่น {a}% · เฉลี่ย {s}': 'Accuracy {a}% · Avg {s}',
    'ยิงให้ไว · ยิงให้แม่น · ขึ้นอันดับ 1': 'Shoot fast · Shoot straight · Be #1',
    'การ์ดผลการเล่น': 'Result card',
    'สรุปผลเกม #{id}': 'Results for game #{id}',
    'กดค้างที่รูปเพื่อบันทึกลงเครื่อง หรือใช้ปุ่มด้านล่าง': 'Press and hold the picture to save it, or use the buttons below',
    'บันทึกรูป': 'Save image',
    'แชร์': 'Share',
    'ดูรายละเอียดเกม': 'Game details',
    'มือถือต้องต่อ WiFi ของสนามจึงจะเปิดการ์ดได้': 'Your phone must be on the range Wi-Fi to open the card',

    // ---------------------------------------------------------------- join poster
    'ป้าย QR เข้าร่วมเกม': 'How-to-join QR poster',
    'ป้ายนี้มีรหัส WiFi จึงต้องเข้าระบบผู้ดูแลก่อน': 'This poster shows the Wi-Fi password, so staff must sign in first',
    'ไปหน้าผู้ดูแล': 'Staff page',
    'กำลังโหลดค่า WiFi…': 'Loading Wi-Fi settings…',
    'พิมพ์ขนาด A4 แนวตั้ง ติดที่สนามหรือหน้าเคาน์เตอร์ ถ้าเปลี่ยนชื่อหรือรหัส WiFi ต้องพิมพ์ใหม่': 'Print on A4 portrait for the range or counter. Reprint after changing the Wi-Fi name or password.',
    'พิมพ์ / บันทึก PDF': 'Print / save PDF',
    'ป้ายวิธีเข้าร่วมเกม': 'How to join',
    'สแกน 2 ขั้น แล้วยิงได้เลย': 'Two scans and you are shooting',
    'สแกนต่อ WiFi': 'Scan to join Wi-Fi',
    'รหัส': 'Password',
    'สแกนเปิดเกม': 'Scan to open the game',
    'ส่วนใหญ่หน้าเกมจะเด้งขึ้นเองหลังต่อ WiFi': 'The game page usually opens by itself after joining the Wi-Fi',
    'เลือกโหมด ใส่ชื่อ กด START': 'Pick a mode, enter your name, press START',
    'เล่นคนเดียว หรือดวล 2 คน · ยิงเฉพาะจุดที่ไฟติด · ติดอันดับบนจอทันที': 'Solo or versus · Shoot only the lit zone · Your rank shows instantly',
    'ถ้าขึ้นว่า "ไม่มีอินเทอร์เน็ต" ให้กด <b>ใช้ต่อไป / เชื่อมต่อต่อไป</b>': 'If it says "No internet", tap <b>Stay connected / Use network as is</b>',
    'ถ้าหน้าเกมไม่ขึ้น ให้สแกนขั้นที่ 2 หรือพิมพ์ {url} ใน Safari': 'If the game does not open, scan step 2 or type {url} in Safari',
    'WiFi นี้ใช้เล่นเกมเท่านั้น ไม่มีอินเทอร์เน็ต · สวมแว่นป้องกันตาทุกครั้ง': 'This Wi-Fi is for the game only and has no internet · Always wear eye protection',

    // ---------------------------------------------------------------- live view over the internet
    'ดูสด': 'Live',
    'สนามออฟไลน์': 'Range offline',
    'สนามยังไม่ออนไลน์ หน้านี้จะอัปเดตเองเมื่อ hub ต่อเน็ต': 'The range is offline. This page updates by itself when the hub is back online.',
    'หน้านี้ดูสดอย่างเดียว เริ่มหรือหยุดเกมได้ที่สนาม': 'This page is view only. Start or stop games at the range.',
    'หน้านี้ดูสดอย่างเดียว เริ่มเกมได้ที่สนาม': 'View only. Start games at the range.',
    'สแกน QR นี้ด้วยมือถือเครื่องไหนก็ได้ จะได้รูปสรุปผลเกมนี้ไว้เก็บหรือแชร์': 'Scan this QR with any phone to get a picture of this game to save or share.',

    // ---------------------------------------------------------------- messages from the hub (numbers become {n})
    'หุ่น {n} ยังไม่ออนไลน์': 'Target {n} is not online yet',
    'หุ่น {n} ขาดการเชื่อมต่อ เกมหยุดชั่วคราว': 'Target {n} lost connection. Game paused.',
    'รอให้หุ่นทั้งสองออนไลน์ก่อน': 'Wait until both targets are online',
    'โหมดเกมไม่ถูกต้อง': 'Invalid game mode',
    'เวลาเกมต้องอยู่ระหว่าง {n}–{n} วินาที': 'Game time must be {n}–{n} seconds',
    'ระดับความยากไม่ถูกต้อง': 'Invalid difficulty',
    'ชื่อผู้เล่นไม่ถูกต้อง': 'Invalid player name',
    'หุ่นต้องเปิดใช้อย่างน้อย 1 โซน': 'Each target needs at least 1 zone enabled',
    'ยังไม่ได้เริ่มเกม': 'The game has not started',
    'ต้องใส่ PIN ผู้ดูแลก่อน': 'Staff PIN required',
    'บันทึกประวัติไม่สำเร็จ': 'Could not save the game history',
    'รูปแบบคำสั่งไม่ถูกต้อง': 'Invalid command',
    // ---------------------------------------------------------------- demo bar (web/demo.js)
    '<b>โหมดทดลอง</b> · แตะโซนบนหุ่นในหน้าเกมเพื่อยิง · PIN ผู้ดูแล 1234': '<b>Demo</b> · Tap a zone on the target to shoot · Staff PIN 1234',
    'บอทยิง: เปิด': 'Bot: on',
    'บอทยิง: ปิด': 'Bot: off',
    'ทำให้หุ่น 2 หลุด': 'Disconnect target 2',
    'ให้หุ่น 2 กลับมา': 'Reconnect target 2',

    'ใส่ PIN ผิดหลายครั้ง รอ {n} นาทีแล้วลองใหม่': 'Too many wrong PINs. Wait {n} minute and try again.',
  };

  const LANGS = { th: null, en: EN };
  const KEY = 'tr.lang';

  function pick() {
    try {
      const v = JSON.parse(localStorage.getItem(KEY));
      if (v in LANGS) return v;
    } catch {
      /* no storage: fall through to the phone's language */
    }
    const prefs = navigator.languages?.length ? navigator.languages : [navigator.language || 'th'];
    return prefs.some((l) => String(l).toLowerCase().startsWith('th')) ? 'th' : 'en';
  }

  let lang = pick();
  document.documentElement.lang = lang;

  const fill = (s, vars) => (vars ? s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? String(vars[k]) : m)) : s);

  function t(th, vars) {
    const dict = LANGS[lang];
    const s = dict && dict[th] != null ? dict[th] : th;
    return fill(s, vars);
  }

  // Hub messages: "หุ่น 2 ยังไม่ออนไลน์" -> pattern "หุ่น {n} ยังไม่ออนไลน์" -> "Target 2 is not online yet".
  function tHub(text) {
    const dict = LANGS[lang];
    if (!dict || !text) return text;
    if (dict[text] != null) return dict[text];
    const nums = [];
    const pattern = String(text).replace(/\d+/g, (d) => (nums.push(d), '{n}'));
    const out = dict[pattern];
    if (out == null) return text;
    let i = 0;
    return out.replace(/\{n\}/g, () => nums[i++] ?? '');
  }

  // Names the hub fills in when a player leaves the name empty ("ผู้เล่น 1").
  function playerName(name) {
    const m = /^ผู้เล่น (\d+)$/.exec(name ?? '');
    return m ? t('ผู้เล่น {n}', { n: m[1] }) : name;
  }

  // Static markup: <span data-i18n="ไทย"> and aria-label via data-i18n-aria.
  function apply(root = document) {
    root.querySelectorAll('[data-i18n]').forEach((el) => (el.textContent = t(el.dataset.i18n)));
    root.querySelectorAll('[data-i18n-aria]').forEach((el) => el.setAttribute('aria-label', t(el.dataset.i18nAria)));
  }

  function set(next) {
    if (!(next in LANGS)) return;
    lang = next;
    document.documentElement.lang = next;
    window.dispatchEvent(new Event('langchange'));
    try {
      localStorage.setItem(KEY, JSON.stringify(next));
    } catch {
      /* private mode: choice lasts for this page only */
    }
  }

  window.I18N = {
    t,
    tHub,
    playerName,
    apply,
    set,
    get lang() {
      return lang;
    },
    locale: () => (lang === 'en' ? 'en-GB' : 'th-TH'),
  };
})();
