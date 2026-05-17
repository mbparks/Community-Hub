/*
 * ╔═══════════════════════════════════════╗
 * ║           PROJECT LANTERN             ║
 * ║        Local Bulletin Board           ║
 * ╚═══════════════════════════════════════╝
 *
 * Fork of https://github.com/SonicDH/Community-Hub 
 *
 * Hardware : ESP32-C3  (also builds for ESP32 WROOM, watch LED_PIN choice)
 * Storage  : Internal Flash via LittleFS
 *
 * Libraries (Arduino Library Manager):
 *   - ArduinoJson   by Benoit Blanchon
 *
 * Board setting: ESP32-C3 Dev Module (or ESP32 Dev Module)
 * Partition scheme: Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)
 * Uses the built-in WebServer (no extra libs needed).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "FS.h"
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include "Community_Hub_pages.h"


// ===================== CONFIG ===================== //
namespace Config {
  //========= Defaults — overridable at runtime via admin panel ========//

  const char* LOCALITY_NAME = "Community Hub";
  const char* BOARD_ICON    = "🌱";
  const char* BOARD_TAGLINE = "Take what you need • Share what you can";
  const char* BOARD_RULES   = "Be local • Be kind • No spam";
  const char* BOARD_FOOTER  = "Powered locally — no internet required";

  const char* ADMIN_KEY = "change_me"; // Please definintely do, either here or in the admin panel.

  const int LED_PIN = 4; 
 
  const int LED_DAY_BRIGHTNESS   = 80;
  const int LED_NIGHT_BRIGHTNESS = 20;
  const int NIGHT_START_HOUR     = 20;
  const int DAY_START_HOUR       = 7;


  //======= Settings that are NOT in the Admin Panel ==========//

  // Access Point settings.
  // SSID is what neighbours see in their WiFi list.
  // AP_PASS must be empty for an open network, or 8+ characters for WPA2.
  // Anything 1-7 chars will cause softAP() to fail silently.
  const char* AP_SSID     = "Fountain Head Hub";
  const char* AP_PASS     = "fountain";        // "" = open network
  const int   AP_CHANNEL  = 6;
  const int   AP_MAX_CONN = 20;

  // Default message expiration time
  const int DEFAULT_EXPIRY_HOURS = 72;


  // !!!! DO NOT CHANGE THESE !!!!
  // The MAX_MSGS amount is not arbitrary. The heap for the array needs to be sized accordingly.
  // And why would you even need to change it? 200 messages is an absurd amount anyhow. 
  const int MAX_MSGS             = 200;
  const char* STORAGE_FILE  = "/msgs.json";
  const char* TIME_FILE     = "/time.json";
  const char* LEDCFG_FILE   = "/led.json";

}

// ===================== RUNTIME IDENTITY =====================
// These shadow the Config defaults and can be changed via the admin panel.
// Persisted to /identity.json.

String id_name     = Config::LOCALITY_NAME;
String id_icon     = Config::BOARD_ICON;
String id_tagline  = Config::BOARD_TAGLINE;
String id_rules    = Config::BOARD_RULES;
String id_footer   = Config::BOARD_FOOTER;
String id_hostname = "";   // empty = derive from id_name; non-empty = explicit override

// Slug a string for use as an mDNS hostname. Rules:
//   - Lowercase ASCII letters, digits, and hyphens only
//   - Non-ASCII characters and punctuation are dropped
//   - Whitespace becomes a hyphen
//   - Runs of hyphens are collapsed, leading/trailing hyphens stripped
//   - Result is capped at 30 chars (mDNS spec allows 63, but shorter is friendlier)
//   - Empty result falls back to "hub"
String slugify(const String& in) {
  String out;
  out.reserve(in.length());
  bool lastWasHyphen = true;  // start true to strip leading hyphens
  for (unsigned int i = 0; i < in.length() && (int)out.length() < 30; i++) {
    char c = in.charAt(i);
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
      lastWasHyphen = false;
    } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
      if (!lastWasHyphen) { out += '-'; lastWasHyphen = true; }
    }
    // anything else (punctuation, emoji, non-ASCII) is silently dropped
  }
  // Strip trailing hyphen
  while (out.length() && out.charAt(out.length() - 1) == '-') {
    out.remove(out.length() - 1);
  }
  if (out.length() == 0) out = "hub";
  return out;
}

// Compute the effective mDNS hostname. If id_hostname is explicitly set, use
// its slug. Otherwise, slug the first whitespace-separated word of id_name.
// This keeps the default short: "Sunbury Hub" -> "sunbury", not "sunbury-hub".
String effectiveHostname() {
  if (id_hostname.length() > 0) return slugify(id_hostname);
  int sp = id_name.indexOf(' ');
  String first = (sp > 0) ? id_name.substring(0, sp) : id_name;
  return slugify(first);
}

// Track what we last advertised, so we know whether to restart mDNS on
// identity changes.
String currentMdnsName = "";

void startMdns() {
  String h = effectiveHostname();
  if (h == currentMdnsName) return;  // no change
  if (currentMdnsName.length() > 0) {
    MDNS.end();
  }
  if (MDNS.begin(h.c_str())) {
    MDNS.addService("http", "tcp", 80);
    currentMdnsName = h;
    Serial.printf("✓ mDNS started: http://%s.local\n", h.c_str());
  } else {
    Serial.println("⚠  MDNS.begin() failed.");
  }
}

void saveIdentityConfig() {
  DynamicJsonDocument doc(1024);
  doc["name"]     = id_name;
  doc["icon"]     = id_icon;
  doc["tagline"]  = id_tagline;
  doc["rules"]    = id_rules;
  doc["footer"]   = id_footer;
  doc["hostname"] = id_hostname;

  File tmp = LittleFS.open("/id.tmp", FILE_WRITE);
  if (!tmp) return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove("/identity.json");
  LittleFS.rename("/id.tmp", "/identity.json");
}

void loadIdentityConfig() {
  if (!LittleFS.exists("/identity.json")) return;
  File f = LittleFS.open("/identity.json");
  if (!f) return;
  DynamicJsonDocument doc(1024);
  if (!deserializeJson(doc, f)) {
    if (doc["name"].as<String>().length())    id_name     = doc["name"].as<String>();
    if (doc["icon"].as<String>().length())    id_icon     = doc["icon"].as<String>();
    if (doc["tagline"].as<String>().length()) id_tagline  = doc["tagline"].as<String>();
    if (doc["rules"].as<String>().length())   id_rules    = doc["rules"].as<String>();
    if (doc["footer"].as<String>().length())  id_footer   = doc["footer"].as<String>();
    // hostname may be absent in old files; empty string means "derive from name"
    id_hostname = doc["hostname"].as<String>();
  }
  f.close();
}

// ===================== RUNTIME ADMIN KEY =====================
// Shadows Config::ADMIN_KEY. Persisted to /adminkey.json.
// Config::ADMIN_KEY is the run-time fallback if the file is absent.

String adminKey    = Config::ADMIN_KEY;
String sessionToken    = "";  // set on successful auth, cleared on reboot
unsigned long tokenIssuedAt = 0; // millis() when token was generated
#define TOKEN_LIFETIME_MS  1800000UL  // 30 minutes

String generateToken() {
  String token = "";
  for (int i = 0; i < 4; i++) {
    uint32_t r = esp_random();
    char chunk[9];
    snprintf(chunk, sizeof(chunk), "%08x", r);
    token += chunk;
  }
  tokenIssuedAt = millis();
  return token;
}

// Shorter 16-char hex token used for per-post ownership and per-claim ownership.
// Not tied to session lifetime, lives as long as the message does.
String generateShortToken() {
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  char buf[17];
  snprintf(buf, sizeof(buf), "%08x%08x", a, b);
  return String(buf);
}

void saveAdminKey() {
  DynamicJsonDocument doc(128);
  doc["key"] = adminKey;
  File tmp = LittleFS.open("/adminkey.tmp", FILE_WRITE);
  if (!tmp) return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove("/adminkey.json");
  LittleFS.rename("/adminkey.tmp", "/adminkey.json");
}

void loadAdminKey() {
  if (!LittleFS.exists("/adminkey.json")) return;
  File f = LittleFS.open("/adminkey.json");
  if (!f) return;
  DynamicJsonDocument doc(128);
  if (!deserializeJson(doc, f)) {
    String k = doc["key"] | "";
    if (k.length()) adminKey = k;
  }
  f.close();
}

// ===================== RUNTIME LED SETTINGS =====================
int  led_day_brightness   = Config::LED_DAY_BRIGHTNESS;
int  led_night_brightness = Config::LED_NIGHT_BRIGHTNESS;
int  led_night_start      = Config::NIGHT_START_HOUR;
int  led_day_start        = Config::DAY_START_HOUR;
int  led_pin              = Config::LED_PIN;
bool led_enabled          = true;
bool led_pulse_enabled    = true;
bool led_activity_enabled = true;

void saveLedConfig() {
  DynamicJsonDocument doc(512);
  doc["day_br"]   = led_day_brightness;
  doc["night_br"] = led_night_brightness;
  doc["night_st"] = led_night_start;
  doc["day_st"]   = led_day_start;
  doc["pin"]      = led_pin;
  doc["enabled"]  = led_enabled;
  doc["pulse"]    = led_pulse_enabled;
  doc["activity"] = led_activity_enabled;

  File tmp = LittleFS.open("/led.tmp", FILE_WRITE);
  if (!tmp) return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove(Config::LEDCFG_FILE);
  LittleFS.rename("/led.tmp", Config::LEDCFG_FILE);
}
void loadLedConfig() {
  if (!LittleFS.exists(Config::LEDCFG_FILE)) return;
  File f = LittleFS.open(Config::LEDCFG_FILE);
  if (!f) return;
  DynamicJsonDocument doc(512);
  if (!deserializeJson(doc, f)) {
    led_day_brightness   = doc["day_br"]   | Config::LED_DAY_BRIGHTNESS;
    led_night_brightness = doc["night_br"] | Config::LED_NIGHT_BRIGHTNESS;
    led_night_start      = doc["night_st"] | Config::NIGHT_START_HOUR;
    led_day_start        = doc["day_st"]   | Config::DAY_START_HOUR;
    led_pin              = doc["pin"]      | Config::LED_PIN;
    led_enabled          = doc["enabled"]  | true;
    led_pulse_enabled    = doc["pulse"]    | true;
    led_activity_enabled = doc["activity"] | true;
  }
  f.close();
}

// ===================== TIME =====================
unsigned long baseEpoch  = 0;
unsigned long baseMillis = 0;
unsigned long lastTimeSave = 0;

unsigned long nowSecs() {
  return baseEpoch + (millis() - baseMillis) / 1000;
}

bool setTimeFromString(String t) {
  if (t.length() != 13) return false;
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_mday = t.substring(0,  2).toInt();
  tm.tm_mon  = t.substring(2,  4).toInt() - 1;
  tm.tm_year = t.substring(4,  8).toInt() - 1900;
  tm.tm_hour = t.substring(9, 11).toInt();
  tm.tm_min  = t.substring(11, 13).toInt();
  time_t epoch = mktime(&tm);
  if (epoch <= 0) return false;
  baseEpoch  = epoch;
  baseMillis = millis();
  return true;
}

void saveTime() {
  DynamicJsonDocument doc(256);
  doc["epoch"] = nowSecs();
  File tmp = LittleFS.open("/time.tmp", FILE_WRITE);
  if (!tmp) return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove(Config::TIME_FILE);
  LittleFS.rename("/time.tmp", Config::TIME_FILE);
}

void loadTime() {
  if (!LittleFS.exists(Config::TIME_FILE)) return;
  File f = LittleFS.open(Config::TIME_FILE);
  if (!f) return;
  DynamicJsonDocument doc(256);
  if (!deserializeJson(doc, f)) {
    baseEpoch  = doc["epoch"];
    baseMillis = millis();
  }
  f.close();
}

int currentHour() {
  time_t t = nowSecs();
  struct tm* tm = localtime(&t);
  return tm ? tm->tm_hour : 12;
}

// ===================== UPTIME =====================
unsigned long bootMillis = 0;

String formatUptime() {
  unsigned long secs  = (millis() - bootMillis) / 1000;
  unsigned long days  = secs / 86400; secs %= 86400;
  unsigned long hours = secs / 3600;  secs %= 3600;
  unsigned long mins  = secs / 60;
  char buf[32];
  if (days > 0)
    snprintf(buf, sizeof(buf), "↑ %lud %luh %lum", days, hours, mins);
  else if (hours > 0)
    snprintf(buf, sizeof(buf), "↑ %luh %lum", hours, mins);
  else
    snprintf(buf, sizeof(buf), "↑ %lum", mins);
  return String(buf);
}

// ===================== MESSAGES =====================
struct Message {
  uint16_t      id;
  String        author;
  String        type;            // Notice | Offer | Need | Event | Poll
  String        text;            // body for messages, question for polls
  unsigned long expires;

  // Ownership: minted on post, returned once to the creator's browser.
  // Required for edit/delete. Never sent in /messages.
  String        ownerToken;

  // Claim state (Offer/Need only):
  bool          claimed;
  String        claimedBy;
  String        claimToken;      // minted on claim, returned once to the claimer.

  // Poll-only fields (pollOptCount == 0 for non-polls):
  uint8_t       pollOptCount;
  String        pollOpts[4];
  uint16_t      pollVotes[4];

  // Reactions: counters for [thanks, me_too, nice, noted]
  uint16_t      reactions[4];

  // Author name color (index into the palette in the page JS, 0 = default ink)
  uint8_t       authorColor;
};

Message msgs[Config::MAX_MSGS];
int msgCount = 0;
uint16_t nextMsgId = 1;
unsigned long lastPostTime = 0;

bool msgsDirty = false;
unsigned long lastMsgDirtyTime = 0;

// Stream save: write each message as a small JSON doc directly to the file,
// rather than building one giant 100KB+ doc in RAM. Friendly to ESP32 heap.
void saveMessages() {
  File tmp = LittleFS.open("/msgs.tmp", FILE_WRITE);
  if (!tmp) return;
  tmp.print("[");
  for (int i = 0; i < msgCount; i++) {
    if (i > 0) tmp.print(",");
    DynamicJsonDocument o(2048);
    o["id"]         = msgs[i].id;
    o["author"]     = msgs[i].author;
    o["type"]       = msgs[i].type;
    o["text"]       = msgs[i].text;
    o["expires"]    = msgs[i].expires;
    o["ownerToken"] = msgs[i].ownerToken;
    if (msgs[i].claimed) {
      o["claimed"]    = true;
      o["claimedBy"]  = msgs[i].claimedBy;
      o["claimToken"] = msgs[i].claimToken;
    }
    if (msgs[i].pollOptCount > 0) {
      JsonArray opts  = o.createNestedArray("options");
      JsonArray votes = o.createNestedArray("votes");
      for (uint8_t k = 0; k < msgs[i].pollOptCount; k++) {
        opts.add(msgs[i].pollOpts[k]);
        votes.add(msgs[i].pollVotes[k]);
      }
    }
    // Only emit reactions array if any are non-zero (saves space)
    if (msgs[i].reactions[0] || msgs[i].reactions[1] ||
        msgs[i].reactions[2] || msgs[i].reactions[3]) {
      JsonArray rxn = o.createNestedArray("reactions");
      for (uint8_t k = 0; k < 4; k++) rxn.add(msgs[i].reactions[k]);
    }
    if (msgs[i].authorColor > 0) {
      o["authorColor"] = msgs[i].authorColor;
    }
    serializeJson(o, tmp);
  }
  tmp.print("]");
  tmp.close();
  LittleFS.remove(Config::STORAGE_FILE);
  LittleFS.rename("/msgs.tmp", Config::STORAGE_FILE);
}

// Load buffer sized for 200 worst-case messages with poll data.
// If you trim MAX_MSGS, you can trim this. Heap will thank you.
#define MSG_LOAD_DOC_SIZE  102400

void loadMessages() {
  if (!LittleFS.exists(Config::STORAGE_FILE)) return;
  File f = LittleFS.open(Config::STORAGE_FILE);
  if (!f) return;
  DynamicJsonDocument doc(MSG_LOAD_DOC_SIZE);
  if (deserializeJson(doc, f)) { f.close(); return; }
  JsonArray arr = doc.as<JsonArray>();
  msgCount = 0;
  for (JsonObject o : arr) {
    if (msgCount >= Config::MAX_MSGS) break;
    msgs[msgCount].id         = o["id"] | nextMsgId;
    msgs[msgCount].author     = (const char*)o["author"];
    msgs[msgCount].type       = (const char*)o["type"];
    msgs[msgCount].text       = (const char*)o["text"];
    msgs[msgCount].expires    = o["expires"];
    msgs[msgCount].ownerToken = (const char*)(o["ownerToken"] | "");
    msgs[msgCount].claimed    = o["claimed"]    | false;
    msgs[msgCount].claimedBy  = (const char*)(o["claimedBy"]  | "");
    msgs[msgCount].claimToken = (const char*)(o["claimToken"] | "");
    msgs[msgCount].pollOptCount = 0;
    JsonArray opts  = o["options"];
    JsonArray votes = o["votes"];
    if (!opts.isNull()) {
      uint8_t k = 0;
      for (JsonVariant v : opts) {
        if (k >= 4) break;
        msgs[msgCount].pollOpts[k] = v.as<String>();
        msgs[msgCount].pollVotes[k] = votes.isNull() ? 0 : (uint16_t)(votes[k] | 0);
        k++;
      }
      msgs[msgCount].pollOptCount = k;
    }
    // Reactions and author color (default to zero if missing from old files)
    for (uint8_t k = 0; k < 4; k++) msgs[msgCount].reactions[k] = 0;
    JsonArray rxn = o["reactions"];
    if (!rxn.isNull()) {
      for (uint8_t k = 0; k < 4 && k < rxn.size(); k++) {
        msgs[msgCount].reactions[k] = (uint16_t)(rxn[k] | 0);
      }
    }
    msgs[msgCount].authorColor = (uint8_t)(o["authorColor"] | 0);
    if (msgs[msgCount].id >= nextMsgId) nextMsgId = msgs[msgCount].id + 1;
    msgCount++;
  }
  f.close();
}

// addMessage uses output references rather than returning a struct, so the
// Arduino IDE's prototype-injecting preprocessor doesn't trip over an unknown
// user-defined return type (it injects prototypes above struct definitions).
// Returns true on success, false if the board is genuinely full.
// On success, outId and outToken receive the new post's id and owner token.
bool addMessage(String author, String type, String text, int expiryHours,
                uint8_t pollCount, String* pollOpts, uint8_t authorColor,
                uint16_t& outId, String& outToken) {
  outId = 0;
  outToken = "";

  if (msgCount >= Config::MAX_MSGS) {
    unsigned long now = nowSecs();
    int evict = -1;
    unsigned long oldest = ULONG_MAX;
    for (int i = 0; i < msgCount; i++) {
      if (msgs[i].expires <= now && msgs[i].expires < oldest) {
        oldest = msgs[i].expires;
        evict = i;
      }
    }
    if (evict < 0) return false;  // genuinely full
    for (int i = evict; i < msgCount - 1; i++) msgs[i] = msgs[i + 1];
    msgCount--;
  }

  int i = msgCount;
  msgs[i].id           = nextMsgId++;
  msgs[i].author       = author;
  msgs[i].type         = type;
  msgs[i].text         = text;
  msgs[i].expires      = nowSecs() + (unsigned long)expiryHours * 3600UL;
  msgs[i].ownerToken   = generateShortToken();
  msgs[i].claimed      = false;
  msgs[i].claimedBy    = "";
  msgs[i].claimToken   = "";
  msgs[i].pollOptCount = pollCount;
  for (uint8_t k = 0; k < pollCount && k < 4; k++) {
    msgs[i].pollOpts[k]  = pollOpts[k];
    msgs[i].pollVotes[k] = 0;
  }
  for (uint8_t k = 0; k < 4; k++) msgs[i].reactions[k] = 0;
  msgs[i].authorColor  = authorColor;

  outId    = msgs[i].id;
  outToken = msgs[i].ownerToken;

  msgCount++;
  lastPostTime = nowSecs();
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  return true;
}

int findMessageIdx(uint16_t id) {
  for (int i = 0; i < msgCount; i++) {
    if (msgs[i].id == id) return i;
  }
  return -1;
}

// ===================== LED =====================
void updateLED() {
  if (!led_enabled) {
    analogWrite(led_pin, 0);
    return;
  }
  int  hour    = currentHour();
  bool isNight = (hour >= led_night_start || hour < led_day_start);
  int  maxBr   = map(isNight ? led_night_brightness : led_day_brightness, 0, 100, 0, 255);

  if (!led_pulse_enabled) {
    analogWrite(led_pin, maxBr);
    return;
  }
  bool recent = led_activity_enabled && (nowSecs() - lastPostTime) < (3 * 3600);
  float speed = recent ? 0.01f : 0.003f;
  int   bright = (int)((sin(millis() * speed) + 1.0f) * (maxBr / 2.0f));
  analogWrite(led_pin, bright);
}

// ===================== HTML: MAIN BOARD =====================
// INDEX_HTML lives in Community_Hub_pages.h (see comment in that file
// explaining why this content was extracted).

// Escapes a string for safe injection into a JS double-quoted string literal.
String jsEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else                out += c;
  }
  return out;
}

// ===================== HTML: ADMIN PANEL =====================
// The admin key is NEVER sent to the browser.
// The gate POSTs the key to /admin/auth which returns a session token.
// All subsequent admin calls use token= not key=.

String buildAdminPage() {
  String page = FPSTR(ADMIN_PAGE_HEAD);

  page += "let SESSION_TOKEN = '';\n";

  page += "window.addEventListener('DOMContentLoaded', () => {\n";
  page += "  document.getElementById('idName').value     = \"" + jsEscape(id_name)     + "\";\n";
  page += "  document.getElementById('idIcon').value     = \"" + jsEscape(id_icon)     + "\";\n";
  page += "  document.getElementById('idTagline').value  = \"" + jsEscape(id_tagline)  + "\";\n";
  page += "  document.getElementById('idRules').value    = \"" + jsEscape(id_rules)    + "\";\n";
  page += "  document.getElementById('idFooter').value   = \"" + jsEscape(id_footer)   + "\";\n";
  page += "  document.getElementById('idHostname').value = \"" + jsEscape(id_hostname) + "\";\n";
  page += "  updateHostnamePreview();\n";
  page += "});\n";

  page += FPSTR(ADMIN_PAGE_TAIL);

  return page;
}


// ===================== WEB SERVER =====================
DNSServer dnsServer;
WebServer server(80);

bool checkKey() {
  if (sessionToken.length() == 0)                      return false;
  if (millis() - tokenIssuedAt > TOKEN_LIFETIME_MS)    return false;
  if (!server.hasArg("token"))                         return false;
  return server.arg("token") == sessionToken;
}

String sanitize(const String& s, int maxLen) {
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c != '<' && c != '>') out += c;
  }
  out.trim();
  if ((int)out.length() > maxLen) out = out.substring(0, maxLen);
  return out;
}

String validateType(const String& t) {
  if (t == "Notice" || t == "Offer" || t == "Need" || t == "Event" || t == "Poll") return t;
  return "Notice";
}

void handleRoot()  { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleAdmin() { server.send(200, "text/html; charset=utf-8", buildAdminPage()); }
void handleWall()  { server.send_P(200, "text/html; charset=utf-8", WALL_HTML); }

void handleAdminAuth() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad request");
    return;
  }
  String submitted = doc["key"] | "";
  if (submitted == adminKey) {
    sessionToken = generateToken();
    server.send(200, "text/plain", sessionToken);
  } else {
    server.send(403, "text/plain", "forbidden");
  }
}

void handleInfo() {
  DynamicJsonDocument doc(512);
  doc["name"]     = id_name;
  doc["icon"]     = id_icon;
  doc["tagline"]  = id_tagline;
  doc["rules"]    = id_rules;
  doc["footer"]   = id_footer;
  doc["hostname"] = effectiveHostname();
  doc["uptime"]   = formatUptime();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Public messages feed. NEVER includes ownerToken or claimToken.
void handleMessages() {
  DynamicJsonDocument doc(20480);
  JsonArray arr = doc.to<JsonArray>();
  unsigned long now = nowSecs();
  for (int i = 0; i < msgCount; i++) {
    if (msgs[i].expires < now) continue;
    JsonObject o = arr.createNestedObject();
    o["id"]      = msgs[i].id;
    o["author"]  = msgs[i].author;
    o["type"]    = msgs[i].type;
    o["text"]    = msgs[i].text;
    o["expires"] = msgs[i].expires;
    if (msgs[i].authorColor > 0) o["authorColor"] = msgs[i].authorColor;
    if (msgs[i].claimed) {
      o["claimed"]   = true;
      o["claimedBy"] = msgs[i].claimedBy;
    }
    if (msgs[i].pollOptCount > 0) {
      JsonArray opts  = o.createNestedArray("options");
      JsonArray votes = o.createNestedArray("votes");
      for (uint8_t k = 0; k < msgs[i].pollOptCount; k++) {
        opts.add(msgs[i].pollOpts[k]);
        votes.add(msgs[i].pollVotes[k]);
      }
    }
    // Always emit reactions array so client doesn't have to check (4 small ints).
    JsonArray rxn = o.createNestedArray("reactions");
    for (uint8_t k = 0; k < 4; k++) rxn.add(msgs[i].reactions[k]);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePost() {
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  String  author      = sanitize(doc["author"] | "neighbor", 24);
  String  type        = validateType(doc["type"] | "Notice");
  String  text        = sanitize(doc["text"]   | "", 300);
  int     expiry      = doc["expiry"] | Config::DEFAULT_EXPIRY_HOURS;
  uint8_t authorColor = (uint8_t)(doc["authorColor"] | 0);
  if (authorColor > 7) authorColor = 0;  // palette is 0..7

  uint8_t pollCount = 0;
  String  pollOpts[4];
  if (type == "Poll") {
    JsonArray opts = doc["options"];
    if (!opts.isNull()) {
      for (JsonVariant v : opts) {
        if (pollCount >= 4) break;
        String o = sanitize(v.as<String>(), 60);
        if (o.length() == 0) continue;
        pollOpts[pollCount++] = o;
      }
    }
    if (pollCount < 2) {
      server.send(400, "text/plain", "polls need at least 2 options");
      return;
    }
  }

  if (author.isEmpty()) author = "neighbor";
  if (text.isEmpty())   { server.send(400, "text/plain", "empty message"); return; }

  uint16_t newId;
  String   newToken;
  if (!addMessage(author, type, text, expiry, pollCount, pollOpts, authorColor, newId, newToken)) {
    server.send(503, "text/plain", "board full");
    return;
  }

  DynamicJsonDocument resp(256);
  resp["id"]    = newId;
  resp["token"] = newToken;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handlePostEdit() {
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id    = doc["id"] | 0;
  String   token = doc["token"] | "";
  String   text  = sanitize(doc["text"] | "", 300);
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (token.length() == 0 || token != msgs[idx].ownerToken) {
    server.send(403, "text/plain", "forbidden"); return;
  }
  if (text.isEmpty())                                   { server.send(400, "text/plain", "empty"); return; }
  msgs[idx].text = text;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePostDelete() {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id    = doc["id"] | 0;
  String   token = doc["token"] | "";
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (token.length() == 0 || token != msgs[idx].ownerToken) {
    server.send(403, "text/plain", "forbidden"); return;
  }
  for (int j = idx; j < msgCount - 1; j++) msgs[j] = msgs[j + 1];
  msgCount--;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePostClaim() {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id   = doc["id"] | 0;
  String   name = sanitize(doc["name"] | "neighbor", 24);
  if (name.isEmpty()) name = "neighbor";
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (msgs[idx].type != "Offer" && msgs[idx].type != "Need") {
    server.send(400, "text/plain", "not claimable"); return;
  }
  if (msgs[idx].claimed)                                { server.send(409, "text/plain", "already claimed"); return; }

  msgs[idx].claimed    = true;
  msgs[idx].claimedBy  = name;
  msgs[idx].claimToken = generateShortToken();
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }

  DynamicJsonDocument resp(256);
  resp["token"] = msgs[idx].claimToken;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handlePostUnclaim() {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id    = doc["id"] | 0;
  String   token = doc["token"] | "";
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (!msgs[idx].claimed)                               { server.send(400, "text/plain", "not claimed"); return; }
  // Either the claim token or the owner token will do.
  if (token.length() == 0 ||
      (token != msgs[idx].claimToken && token != msgs[idx].ownerToken)) {
    server.send(403, "text/plain", "forbidden"); return;
  }
  msgs[idx].claimed    = false;
  msgs[idx].claimedBy  = "";
  msgs[idx].claimToken = "";
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePollVote() {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id     = doc["id"] | 0;
  int      option = doc["option"] | -1;
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (msgs[idx].type != "Poll")                         { server.send(400, "text/plain", "not a poll"); return; }
  if (option < 0 || option >= msgs[idx].pollOptCount)   { server.send(400, "text/plain", "bad option"); return; }
  // Cap at uint16 max just in case the poll goes viral in the neighborhood.
  if (msgs[idx].pollVotes[option] < 0xFFFF) msgs[idx].pollVotes[option]++;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

// Reaction handler. Bumps one of four counters per post.
// Client-side localStorage prevents the same browser from double-counting;
// not bulletproof (clear-and-react again works), but the trust model here
// matches the rest of the board.
void handlePostReact() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id   = doc["id"] | 0;
  int      type = doc["type"] | -1;
  int idx = findMessageIdx(id);
  if (idx < 0)                  { server.send(404, "text/plain", "not found"); return; }
  if (type < 0 || type > 3)     { server.send(400, "text/plain", "bad type"); return; }
  if (msgs[idx].reactions[type] < 0xFFFF) msgs[idx].reactions[type]++;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

// ===================== WAVE =====================
// Lightweight transient "hello" broadcast. Each wave gets a monotonic id and a
// creation timestamp (in millis). Clients poll /wave/recent?since=N every few
// seconds and animate any new entries. Waves older than WAVE_TTL_MS are dropped
// so latecomers don't see stale activity.

struct WaveEntry {
  uint32_t      id;
  unsigned long createdMs;
  String        icon;     // emoji, 1-8 bytes
  String        from;     // optional name
};

#define MAX_WAVES   20
#define WAVE_TTL_MS 10000UL

WaveEntry waves[MAX_WAVES];
int       waveCount  = 0;     // 0..MAX_WAVES, oldest at [0]
uint32_t  nextWaveId = 1;

void pruneWaves() {
  unsigned long now = millis();
  int keep = 0;
  for (int i = 0; i < waveCount; i++) {
    if (now - waves[i].createdMs <= WAVE_TTL_MS) {
      if (keep != i) waves[keep] = waves[i];
      keep++;
    }
  }
  waveCount = keep;
}

void handleWavePost() {
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  String icon = sanitize(doc["icon"] | "👋", 8);
  String from = sanitize(doc["from"] | "neighbor", 24);
  if (icon.isEmpty()) icon = "👋";
  if (from.isEmpty()) from = "neighbor";

  pruneWaves();
  if (waveCount >= MAX_WAVES) {
    // Drop the oldest to make room (ring buffer behavior)
    for (int i = 0; i < MAX_WAVES - 1; i++) waves[i] = waves[i + 1];
    waveCount = MAX_WAVES - 1;
  }
  waves[waveCount].id        = nextWaveId++;
  waves[waveCount].createdMs = millis();
  waves[waveCount].icon      = icon;
  waves[waveCount].from      = from;
  waveCount++;

  DynamicJsonDocument resp(64);
  resp["id"] = waves[waveCount - 1].id;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handleWaveRecent() {
  uint32_t since = 0;
  if (server.hasArg("since")) since = (uint32_t)server.arg("since").toInt();
  pruneWaves();
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < waveCount; i++) {
    if (waves[i].id <= since) continue;
    JsonObject o = arr.createNestedObject();
    o["id"]   = waves[i].id;
    o["icon"] = waves[i].icon;
    o["from"] = waves[i].from;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ===================== WALL =====================
// Shared community graffiti canvas. Strokes are stored as polylines on a fixed
// 1000x600 virtual canvas; the client scales to fit its viewport.
//
// Fade-out timing:
//   - Strokes are full opacity for 7 days
//   - Then fade linearly to 20% over the next 7 days
//   - At 14 days they drop off the wall entirely
//
// Memory budget (worst case, fixed allocation):
//   MAX_STROKES * (metadata + MAX_POINTS * 4 bytes) ~= 150 * (12 + 240) = ~38 KB
//
// Persistence happens via the same dirty-flag pattern as messages.

#define MAX_STROKES            150
#define MAX_POINTS_PER_STROKE  60
#define WALL_FADE_START_SECS   (7UL  * 86400UL)
#define WALL_LIFETIME_SECS     (14UL * 86400UL)

struct Stroke {
  uint32_t       id;
  unsigned long  createdEpoch;
  uint8_t        color;       // palette index, see WALL_COLORS in pages.h
  uint8_t        width;       // brush index, 0..3
  uint8_t        pointCount;  // 0..MAX_POINTS_PER_STROKE
  int16_t        xs[MAX_POINTS_PER_STROKE];
  int16_t        ys[MAX_POINTS_PER_STROKE];
};

// Heap-allocated in setup() rather than static, because at ~37 KB this array
// overflows the ESP32's DRAM `.bss` section if declared statically alongside
// the WiFi stack, message buffers, and identity strings. The heap allocation
// reserves the same RAM but does it at runtime, after the linker is done.
Stroke*  strokes         = nullptr;
int      strokeCount     = 0;
uint32_t nextStrokeId    = 1;
bool     wallDirty       = false;
unsigned long lastWallDirtyTime = 0;

// Drop strokes older than WALL_LIFETIME_SECS. Called before reading, writing,
// or persisting. Shifts remaining strokes down to fill gaps.
void pruneStrokes() {
  if (!strokes) return;
  unsigned long now = nowSecs();
  int keep = 0;
  for (int i = 0; i < strokeCount; i++) {
    if (now - strokes[i].createdEpoch <= WALL_LIFETIME_SECS) {
      if (keep != i) strokes[keep] = strokes[i];
      keep++;
    }
  }
  if (keep != strokeCount) {
    strokeCount = keep;
    if (!wallDirty) { wallDirty = true; lastWallDirtyTime = millis(); }
  }
}

// Stream save: write strokes directly to file rather than building a giant doc.
void saveWall() {
  if (!strokes) return;
  pruneStrokes();
  File tmp = LittleFS.open("/wall.tmp", FILE_WRITE);
  if (!tmp) return;
  tmp.print("[");
  for (int i = 0; i < strokeCount; i++) {
    if (i > 0) tmp.print(",");
    tmp.print("{\"id\":");
    tmp.print(strokes[i].id);
    tmp.print(",\"t\":");
    tmp.print(strokes[i].createdEpoch);
    tmp.print(",\"c\":");
    tmp.print(strokes[i].color);
    tmp.print(",\"w\":");
    tmp.print(strokes[i].width);
    tmp.print(",\"p\":[");
    for (uint8_t k = 0; k < strokes[i].pointCount; k++) {
      if (k > 0) tmp.print(",");
      tmp.print(strokes[i].xs[k]);
      tmp.print(",");
      tmp.print(strokes[i].ys[k]);
    }
    tmp.print("]}");
  }
  tmp.print("]");
  tmp.close();
  LittleFS.remove("/wall.json");
  LittleFS.rename("/wall.tmp", "/wall.json");
}

void loadWall() {
  if (!strokes) return;
  if (!LittleFS.exists("/wall.json")) return;
  File f = LittleFS.open("/wall.json");
  if (!f) return;
  // Each stroke serializes to roughly 200-600 bytes of JSON. Budget 64 KB for
  // the parse buffer, which comfortably handles a full 150-stroke wall.
  DynamicJsonDocument doc(65536);
  if (deserializeJson(doc, f)) { f.close(); return; }
  JsonArray arr = doc.as<JsonArray>();
  strokeCount = 0;
  for (JsonObject o : arr) {
    if (strokeCount >= MAX_STROKES) break;
    Stroke& s = strokes[strokeCount];
    s.id           = o["id"] | nextStrokeId;
    s.createdEpoch = o["t"]  | 0;
    s.color        = (uint8_t)(o["c"] | 0);
    s.width        = (uint8_t)(o["w"] | 1);
    s.pointCount   = 0;
    JsonArray p = o["p"];
    if (!p.isNull()) {
      // Points are stored as a flat [x0,y0,x1,y1,...] array
      uint8_t pairs = p.size() / 2;
      if (pairs > MAX_POINTS_PER_STROKE) pairs = MAX_POINTS_PER_STROKE;
      for (uint8_t k = 0; k < pairs; k++) {
        s.xs[k] = (int16_t)(p[k * 2]     | 0);
        s.ys[k] = (int16_t)(p[k * 2 + 1] | 0);
      }
      s.pointCount = pairs;
    }
    if (s.id >= nextStrokeId) nextStrokeId = s.id + 1;
    strokeCount++;
  }
  f.close();
}

// GET /wall/data — returns all currently-visible strokes.
// Stream-serializes directly to the response to avoid building a 50+ KB
// JsonDocument in RAM. Uses chunked transfer encoding via sendContent().
//
// Note: server.send(200, type, "") with an empty body finalizes the response;
// subsequent writes are ignored. We must call send() with the headers, then
// use sendContent() repeatedly to stream the body.
void handleWallGet() {
  pruneStrokes();

  // Build the body in chunks. Each loop iteration accumulates a small string
  // and flushes it. Keeping the buffer small avoids large RAM spikes.
  String chunk;
  chunk.reserve(512);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");  // headers only

  chunk = "[";
  for (int i = 0; i < strokeCount; i++) {
    if (i > 0) chunk += ",";
    chunk += "{\"id\":";    chunk += strokes[i].id;
    chunk += ",\"t\":";     chunk += strokes[i].createdEpoch;
    chunk += ",\"c\":";     chunk += strokes[i].color;
    chunk += ",\"w\":";     chunk += strokes[i].width;
    chunk += ",\"p\":[";
    for (uint8_t k = 0; k < strokes[i].pointCount; k++) {
      if (k > 0) chunk += ",";
      chunk += strokes[i].xs[k];
      chunk += ",";
      chunk += strokes[i].ys[k];
    }
    chunk += "]}";

    // Flush every ~1 KB so we never hold too much in RAM at once
    if (chunk.length() > 1024) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]";
  server.sendContent(chunk);
  server.sendContent("");  // empty chunk signals end of response
}

// POST /wall/stroke — add a new stroke
//   Body: { color, width, points: [x0,y0,x1,y1,...] }
void handleWallStroke() {
  if (!strokes) { server.send(503, "text/plain", "wall unavailable"); return; }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint8_t color = (uint8_t)(doc["color"] | 0);
  uint8_t width = (uint8_t)(doc["width"] | 1);
  // Palette is 0..11 (12 graffiti colors), brushes are 0..3
  if (color > 11) color = 0;
  if (width > 3)  width = 1;

  JsonArray p = doc["points"];
  if (p.isNull() || p.size() < 4 || (p.size() % 2) != 0) {
    server.send(400, "text/plain", "need at least 2 points (4 values)"); return;
  }

  pruneStrokes();

  // Evict oldest stroke if full (FIFO; oldest is at [0] since we shift on prune)
  if (strokeCount >= MAX_STROKES) {
    for (int i = 0; i < MAX_STROKES - 1; i++) strokes[i] = strokes[i + 1];
    strokeCount = MAX_STROKES - 1;
  }

  Stroke& s = strokes[strokeCount];
  s.id           = nextStrokeId++;
  s.createdEpoch = nowSecs();
  s.color        = color;
  s.width        = width;
  uint8_t pairs = p.size() / 2;
  if (pairs > MAX_POINTS_PER_STROKE) pairs = MAX_POINTS_PER_STROKE;
  for (uint8_t k = 0; k < pairs; k++) {
    int x = p[k * 2]     | 0;
    int y = p[k * 2 + 1] | 0;
    // Clamp to virtual canvas (1000x600) with a small margin
    if (x < 0)    x = 0;
    if (x > 1000) x = 1000;
    if (y < 0)    y = 0;
    if (y > 600)  y = 600;
    s.xs[k] = (int16_t)x;
    s.ys[k] = (int16_t)y;
  }
  s.pointCount = pairs;
  strokeCount++;

  if (!wallDirty) { wallDirty = true; lastWallDirtyTime = millis(); }

  DynamicJsonDocument resp(64);
  resp["id"] = s.id;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

// GET /wall/info — small endpoint for the "wall has N new strokes" indicator
// on the main board header. Returns latest stroke id and total stroke count.
void handleWallInfo() {
  pruneStrokes();
  uint32_t latestId = 0;
  for (int i = 0; i < strokeCount; i++) {
    if (strokes[i].id > latestId) latestId = strokes[i].id;
  }
  DynamicJsonDocument doc(96);
  doc["latest"] = latestId;
  doc["count"]  = strokeCount;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Admin-only: wipe the wall
void handleAdminWallClear() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  strokeCount = 0;
  saveWall();
  wallDirty = false;
  server.send(200, "text/plain", "wall cleared");
}

// Public health endpoint. Mild info only (heap, uptime, client count, storage),
// kept unauthenticated so it can be polled by external dashboards or an e-paper
// companion device. Add checkKey() at the top if you'd rather gate it.
void handleHealth() {
  DynamicJsonDocument doc(512);

  // Heap
  doc["free_heap"]     = ESP.getFreeHeap();
  doc["min_free_heap"] = ESP.getMinFreeHeap();  // low-water mark since boot
  doc["heap_size"]     = ESP.getHeapSize();

  // Messages
  doc["msg_count"] = msgCount;
  doc["max_msgs"]  = Config::MAX_MSGS;

  // Wall
  doc["stroke_count"] = strokeCount;
  doc["max_strokes"]  = MAX_STROKES;

  int claimedCount = 0, pollCount = 0, expiredCount = 0;
  unsigned long now = nowSecs();
  for (int i = 0; i < msgCount; i++) {
    if (msgs[i].claimed)          claimedCount++;
    if (msgs[i].pollOptCount > 0) pollCount++;
    if (msgs[i].expires < now)    expiredCount++;
  }
  doc["claimed_count"] = claimedCount;
  doc["poll_count"]    = pollCount;
  doc["expired_count"] = expiredCount;

  // Uptime, both forms
  doc["uptime_secs"] = (millis() - bootMillis) / 1000;
  doc["uptime_str"]  = formatUptime();

  // Storage
  doc["fs_used"]  = LittleFS.usedBytes();
  doc["fs_total"] = LittleFS.totalBytes();

  // Network
  doc["wifi_clients"] = WiFi.softAPgetStationNum();

  // Activity (omitted entirely if no posts since boot)
  if (lastPostTime > 0) {
    doc["last_post_secs_ago"] = (long)(now - lastPostTime);
  }

  // State flags
  doc["msgs_dirty"] = msgsDirty;
  doc["time_set"]   = baseEpoch > 0;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ── Admin handlers ────────────────────────────────────────────────────────────

void handleAdminIdentityGet() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  DynamicJsonDocument doc(1024);
  doc["name"]              = id_name;
  doc["icon"]              = id_icon;
  doc["tagline"]           = id_tagline;
  doc["rules"]             = id_rules;
  doc["footer"]            = id_footer;
  doc["hostname"]          = id_hostname;          // raw override, may be empty
  doc["effectiveHostname"] = effectiveHostname();  // the slug we actually advertise
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAdminIdentitySet() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (server.hasArg("name")    && server.arg("name").length())
    id_name    = sanitize(server.arg("name"),    48);
  if (server.hasArg("icon")    && server.arg("icon").length())
    id_icon    = sanitize(server.arg("icon"),     8);
  if (server.hasArg("tagline"))
    id_tagline = sanitize(server.arg("tagline"), 100);
  if (server.hasArg("rules"))
    id_rules   = sanitize(server.arg("rules"),   100);
  if (server.hasArg("footer"))
    id_footer  = sanitize(server.arg("footer"),  100);
  if (server.hasArg("hostname")) {
    // Slug client-side input too so admin can type whatever; we store the slug
    // (or empty if they cleared it, meaning "derive from name").
    String raw = server.arg("hostname");
    raw.trim();
    id_hostname = raw.length() ? slugify(raw) : "";
  }
  saveIdentityConfig();
  // Re-advertise mDNS in case the effective hostname changed.
  startMdns();
  server.send(200, "text/plain", "identity saved");
}

void handleAdminTime() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (!setTimeFromString(server.arg("time"))) {
    server.send(400, "text/plain", "bad format — use DDMMYYYY-HHMM");
    return;
  }
  saveTime();
  server.send(200, "text/plain", "time set");
}

void handleAdminLedGet() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  DynamicJsonDocument doc(512);
  doc["day_br"]   = led_day_brightness;
  doc["night_br"] = led_night_brightness;
  doc["day_st"]   = led_day_start;
  doc["night_st"] = led_night_start;
  doc["pin"]      = led_pin;
  doc["enabled"]  = led_enabled;
  doc["pulse"]    = led_pulse_enabled;
  doc["activity"] = led_activity_enabled;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAdminLedSet() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (server.hasArg("day_br"))   led_day_brightness   = constrain(server.arg("day_br").toInt(),   0, 100);
  if (server.hasArg("night_br")) led_night_brightness = constrain(server.arg("night_br").toInt(), 0, 100);
  if (server.hasArg("day_st"))   led_day_start        = constrain(server.arg("day_st").toInt(),   0, 23);
  if (server.hasArg("night_st")) led_night_start      = constrain(server.arg("night_st").toInt(), 0, 23);
  if (server.hasArg("pin")) {
    int newPin = constrain(server.arg("pin").toInt(), 0, 48);
    if (newPin != led_pin) {
      analogWrite(led_pin, 0);
      pinMode(led_pin, INPUT);
      led_pin = newPin;
      pinMode(led_pin, OUTPUT);
    }
  }
  if (server.hasArg("enabled"))  led_enabled          = server.arg("enabled")  == "1";
  if (server.hasArg("pulse"))    led_pulse_enabled    = server.arg("pulse")    == "1";
  if (server.hasArg("activity")) led_activity_enabled = server.arg("activity") == "1";
  saveLedConfig();
  server.send(200, "text/plain", "LED settings saved");
}

void handleAdminBackup() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (msgsDirty) { saveMessages(); msgsDirty = false; }
  File f = LittleFS.open(Config::STORAGE_FILE);
  if (!f) { server.send(500, "text/plain", "no file"); return; }
  String out = f.readString();
  f.close();
  server.send(200, "application/json", out);
}

void handleAdminRestore() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  DynamicJsonDocument doc(MSG_LOAD_DOC_SIZE);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  msgCount = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (msgCount >= Config::MAX_MSGS) break;
    msgs[msgCount].id           = o["id"] | nextMsgId;
    msgs[msgCount].author       = (const char*)o["author"];
    msgs[msgCount].type         = (const char*)o["type"];
    msgs[msgCount].text         = (const char*)o["text"];
    msgs[msgCount].expires      = o["expires"];
    msgs[msgCount].ownerToken   = (const char*)(o["ownerToken"] | "");
    msgs[msgCount].claimed      = o["claimed"]    | false;
    msgs[msgCount].claimedBy    = (const char*)(o["claimedBy"]  | "");
    msgs[msgCount].claimToken   = (const char*)(o["claimToken"] | "");
    msgs[msgCount].pollOptCount = 0;
    JsonArray opts  = o["options"];
    JsonArray votes = o["votes"];
    if (!opts.isNull()) {
      uint8_t k = 0;
      for (JsonVariant v : opts) {
        if (k >= 4) break;
        msgs[msgCount].pollOpts[k]  = v.as<String>();
        msgs[msgCount].pollVotes[k] = votes.isNull() ? 0 : (uint16_t)(votes[k] | 0);
        k++;
      }
      msgs[msgCount].pollOptCount = k;
    }
    for (uint8_t k = 0; k < 4; k++) msgs[msgCount].reactions[k] = 0;
    JsonArray rxn = o["reactions"];
    if (!rxn.isNull()) {
      for (uint8_t k = 0; k < 4 && k < rxn.size(); k++) {
        msgs[msgCount].reactions[k] = (uint16_t)(rxn[k] | 0);
      }
    }
    msgs[msgCount].authorColor = (uint8_t)(o["authorColor"] | 0);
    if (msgs[msgCount].id >= nextMsgId) nextMsgId = msgs[msgCount].id + 1;
    msgCount++;
  }
  saveMessages();
  server.send(200, "text/plain", "restored " + String(msgCount) + " messages");
}

void handleAdminSetKey() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  String newKey = server.arg("newkey");
  newKey.trim();
  if (newKey.length() < 4) {
    server.send(400, "text/plain", "key must be at least 4 characters");
    return;
  }
  adminKey = newKey;
  saveAdminKey();
  server.send(200, "text/plain", "key updated — page will reload");
}

void handleAdminOTA() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  server.send(200, "text/plain", Update.hasError() ? "UPDATE FAILED" : "UPDATE OK — rebooting");
  delay(500);
  ESP.restart();
}

void handleAdminOTAUpload() {
  if (!checkKey()) return;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Starting: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }
    Serial.printf("[OTA] Written %u bytes\n", upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { Serial.printf("[OTA] Success: %u bytes total\n", upload.totalSize); }
    else                  { Update.printError(Serial); }
  }
}

void handleAdminFlush() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  saveMessages();
  msgsDirty = false;
  saveWall();
  wallDirty = false;
  saveTime();
  server.send(200, "text/plain", "flushed");
}

void handleAdminDeletePost() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (!server.hasArg("id")) { server.send(400, "text/plain", "missing id"); return; }
  uint16_t targetId = (uint16_t)server.arg("id").toInt();
  int idx = findMessageIdx(targetId);
  if (idx < 0) { server.send(404, "text/plain", "not found"); return; }
  for (int j = idx; j < msgCount - 1; j++) msgs[j] = msgs[j + 1];
  msgCount--;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "deleted");
}

void handleAdminClear() {
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  msgCount = 0;
  saveMessages();
  server.send(200, "text/plain", "cleared");
}

// ===================== SETUP =====================
void setup() {
  bootMillis = millis();
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("╔══════════════════════════════╗");
  Serial.println("║      C O M M U N I T Y       ║");
  Serial.println("║            H U B             ║");
  Serial.println("╚══════════════════════════════╝");

  pinMode(led_pin, OUTPUT);

  // WiFi Access Point
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(Config::AP_SSID,
                          Config::AP_PASS[0] ? Config::AP_PASS : nullptr,
                          Config::AP_CHANNEL, 0, Config::AP_MAX_CONN);
  delay(200);

  if (!apOk) {
    Serial.println("⚠  softAP() failed. Check AP_PASS is empty or 8+ chars.");
  }

  IPAddress apIP = WiFi.softAPIP();
  Serial.println("✓ Access Point started.");
  Serial.print("  SSID : "); Serial.println(Config::AP_SSID);
  Serial.print("  IP   : "); Serial.println(apIP);
  Serial.print("  Pass : "); Serial.println(Config::AP_PASS[0] ? Config::AP_PASS : "(open)");
  Serial.print("  Admin: http://"); Serial.print(apIP); Serial.println("/admin");

  dnsServer.start(53, "*", apIP);

  // Heap allocation for the strokes ring buffer. ~37 KB at MAX_STROKES=150.
  // Done here rather than statically to keep DRAM `.bss` within ESP32 limits.
  // If this fails the wall stays disabled but the rest of the board works fine.
  strokes = (Stroke*) calloc(MAX_STROKES, sizeof(Stroke));
  if (!strokes) {
    Serial.println("⚠  Failed to allocate strokes buffer — wall disabled.");
  } else {
    Serial.printf("✓ Strokes buffer allocated: %u bytes\n",
                  (unsigned)(MAX_STROKES * sizeof(Stroke)));
  }

  if (!LittleFS.begin(true)) {
    Serial.println("⚠  LittleFS init failed — running without persistence.");
  } else {
    Serial.println("✓ LittleFS mounted.");
    loadTime();
    loadLedConfig();
    loadAdminKey();
    loadIdentityConfig();
    loadMessages();
    if (strokes) loadWall();  // skip wall load if allocation failed
    Serial.printf("  Loaded %d message(s), %d stroke(s).\n", msgCount, strokeCount);
  }

  // mDNS: advertise http://<hostname>.local so neighbors don't have to memorize
  // the IP. Must come after identity loads (so we know the effective hostname)
  // and after the AP is up (so there's a network interface to bind to).
  startMdns();

  // Public routes
  server.on("/",                   handleRoot);
  server.on("/wall",               handleWall);
  server.on("/admin",              handleAdmin);
  server.on("/admin/auth", HTTP_POST, handleAdminAuth);
  server.on("/info",               handleInfo);
  server.on("/messages",           handleMessages);
  server.on("/post",         HTTP_POST, handlePost);
  server.on("/post/edit",    HTTP_POST, handlePostEdit);
  server.on("/post/delete",  HTTP_POST, handlePostDelete);
  server.on("/post/claim",   HTTP_POST, handlePostClaim);
  server.on("/post/unclaim", HTTP_POST, handlePostUnclaim);
  server.on("/post/react",   HTTP_POST, handlePostReact);
  server.on("/poll/vote",    HTTP_POST, handlePollVote);
  server.on("/wave",         HTTP_POST, handleWavePost);
  server.on("/wave/recent",  HTTP_GET,  handleWaveRecent);
  server.on("/wall/data",    HTTP_GET,  handleWallGet);
  server.on("/wall/stroke",  HTTP_POST, handleWallStroke);
  server.on("/wall/info",    HTTP_GET,  handleWallInfo);

  server.on("/api/status", HTTP_GET, []() {
    unsigned long now = nowSecs();
    bool hasExpired = false;
    for (int i = 0; i < msgCount; i++) {
      if (msgs[i].expires <= now) { hasExpired = true; break; }
    }
    bool boardFull = (msgCount >= Config::MAX_MSGS) && !hasExpired;
    DynamicJsonDocument doc(64);
    doc["full"] = boardFull;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/health", HTTP_GET, handleHealth);

  // Captive portal probes
  server.on("/hotspot-detect.html",       []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/library/test/success.html", []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/success.html",              []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/captive.apple.com",         []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/generate_204",              []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/gen_204",                   []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/connectivitycheck",         []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/connectivity-check",        []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/connecttest.txt",           []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/ncsi.txt",                  []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/redirect",                  []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/fwlink/",                   []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/fwlink",                    []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/success.txt",               []() { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/canonical.html",            []() { server.sendHeader("Location", "/"); server.send(302); });

  // Admin routes
  server.on("/admin/identity/get",  handleAdminIdentityGet);
  server.on("/admin/identity/set",  handleAdminIdentitySet);
  server.on("/admin/time",          handleAdminTime);
  server.on("/admin/led/get",       handleAdminLedGet);
  server.on("/admin/led/set",       handleAdminLedSet);
  server.on("/admin/backup",        handleAdminBackup);
  server.on("/admin/restore", HTTP_POST, handleAdminRestore);
  server.on("/admin/setkey",        handleAdminSetKey);
  server.on("/admin/ota", HTTP_POST, handleAdminOTA, handleAdminOTAUpload);
  server.on("/admin/flush",         handleAdminFlush);
  server.on("/admin/clear",         handleAdminClear);
  server.on("/admin/wall/clear",    handleAdminWallClear);
  server.on("/admin/delete/post",   handleAdminDeletePost);

  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302); });

  server.begin();
  Serial.println("✓ HTTP server started.\n");
}

// ===================== LOOP =====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateLED();

  unsigned long now = millis();
  if (msgsDirty && (now - lastMsgDirtyTime) >= 60000) {
    saveMessages();
    msgsDirty = false;
  }
  if (wallDirty && (now - lastWallDirtyTime) >= 10000) {
    saveWall();
    wallDirty = false;
  }
  if (now - lastTimeSave > 1800000) {
    saveTime();
    lastTimeSave = now;
  }
}
