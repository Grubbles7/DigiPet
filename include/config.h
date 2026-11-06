#pragma once

// ── WiFi — open network scanning, no credentials needed ───────────────────────
// The device scans for open (no password) networks and tries each in turn.
#define WIFI_TIMEOUT_PER_NET  8     // seconds to wait per network attempt
#define WIFI_MAX_ATTEMPTS     3     // max open networks to try per scan

// ── NTP / Time ────────────────────────────────────────────────────────────────
#define NTP_SERVER  "pool.ntp.org"
#define UTC_OFFSET  28800           // AWST (Perth) = UTC+8 = 8*3600
#define DST_OFFSET  0               // Perth does not observe DST

// ── OpenWeatherMap ────────────────────────────────────────────────────────────
#define OWM_API_KEY  "bd5e378503939ddaee76f12ad7a97608"
#define OWM_LAT      "-31.95"       // Perth, WA
#define OWM_LON      "115.86"
