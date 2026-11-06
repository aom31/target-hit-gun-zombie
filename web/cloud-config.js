/*
 * Firebase project for the public live view (phase 1). These values are public:
 * access is controlled by the Realtime Database rules (firebase/database.rules.json).
 * Keep in sync with src/hub/cloud_config.h.
 *
 * The page runs in "cloud" (live view) mode on GitHub Pages, or anywhere with ?cloud.
 */
window.TR_CLOUD = {
  dbUrl: 'https://v1-hit-detection-gun-default-rtdb.asia-southeast1.firebasedatabase.app',
  publicUrl: 'https://aom31.github.io/target-hit-gun-zombie/web/',
  // The hub sends a heartbeat every 30 s; older than this means the range is offline.
  offlineAfterMs: 75000,
};
