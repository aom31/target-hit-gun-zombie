/*
 * Firebase project for the public live view (phase 1). These values are public:
 * access is controlled by the Realtime Database rules (firebase/database.rules.json).
 * Keep in sync with src/hub/cloud_config.h.
 *
 * The page runs in "cloud" (live view) mode on the publicUrl domain, on any github.io
 * address, or anywhere with ?cloud.
 *
 * Custom domain: set publicUrl here AND kPublicUrl in src/hub/cloud_config.h (same value,
 * with https:// and the trailing /web/), then reflash the hub (internal/NOTES.md section 10).
 */
window.TR_CLOUD = {
  dbUrl: 'https://v1-hit-detection-gun-default-rtdb.asia-southeast1.firebasedatabase.app',
  publicUrl: 'https://aom31.github.io/target-hit-gun-zombie/web/',
  // The hub sends a heartbeat every 30 s; older than this means the range is offline.
  offlineAfterMs: 75000,
};
