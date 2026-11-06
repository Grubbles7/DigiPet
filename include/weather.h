#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include "config.h"

// ── Weather scene IDs ─────────────────────────────────────────────────────────
#define WX_SUNNY   0   // clear sky, daytime
#define WX_NIGHT   1   // clear sky, night
#define WX_CLOUDY  2   // cloudy / overcast
#define WX_RAIN    3   // rain / drizzle
#define WX_STORM   4   // thunderstorm
#define WX_SNOW    5   // snow
#define WX_UNKNOWN 2   // fallback → cloudy

// ── BLE Current Time Service UUIDs ───────────────────────────────────────────
// CTS is a standard GATT service broadcast by many phones/watches
#define BLE_CTS_SERVICE_UUID    "00001805-0000-1000-8000-00805f9b34fb"
#define BLE_CTS_CHAR_UUID       "00002a2b-0000-1000-8000-00805f9b34fb"
#define BLE_SCAN_DURATION_S     8     // seconds to scan for BLE devices
#define BLE_CONNECT_TIMEOUT_MS  5000  // ms to wait for BLE connection

// ── Persistent weather state ──────────────────────────────────────────────────
uint8_t currentWeather = WX_SUNNY;

// ── NVS cache ─────────────────────────────────────────────────────────────────
inline void saveWeatherCache(uint8_t scene) {
  Preferences prefs;
  prefs.begin("digiedc", false);
  prefs.putUChar("wxscene", scene);
  prefs.end();
}

inline uint8_t loadWeatherCache() {
  Preferences prefs;
  prefs.begin("digiedc", true);
  uint8_t scene = prefs.getUChar("wxscene", WX_SUNNY);
  prefs.end();
  return scene;
}

// ── OWM condition code → scene ────────────────────────────────────────────────
inline uint8_t owmCodeToScene(int id, bool isDay) {
  if (id == 800)             return isDay ? WX_SUNNY : WX_NIGHT;
  if (id >= 801 && id <= 804) return WX_CLOUDY;
  if (id >= 200 && id <= 232) return WX_STORM;
  if (id >= 300 && id <= 321) return WX_RAIN;
  if (id >= 500 && id <= 531) return WX_RAIN;
  if (id >= 600 && id <= 622) return WX_SNOW;
  return WX_UNKNOWN;
}

// ── Open WiFi scan + connect ──────────────────────────────────────────────────
inline bool connectToOpenWifi() {
  Serial.println("WX: Scanning for open networks...");
  int found = WiFi.scanNetworks(false, true);
  if (found <= 0) { WiFi.scanDelete(); Serial.println("WX: No networks found."); return false; }

  struct OpenNet { String ssid; int rssi; };
  OpenNet open[WIFI_MAX_ATTEMPTS];
  int count = 0;

  for (int i = 0; i < found && count < WIFI_MAX_ATTEMPTS; i++) {
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN && WiFi.SSID(i).length() > 0) {
      int j = count;
      while (j > 0 && open[j-1].rssi < WiFi.RSSI(i)) { open[j] = open[j-1]; j--; }
      open[j] = { WiFi.SSID(i), WiFi.RSSI(i) };
      count++;
    }
  }
  WiFi.scanDelete();

  if (count == 0) { Serial.println("WX: No open networks."); return false; }

  WiFi.mode(WIFI_STA);
  for (int i = 0; i < count; i++) {
    Serial.printf("WX: Trying \"%s\"...\n", open[i].ssid.c_str());
    WiFi.begin(open[i].ssid.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < (unsigned long)WIFI_TIMEOUT_PER_NET * 1000) { delay(300); }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WX: Connected to \"%s\"\n", open[i].ssid.c_str());
      return true;
    }
    WiFi.disconnect(true);
    delay(200);
  }
  Serial.println("WX: All open networks failed.");
  return false;
}

// ── NTP sync ──────────────────────────────────────────────────────────────────
inline bool syncTime() {
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  struct tm t;
  int attempts = 0;
  while (!getLocalTime(&t) && attempts < 10) { delay(500); attempts++; }
  if (getLocalTime(&t)) { Serial.println("WX: NTP synced."); return true; }
  Serial.println("WX: NTP failed.");
  return false;
}

// ── OWM fetch ─────────────────────────────────────────────────────────────────
inline uint8_t fetchWeatherScene() {
  String url = "http://api.openweathermap.org/data/2.5/weather?lat=";
  url += OWM_LAT; url += "&lon="; url += OWM_LON; url += "&appid="; url += OWM_API_KEY;
  HTTPClient http;
  http.begin(url); http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { Serial.printf("WX: HTTP error %d\n", code); http.end(); return WX_UNKNOWN; }
  String payload = http.getString(); http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) { Serial.printf("WX: JSON error: %s\n", err.c_str()); return WX_UNKNOWN; }
  int  wxId    = doc["weather"][0]["id"] | 800;
  long dt      = doc["dt"]              | 0;
  long sunrise = doc["sys"]["sunrise"]  | 0;
  long sunset  = doc["sys"]["sunset"]   | 0;
  bool isDay   = (sunrise > 0 && sunset > 0) ? (dt >= sunrise && dt < sunset) : true;
  uint8_t scene = owmCodeToScene(wxId, isDay);
  Serial.printf("WX: id=%d isDay=%d scene=%d\n", wxId, isDay, scene);
  return scene;
}

// ── BLE Current Time Service fallback ─────────────────────────────────────────
// Scans for nearby BLE devices advertising the Current Time Service (0x1805).
// If found, connects, reads the 10-byte CTS characteristic, and sets the
// ESP32 system clock without requiring any pairing or phone app.
//
// CTS characteristic 0x2A2B byte layout:
//   [0-1] year (uint16 LE)  [2] month  [3] day
//   [4] hours  [5] minutes  [6] seconds
//   [7] day-of-week  [8] fractions256  [9] adjust reason

static String _bleFoundAddr = "";  // address of the CTS device we found

// BLE advertised device callback — fires during scan
class _CTSAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice dev) override {
    if (_bleFoundAddr.length() > 0) return;  // already found one
    if (dev.haveServiceUUID() && dev.isAdvertisingService(BLEUUID(BLE_CTS_SERVICE_UUID))) {
      Serial.printf("BT: Found CTS device: %s (%s)\n",
                    dev.getName().c_str(), dev.getAddress().toString().c_str());
      _bleFoundAddr = dev.getAddress().toString().c_str();
      dev.getScan()->stop();
    }
  }
};

inline bool syncTimeViaBLE() {
  Serial.println("BT: Scanning for Current Time Service...");
  _bleFoundAddr = "";

  BLEDevice::init("");
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new _CTSAdvertisedDeviceCallbacks(), true);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->start(BLE_SCAN_DURATION_S, false);

  if (_bleFoundAddr.length() == 0) {
    Serial.println("BT: No CTS device found.");
    BLEDevice::deinit(false);
    return false;
  }

  // Connect and read the characteristic
  BLEClient *pClient = BLEDevice::createClient();
  BLEAddress addr(_bleFoundAddr.c_str());

  Serial.printf("BT: Connecting to %s...\n", _bleFoundAddr.c_str());
  unsigned long connStart = millis();
  bool connected = pClient->connect(addr);
  if (!connected || millis() - connStart > BLE_CONNECT_TIMEOUT_MS) {
    Serial.println("BT: Connect failed.");
    pClient->disconnect();
    BLEDevice::deleteClient(pClient);
    BLEDevice::deinit(false);
    return false;
  }

  BLERemoteService *pCTS = pClient->getService(BLEUUID(BLE_CTS_SERVICE_UUID));
  if (!pCTS) {
    Serial.println("BT: CTS service not found on device.");
    pClient->disconnect();
    BLEDevice::deleteClient(pClient);
    BLEDevice::deinit(false);
    return false;
  }

  BLERemoteCharacteristic *pChar = pCTS->getCharacteristic(BLEUUID(BLE_CTS_CHAR_UUID));
  if (!pChar || !pChar->canRead()) {
    Serial.println("BT: CTS characteristic not readable.");
    pClient->disconnect();
    BLEDevice::deleteClient(pClient);
    BLEDevice::deinit(false);
    return false;
  }

  std::string val = pChar->readValue();
  pClient->disconnect();
  BLEDevice::deleteClient(pClient);
  BLEDevice::deinit(false);

  if (val.size() < 7) {
    Serial.printf("BT: CTS data too short (%d bytes).\n", val.size());
    return false;
  }

  // Parse CTS bytes into struct tm
  const uint8_t *b = (const uint8_t *)val.data();
  uint16_t year  = b[0] | (b[1] << 8);
  uint8_t  month = b[2];   // 1-12
  uint8_t  day   = b[3];   // 1-31
  uint8_t  hour  = b[4];
  uint8_t  min   = b[5];
  uint8_t  sec   = b[6];

  Serial.printf("BT: CTS time = %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, min, sec);

  // Sanity check before applying
  if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
    Serial.println("BT: CTS data out of range, ignoring.");
    return false;
  }

  // Convert to time_t and set system clock
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon  = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min  = min;
  t.tm_sec  = sec;
  t.tm_isdst = 0;

  time_t epoch = mktime(&t);
  // CTS gives local time — subtract UTC_OFFSET to get UTC, then re-apply
  // (mktime treats the tm as local but ESP32 epoch is UTC-based)
  // We store as-is since configTime has already set the timezone offset.
  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  settimeofday(&tv, nullptr);

  Serial.println("BT: System clock set via BLE CTS.");
  return true;
}

// ── Full update: WiFi primary → BLE time fallback ─────────────────────────────
// Call on boot and hourly.
// WiFi success  → NTP time + OWM weather (best case)
// BLE success   → BLE time only, weather stays cached (partial)
// Both fail     → cached time + cached weather (worst case)
inline bool doWeatherUpdate() {
  // ── Try WiFi first ────────────────────────────────────────────────────────
  if (connectToOpenWifi()) {
    bool gotTime  = syncTime();
    uint8_t scene = fetchWeatherScene();
    if (scene != WX_UNKNOWN) {
      currentWeather = scene;
      saveWeatherCache(scene);
      Serial.printf("WX: Weather updated → scene %d\n", scene);
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    if (gotTime || scene != WX_UNKNOWN) return true;
  }

  // ── WiFi failed — try BLE time sync ──────────────────────────────────────
  Serial.println("WX: WiFi unavailable — trying BLE time sync...");
  bool gotBLETime = syncTimeViaBLE();
  if (gotBLETime) {
    Serial.println("WX: Time synced via BLE. Weather stays cached.");
  } else {
    Serial.println("WX: BLE also failed. Using cached time + weather.");
  }

  // Weather stays as whatever was last cached — already loaded at boot
  return gotBLETime;
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Draw weather background (vivid style) ────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

#define WX_PZ_Y  76
#define WX_PZ_H  68
#define WX_W     135

inline void drawWeatherBackground(TFT_eSPI &tft, uint8_t scene) {

  // ── Sunny ──────────────────────────────────────────────────────────────────
  if (scene == WX_SUNNY) {
    for (int y = 0; y < WX_PZ_H; y++) {
      tft.drawFastHLine(0, WX_PZ_Y + y, WX_W, tft.color565(
        map(y, 0, WX_PZ_H, 10, 30),
        map(y, 0, WX_PZ_H, 40, 80),
        map(y, 0, WX_PZ_H, 80, 140)));
    }
    tft.fillCircle(105, WX_PZ_Y + 18, 12, tft.color565(255, 200, 0));
    tft.fillCircle(105, WX_PZ_Y + 18,  9, tft.color565(255, 230, 50));
    for (int a = 0; a < 8; a++) {
      float ang = a * 3.14159f / 4.0f;
      tft.drawLine(105 + (int)(cos(ang)*14), (WX_PZ_Y+18) + (int)(sin(ang)*14),
                   105 + (int)(cos(ang)*20), (WX_PZ_Y+18) + (int)(sin(ang)*20),
                   tft.color565(255, 220, 0));
    }
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 8, WX_W, 8, tft.color565(20, 60, 20));
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 4, WX_W, 4, tft.color565(30, 80, 30));
  }

  // ── Night ──────────────────────────────────────────────────────────────────
  else if (scene == WX_NIGHT) {
    tft.fillRect(0, WX_PZ_Y, WX_W, WX_PZ_H, tft.color565(5, 8, 25));
    const uint8_t stars[][2] = {
      {10,6},{25,4},{45,10},{70,3},{90,7},{115,13},
      {18,20},{60,18},{100,16},{35,26},{108,28},{50,35},{80,40},{15,50},{95,48}
    };
    for (auto &s : stars) tft.fillRect(s[0], WX_PZ_Y + s[1], 1, 1, TFT_WHITE);
    const uint8_t bright[][2] = {{30,12},{75,8},{110,22},{55,45}};
    for (auto &s : bright) tft.fillRect(s[0], WX_PZ_Y + s[1], 2, 2, tft.color565(200,200,255));
    tft.fillCircle(105, WX_PZ_Y + 18, 10, tft.color565(210, 205, 160));
    tft.fillCircle(110, WX_PZ_Y + 14,  8, tft.color565(5, 8, 25));
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 6, WX_W, 6, tft.color565(10, 12, 30));
  }

  // ── Cloudy ─────────────────────────────────────────────────────────────────
  else if (scene == WX_CLOUDY) {
    for (int y = 0; y < WX_PZ_H; y++) {
      uint8_t v = map(y, 0, WX_PZ_H, 30, 55);
      tft.drawFastHLine(0, WX_PZ_Y + y, WX_W, tft.color565(v, v+5, v+10));
    }
    struct { int x, y, r; uint8_t br; } clouds[] = {
      {20,12,12,100},{38,8,16,110},{60,14,12,95},
      {80,9,18,115},{105,13,12,100},{50,28,14,85},{90,25,16,90},{15,30,10,80}
    };
    for (auto &c : clouds)
      tft.fillCircle(c.x, WX_PZ_Y + c.y, c.r, tft.color565(c.br, c.br+5, c.br+8));
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 6, WX_W, 6, tft.color565(25, 30, 25));
  }

  // ── Rain ───────────────────────────────────────────────────────────────────
  else if (scene == WX_RAIN) {
    tft.fillRect(0, WX_PZ_Y, WX_W, WX_PZ_H, tft.color565(15, 20, 35));
    tft.fillRect(0, WX_PZ_Y, WX_W, 20, tft.color565(40, 45, 55));
    for (int x = 0; x < WX_W; x += 12)
      tft.fillCircle(x + 6, WX_PZ_Y + 10, 10, tft.color565(50, 55, 65));
    const uint8_t drops[][2] = {
      {8,24},{16,32},{24,22},{32,38},{40,28},{48,20},{56,36},{64,24},
      {72,32},{80,26},{88,38},{96,23},{104,34},{112,28},{120,20},
      {12,50},{28,52},{44,58},{60,48},{76,56},{92,51},{108,54},{124,50}
    };
    for (auto &d : drops)
      tft.drawLine(d[0]-1, WX_PZ_Y+d[1], d[0]+1, WX_PZ_Y+d[1]+5, tft.color565(80,140,200));
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 5, WX_W, 5, tft.color565(20, 35, 55));
  }

  // ── Storm ──────────────────────────────────────────────────────────────────
  else if (scene == WX_STORM) {
    tft.fillRect(0, WX_PZ_Y, WX_W, WX_PZ_H, tft.color565(8, 8, 18));
    tft.fillRect(0, WX_PZ_Y, WX_W, 26, tft.color565(25, 28, 38));
    for (int x = 0; x < WX_W; x += 10)
      tft.fillCircle(x + 5, WX_PZ_Y + 12, 12, tft.color565(30, 32, 42));
    tft.fillTriangle(62, WX_PZ_Y+6,  58, WX_PZ_Y+26, 68, WX_PZ_Y+24, tft.color565(255,255,100));
    tft.fillTriangle(68, WX_PZ_Y+24, 58, WX_PZ_Y+26, 54, WX_PZ_Y+44, tft.color565(255,255,100));
    tft.drawLine(65, WX_PZ_Y+6, 56, WX_PZ_Y+44, tft.color565(80, 80, 20));
    const uint8_t sdrops[][2] = {
      {10,28},{24,34},{38,26},{90,30},{104,38},{118,26},{20,50},{50,56},{80,52},{110,54}
    };
    for (auto &d : sdrops)
      tft.drawLine(d[0]-1, WX_PZ_Y+d[1], d[0]+1, WX_PZ_Y+d[1]+5, tft.color565(50,80,120));
  }

  // ── Snow ───────────────────────────────────────────────────────────────────
  else if (scene == WX_SNOW) {
    for (int y = 0; y < WX_PZ_H; y++) {
      uint8_t v = map(y, 0, WX_PZ_H, 15, 30);
      tft.drawFastHLine(0, WX_PZ_Y + y, WX_W, tft.color565(v, v+5, v+15));
    }
    const uint8_t flakes[][3] = {
      {10,8,1},{28,18,2},{45,10,1},{62,26,2},{78,8,1},{95,16,2},{112,24,1},
      {18,38,2},{35,46,1},{52,54,2},{70,40,1},{88,50,2},{105,42,1},{122,52,2},
      {5,28,1},{40,32,2},{75,30,1},{110,34,2},{20,58,1},{60,62,2},{100,58,1}
    };
    for (auto &f : flakes) tft.fillRect(f[0], WX_PZ_Y+f[1], f[2], f[2], tft.color565(200,220,240));
    tft.fillRect(0, WX_PZ_Y + WX_PZ_H - 8, WX_W, 8, tft.color565(180, 200, 220));
    for (int x = 0; x < WX_W; x += 18)
      tft.fillCircle(x + 9, WX_PZ_Y + WX_PZ_H - 8, 8, tft.color565(190, 210, 230));
  }
}
