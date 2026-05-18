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
#include "Community_Hub_pages_gz.h"


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

// ===================== PINNED POST =====================
// Admin pins one post at a time to surface above the regular board. The pinned
// post is exempt from normal expiry filtering so it doesn't disappear at 72h.
// pinnedMsgId == 0 means nothing is pinned.
uint16_t pinnedMsgId = 0;

void savePinned() {
  DynamicJsonDocument doc(64);
  doc["id"] = pinnedMsgId;
  File tmp = LittleFS.open("/pin.tmp", FILE_WRITE);
  if (!tmp) return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove("/pinned.json");
  LittleFS.rename("/pin.tmp", "/pinned.json");
}

void loadPinned() {
  if (!LittleFS.exists("/pinned.json")) return;
  File f = LittleFS.open("/pinned.json");
  if (!f) return;
  DynamicJsonDocument doc(64);
  if (!deserializeJson(doc, f)) {
    pinnedMsgId = (uint16_t)(doc["id"] | 0);
  }
  f.close();
}

// Called from any path that deletes a message, so a stale pinnedMsgId never
// outlives its target post.
void clearPinIfMatches(uint16_t id) {
  if (pinnedMsgId == id && id != 0) {
    pinnedMsgId = 0;
    savePinned();
  }
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

// ===================== RUNTIME STATS =====================
// In-RAM counters, reset on reboot. Persisting them adds complexity for not
// much value; the "since boot" framing matches the uptime field nicely.
uint32_t stats_posts_total     = 0;
uint32_t stats_reactions_total = 0;
uint32_t stats_claims_total    = 0;
uint32_t stats_votes_total     = 0;
uint32_t stats_waves_total     = 0;
uint32_t stats_strokes_total   = 0;


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

// ===================== MESSAGE TYPE ENUM =====================
// Stored as a single byte in RAM instead of a String. Saves ~25 bytes per
// message vs the old `String type` field. The wire/disk format keeps the
// human-readable strings ("Notice", "Offer", ...) for compatibility.
enum MsgType : uint8_t {
  MSG_NOTICE = 0,
  MSG_OFFER  = 1,
  MSG_NEED   = 2,
  MSG_EVENT  = 3,
  MSG_POLL   = 4
};

const char* msgTypeToString(uint8_t t) {
  switch (t) {
    case MSG_OFFER: return "Offer";
    case MSG_NEED:  return "Need";
    case MSG_EVENT: return "Event";
    case MSG_POLL:  return "Poll";
    default:        return "Notice";
  }
}

uint8_t stringToMsgType(const char* s) {
  if (!s)                          return MSG_NOTICE;
  if (strcmp(s, "Offer") == 0)     return MSG_OFFER;
  if (strcmp(s, "Need")  == 0)     return MSG_NEED;
  if (strcmp(s, "Event") == 0)     return MSG_EVENT;
  if (strcmp(s, "Poll")  == 0)     return MSG_POLL;
  return MSG_NOTICE;
}

uint8_t stringToMsgType(const String& s) { return stringToMsgType(s.c_str()); }

// ===================== TOKEN HELPERS (binary) =====================
// Per-message owner/claim tokens are 64 bits stored as 8 raw bytes in RAM
// (vs the old 16-char hex String, which carried ~33 bytes of overhead each).
// Converted to/from hex only at the wire boundary so the on-disk JSON and the
// browser API stay unchanged.
#define TOKEN_BYTES 8
#define TOKEN_HEX_CHARS (TOKEN_BYTES * 2)

void generateBinaryToken(uint8_t* out) {
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  out[0] = (a >> 24) & 0xFF; out[1] = (a >> 16) & 0xFF;
  out[2] = (a >>  8) & 0xFF; out[3] =  a        & 0xFF;
  out[4] = (b >> 24) & 0xFF; out[5] = (b >> 16) & 0xFF;
  out[6] = (b >>  8) & 0xFF; out[7] =  b        & 0xFF;
}

String tokenToHex(const uint8_t* in) {
  static const char hex[] = "0123456789abcdef";
  char buf[TOKEN_HEX_CHARS + 1];
  for (size_t i = 0; i < TOKEN_BYTES; i++) {
    buf[2 * i]     = hex[(in[i] >> 4) & 0xF];
    buf[2 * i + 1] = hex[ in[i]       & 0xF];
  }
  buf[TOKEN_HEX_CHARS] = '\0';
  return String(buf);
}

// Returns true on success, fills `out` with 8 bytes. Returns false (and zeroes
// `out`) on any malformed input, including null, wrong length, or non-hex
// characters. Tolerant of mixed case.
bool hexToToken(const char* hex, uint8_t* out) {
  memset(out, 0, TOKEN_BYTES);
  if (!hex) return false;
  if (strlen(hex) != TOKEN_HEX_CHARS) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (size_t i = 0; i < TOKEN_BYTES; i++) {
    int hi = nibble(hex[2 * i]);
    int lo = nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) { memset(out, 0, TOKEN_BYTES); return false; }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

bool tokenIsZero(const uint8_t* t) {
  for (size_t i = 0; i < TOKEN_BYTES; i++) if (t[i]) return false;
  return true;
}

bool tokenEqualsHex(const uint8_t* t, const String& hex) {
  if (hex.length() != TOKEN_HEX_CHARS) return false;
  uint8_t cmp[TOKEN_BYTES];
  if (!hexToToken(hex.c_str(), cmp)) return false;
  return memcmp(t, cmp, TOKEN_BYTES) == 0;
}

// ===================== MESSAGES =====================
struct Message {
  uint16_t      id;
  uint8_t       type;            // MsgType enum, was String
  uint8_t       authorColor;     // palette index, 0 = default ink
  unsigned long expires;

  String        author;
  String        text;            // body for messages, question for polls

  // Ownership: minted on post, returned once to the creator's browser.
  // Required for edit/delete. Never sent in /messages. Zero = unowned.
  uint8_t       ownerToken[TOKEN_BYTES];

  // Claim state (Offer/Need only):
  bool          claimed;
  String        claimedBy;
  uint8_t       claimToken[TOKEN_BYTES];  // valid only when `claimed`

  // Reactions: counters for [thanks, me_too, nice, noted]
  uint16_t      reactions[4];

  // Poll fields live in a separate sparse pool (see PollData below). The vast
  // majority of messages aren't polls, so embedding 4 Strings and an array
  // per-message wasted ~14 KB across MAX_MSGS=200.
};

Message msgs[Config::MAX_MSGS];
int msgCount = 0;
uint16_t nextMsgId = 1;
unsigned long lastPostTime = 0;

bool msgsDirty = false;
unsigned long lastMsgDirtyTime = 0;

// ===================== POLL POOL (sparse) =====================
// Only allocated for messages that are actually polls. msgId==0 means the
// slot is free. Worst case: every message is a poll up to MAX_POLLS, after
// which new poll posts fail. 30 simultaneous polls is plenty for a
// neighborhood board.
#define MAX_POLLS 30

struct PollData {
  uint16_t msgId;      // 0 = free slot
  uint8_t  optCount;   // 0..4
  String   opts[4];
  uint16_t votes[4];
};

PollData polls[MAX_POLLS];

// Helpers return int indices (or -1) rather than PollData*, because the
// Arduino IDE's prototype-injecting preprocessor puts auto-generated prototypes
// above any user-defined struct, so a function whose return type or parameter
// list mentions PollData would fail with "PollData does not name a type".
// Same trick `addMessage` uses to avoid returning `Message`.

// Returns index in polls[], or -1 if not found.
int findPollIdx(uint16_t msgId) {
  if (msgId == 0) return -1;
  for (int i = 0; i < MAX_POLLS; i++) {
    if (polls[i].msgId == msgId) return i;
  }
  return -1;
}

// Reserves a free slot for msgId. Returns slot index, or -1 if pool full.
int allocPollIdx(uint16_t msgId) {
  for (int i = 0; i < MAX_POLLS; i++) {
    if (polls[i].msgId == 0) {
      polls[i].msgId    = msgId;
      polls[i].optCount = 0;
      for (uint8_t k = 0; k < 4; k++) {
        polls[i].opts[k]  = "";
        polls[i].votes[k] = 0;
      }
      return i;
    }
  }
  return -1;
}

void freePoll(uint16_t msgId) {
  int idx = findPollIdx(msgId);
  if (idx < 0) return;
  polls[idx].msgId    = 0;
  polls[idx].optCount = 0;
  for (uint8_t k = 0; k < 4; k++) {
    polls[idx].opts[k]  = "";
    polls[idx].votes[k] = 0;
  }
}

// Stream save: write each message as a small JSON doc directly to the file,
// rather than building one giant 100KB+ doc in RAM. Friendly to ESP32 heap.
//
// On-disk format is unchanged from before the struct refactor: type stays as a
// human-readable string, tokens stay as 16-char hex. Old /msgs.json files load
// cleanly; new files load cleanly on old firmware too.
void saveMessages() {
  File tmp = LittleFS.open("/msgs.tmp", FILE_WRITE);
  if (!tmp) return;
  tmp.print("[");
  for (int i = 0; i < msgCount; i++) {
    if (i > 0) tmp.print(",");
    DynamicJsonDocument o(2048);
    o["id"]         = msgs[i].id;
    o["author"]     = msgs[i].author;
    o["type"]       = msgTypeToString(msgs[i].type);
    o["text"]       = msgs[i].text;
    o["expires"]    = msgs[i].expires;
    if (!tokenIsZero(msgs[i].ownerToken)) {
      o["ownerToken"] = tokenToHex(msgs[i].ownerToken);
    }
    if (msgs[i].claimed) {
      o["claimed"]    = true;
      o["claimedBy"]  = msgs[i].claimedBy;
      o["claimToken"] = tokenToHex(msgs[i].claimToken);
    }
    if (msgs[i].type == MSG_POLL) {
      int pidx = findPollIdx(msgs[i].id);
      if (pidx >= 0 && polls[pidx].optCount > 0) {
        JsonArray opts  = o.createNestedArray("options");
        JsonArray votes = o.createNestedArray("votes");
        for (uint8_t k = 0; k < polls[pidx].optCount; k++) {
          opts.add(polls[pidx].opts[k]);
          votes.add(polls[pidx].votes[k]);
        }
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
    Message& m = msgs[msgCount];
    m.id          = o["id"] | nextMsgId;
    m.author      = (const char*)(o["author"] | "");
    m.type        = stringToMsgType((const char*)(o["type"] | "Notice"));
    m.text        = (const char*)(o["text"] | "");
    m.expires     = o["expires"];
    hexToToken(o["ownerToken"] | "", m.ownerToken);  // zeros on missing/invalid
    m.claimed     = o["claimed"] | false;
    m.claimedBy   = (const char*)(o["claimedBy"] | "");
    hexToToken(o["claimToken"] | "", m.claimToken);
    for (uint8_t k = 0; k < 4; k++) m.reactions[k] = 0;
    JsonArray rxn = o["reactions"];
    if (!rxn.isNull()) {
      for (uint8_t k = 0; k < 4 && k < rxn.size(); k++) {
        m.reactions[k] = (uint16_t)(rxn[k] | 0);
      }
    }
    m.authorColor = (uint8_t)(o["authorColor"] | 0);

    // Polls go into the sparse pool keyed by message id. If the pool is full,
    // the message survives but loses its options; it'll behave like a Notice.
    JsonArray opts  = o["options"];
    JsonArray votes = o["votes"];
    if (!opts.isNull() && m.type == MSG_POLL) {
      int pidx = allocPollIdx(m.id);
      if (pidx >= 0) {
        uint8_t k = 0;
        for (JsonVariant v : opts) {
          if (k >= 4) break;
          polls[pidx].opts[k]  = v.as<String>();
          polls[pidx].votes[k] = votes.isNull() ? 0 : (uint16_t)(votes[k] | 0);
          k++;
        }
        polls[pidx].optCount = k;
      }
    }

    if (m.id >= nextMsgId) nextMsgId = m.id + 1;
    msgCount++;
  }
  f.close();
}

// addMessage uses output references rather than returning a struct, so the
// Arduino IDE's prototype-injecting preprocessor doesn't trip over an unknown
// user-defined return type (it injects prototypes above struct definitions).
// Returns true on success, false if the board is genuinely full (or the poll
// pool is exhausted for a Poll post).
// On success, outId and outToken receive the new post's id and owner token (hex).
bool addMessage(String author, String type, String text, int expiryHours,
                uint8_t pollCount, String* pollOpts, uint8_t authorColor,
                uint16_t& outId, String& outToken) {
  outId = 0;
  outToken = "";

  uint8_t typeEnum = stringToMsgType(type);

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
    // If we're evicting a poll, return its slot to the pool.
    if (msgs[evict].type == MSG_POLL) freePoll(msgs[evict].id);
    clearPinIfMatches(msgs[evict].id);
    for (int i = evict; i < msgCount - 1; i++) msgs[i] = msgs[i + 1];
    msgCount--;
  }

  int i = msgCount;
  uint16_t newId = nextMsgId++;

  // For polls, claim a slot in the sparse pool first. If the pool is full,
  // fail cleanly without partially constructing the message.
  if (typeEnum == MSG_POLL) {
    int pidx = allocPollIdx(newId);
    if (pidx < 0) {
      nextMsgId--;  // give the id back since we're not using it
      return false;
    }
    for (uint8_t k = 0; k < pollCount && k < 4; k++) {
      polls[pidx].opts[k]  = pollOpts[k];
      polls[pidx].votes[k] = 0;
    }
    polls[pidx].optCount = (pollCount > 4) ? 4 : pollCount;
  }

  msgs[i].id           = newId;
  msgs[i].author       = author;
  msgs[i].type         = typeEnum;
  msgs[i].text         = text;
  msgs[i].expires      = nowSecs() + (unsigned long)expiryHours * 3600UL;
  generateBinaryToken(msgs[i].ownerToken);
  msgs[i].claimed      = false;
  msgs[i].claimedBy    = "";
  memset(msgs[i].claimToken, 0, TOKEN_BYTES);
  for (uint8_t k = 0; k < 4; k++) msgs[i].reactions[k] = 0;
  msgs[i].authorColor  = authorColor;

  outId    = msgs[i].id;
  outToken = tokenToHex(msgs[i].ownerToken);

  msgCount++;
  lastPostTime = nowSecs();
  stats_posts_total++;
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

// CORS headers, queued before each response. Sent on every API endpoint so
// the page works regardless of which origin the browser thinks loaded it,
// which matters because:
//   - iOS captive-portal flow can land the page at an Apple probe URL while
//     fetches resolve to fountainhead.local, crossing origins
//   - iOS Safari treats .local hostnames as a privacy boundary in some cases
//   - The threat model on an AP-only board is "neighbor on the network," not
//     cross-origin attackers, so * is fine
// Expose-Headers is needed so the wall page can read X-Server-Now under CORS.
void addCors() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Expose-Headers", "X-Server-Now");
}

// Some browsers (or iOS in certain modes) send an OPTIONS preflight before the
// real request. We just need to respond 204 with the CORS headers set.
void handleOptions() {
  addCors();
  server.send(204);
}

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

// The main board and wall pages are served pre-gzipped from
// Community_Hub_pages_gz.h. Browsers decompress transparently via the
// Content-Encoding: gzip header. Cuts the main page from ~41 KB to ~10 KB.
//
// The admin page is built dynamically (HEAD + injected JS + TAIL) and stays
// uncompressed. It's only loaded by the host occasionally; not worth the
// extra plumbing to gzip a dynamic response.
void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8",
                (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}
void handleAdmin() { server.send(200, "text/html; charset=utf-8", buildAdminPage()); }
void handleWall() {
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8",
                (PGM_P)WALL_HTML_GZ, WALL_HTML_GZ_LEN);
}

void handleAdminAuth() {
  addCors();
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
  addCors();
  DynamicJsonDocument doc(512);
  doc["name"]     = id_name;
  doc["icon"]     = id_icon;
  doc["tagline"]  = id_tagline;
  doc["rules"]    = id_rules;
  doc["footer"]   = id_footer;
  doc["hostname"] = effectiveHostname();
  doc["uptime"]   = formatUptime();
  doc["pinned"]   = pinnedMsgId;   // 0 = nothing pinned
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Public messages feed. NEVER includes ownerToken or claimToken.
//
// Streams the response chunk-by-chunk via sendContent() so we never hold the
// full payload in RAM. Each message is built in its own ~1 KB JsonDocument and
// flushed; peak transient RAM stays around 1-2 KB regardless of board size.
// Replaces the old approach of a single 20 KB document.
void handleMessages() {
  addCors();
  unsigned long now = nowSecs();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");  // headers only

  String chunk;
  chunk.reserve(1536);
  chunk = "[";
  bool first = true;

  for (int i = 0; i < msgCount; i++) {
    // Pinned posts are exempt from the normal expiry filter; they live until
    // explicitly unpinned, deleted, or evicted.
    if (msgs[i].expires < now && msgs[i].id != pinnedMsgId) continue;

    DynamicJsonDocument o(1024);
    o["id"]      = msgs[i].id;
    o["author"]  = msgs[i].author;
    o["type"]    = msgTypeToString(msgs[i].type);
    o["text"]    = msgs[i].text;
    o["expires"] = msgs[i].expires;
    if (msgs[i].authorColor > 0) o["authorColor"] = msgs[i].authorColor;
    if (msgs[i].claimed) {
      o["claimed"]   = true;
      o["claimedBy"] = msgs[i].claimedBy;
    }
    if (msgs[i].type == MSG_POLL) {
      int pidx = findPollIdx(msgs[i].id);
      if (pidx >= 0 && polls[pidx].optCount > 0) {
        JsonArray opts  = o.createNestedArray("options");
        JsonArray votes = o.createNestedArray("votes");
        for (uint8_t k = 0; k < polls[pidx].optCount; k++) {
          opts.add(polls[pidx].opts[k]);
          votes.add(polls[pidx].votes[k]);
        }
      }
    }
    // Always emit reactions array so client doesn't have to check (4 small ints).
    JsonArray rxn = o.createNestedArray("reactions");
    for (uint8_t k = 0; k < 4; k++) rxn.add(msgs[i].reactions[k]);

    String body;
    serializeJson(o, body);
    if (!first) chunk += ",";
    chunk += body;
    first = false;

    // Flush every ~1 KB so transient memory stays small.
    if (chunk.length() > 1024) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += "]";
  server.sendContent(chunk);
  server.sendContent("");  // empty chunk signals end of response
}

void handlePost() {
  addCors();
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
  addCors();
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id    = doc["id"] | 0;
  String   token = doc["token"] | "";
  String   text  = sanitize(doc["text"] | "", 300);
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (tokenIsZero(msgs[idx].ownerToken) ||
      !tokenEqualsHex(msgs[idx].ownerToken, token)) {
    server.send(403, "text/plain", "forbidden"); return;
  }
  if (text.isEmpty())                                   { server.send(400, "text/plain", "empty"); return; }
  msgs[idx].text = text;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePostDelete() {
  addCors();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id    = doc["id"] | 0;
  String   token = doc["token"] | "";
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (tokenIsZero(msgs[idx].ownerToken) ||
      !tokenEqualsHex(msgs[idx].ownerToken, token)) {
    server.send(403, "text/plain", "forbidden"); return;
  }
  if (msgs[idx].type == MSG_POLL) freePoll(msgs[idx].id);
  clearPinIfMatches(msgs[idx].id);
  for (int j = idx; j < msgCount - 1; j++) msgs[j] = msgs[j + 1];
  msgCount--;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePostClaim() {
  addCors();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id   = doc["id"] | 0;
  String   name = sanitize(doc["name"] | "neighbor", 24);
  if (name.isEmpty()) name = "neighbor";
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (msgs[idx].type != MSG_OFFER && msgs[idx].type != MSG_NEED) {
    server.send(400, "text/plain", "not claimable"); return;
  }
  if (msgs[idx].claimed)                                { server.send(409, "text/plain", "already claimed"); return; }

  msgs[idx].claimed   = true;
  msgs[idx].claimedBy = name;
  generateBinaryToken(msgs[idx].claimToken);
  stats_claims_total++;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }

  DynamicJsonDocument resp(256);
  resp["token"] = tokenToHex(msgs[idx].claimToken);
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handlePostUnclaim() {
  addCors();
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
  bool ok = false;
  if (token.length() == TOKEN_HEX_CHARS) {
    if (tokenEqualsHex(msgs[idx].claimToken, token)) ok = true;
    else if (!tokenIsZero(msgs[idx].ownerToken) &&
             tokenEqualsHex(msgs[idx].ownerToken, token)) ok = true;
  }
  if (!ok) { server.send(403, "text/plain", "forbidden"); return; }
  msgs[idx].claimed   = false;
  msgs[idx].claimedBy = "";
  memset(msgs[idx].claimToken, 0, TOKEN_BYTES);
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

void handlePollVote() {
  addCors();
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json"); return;
  }
  uint16_t id     = doc["id"] | 0;
  int      option = doc["option"] | -1;
  int idx = findMessageIdx(id);
  if (idx < 0)                                          { server.send(404, "text/plain", "not found"); return; }
  if (msgs[idx].type != MSG_POLL)                       { server.send(400, "text/plain", "not a poll"); return; }
  int pidx = findPollIdx(id);
  if (pidx < 0)                                         { server.send(404, "text/plain", "poll data missing"); return; }
  if (option < 0 || option >= polls[pidx].optCount)     { server.send(400, "text/plain", "bad option"); return; }
  // Cap at uint16 max just in case the poll goes viral in the neighborhood.
  if (polls[pidx].votes[option] < 0xFFFF) polls[pidx].votes[option]++;
  stats_votes_total++;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "ok");
}

// Reaction handler. Bumps one of four counters per post.
// Client-side localStorage prevents the same browser from double-counting;
// not bulletproof (clear-and-react again works), but the trust model here
// matches the rest of the board.
void handlePostReact() {
  addCors();
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
  stats_reactions_total++;
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
  addCors();
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
  stats_waves_total++;

  DynamicJsonDocument resp(64);
  resp["id"] = waves[waveCount - 1].id;
  String out;
  serializeJson(resp, out);
  server.send(200, "application/json", out);
}

void handleWaveRecent() {
  addCors();
  // Safari 18+ has a keep-alive reuse bug that surfaces as "Fetch API cannot
  // load ... due to access control checks" when a connection is reused after
  // the server (in our case the ESP32) has already closed it. The wave poll
  // runs every 4 seconds, well inside the keep-alive window where the race
  // happens. Forcing Connection: close means each poll opens a fresh TCP
  // connection, sidestepping the bug entirely.
  // https://discussions.apple.com/thread/256112607
  server.sendHeader("Connection", "close");
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
//
// Underflow guard: if the admin sets the board's clock AFTER strokes were
// drawn (createdEpoch is "seconds since boot", then nowSecs() jumps to a real
// Unix epoch), createdEpoch > now and the unsigned subtraction wraps to
// billions of seconds. Without the guard, every stroke gets pruned. We treat
// strokes from "the future" as fresh (age=0) instead.
void pruneStrokes() {
  if (!strokes) return;
  unsigned long now = nowSecs();
  int keep = 0;
  for (int i = 0; i < strokeCount; i++) {
    unsigned long age = (now > strokes[i].createdEpoch)
                          ? (now - strokes[i].createdEpoch)
                          : 0;
    if (age <= WALL_LIFETIME_SECS) {
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
// Emits X-Server-Now (board's current nowSecs()) so the browser can compute
// stroke ages against server time, not its own clock. Otherwise, on a board
// where the admin hasn't set the time, fresh strokes look ~54 years old to
// the browser and render at opacity 0 ("blank wall" bug).
//
// Note: server.send(200, type, "") with an empty body finalizes the response;
// subsequent writes are ignored. We must call send() with the headers, then
// use sendContent() repeatedly to stream the body.
void handleWallGet() {
  addCors();
  pruneStrokes();

  String chunk;
  chunk.reserve(512);

  server.sendHeader("X-Server-Now", String(nowSecs()));
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
  addCors();
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
  stats_strokes_total++;

  if (!wallDirty) { wallDirty = true; lastWallDirtyTime = millis(); }

  // Return the server's time for this stroke so the optimistic client-side
  // entry uses the same reference frame as future /wall/data fetches. Without
  // this, the optimistic entry would carry a client-Date.now() timestamp that
  // disagrees with the server's, and the stroke would fade to opacity 0 on the
  // next renderAll cycle.
  DynamicJsonDocument resp(96);
  resp["id"] = s.id;
  resp["t"]  = s.createdEpoch;
  String out;
  serializeJson(resp, out);
  server.sendHeader("X-Server-Now", String(nowSecs()));
  server.send(200, "application/json", out);
}

// GET /wall/info — small endpoint for the "wall has N new strokes" indicator
// on the main board header. Returns latest stroke id and total stroke count.
void handleWallInfo() {
  addCors();
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
  server.sendHeader("X-Server-Now", String(nowSecs()));
  server.send(200, "application/json", out);
}

// Admin-only: wipe the wall
void handleAdminWallClear() {
  addCors();
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
  addCors();
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
    if (msgs[i].type == MSG_POLL) pollCount++;
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
  addCors();
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
  addCors();
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
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (!setTimeFromString(server.arg("time"))) {
    server.send(400, "text/plain", "bad format — use DDMMYYYY-HHMM");
    return;
  }
  saveTime();
  server.send(200, "text/plain", "time set");
}

void handleAdminLedGet() {
  addCors();
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
  addCors();
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
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (msgsDirty) { saveMessages(); msgsDirty = false; }
  File f = LittleFS.open(Config::STORAGE_FILE);
  if (!f) { server.send(500, "text/plain", "no file"); return; }
  String out = f.readString();
  f.close();
  server.send(200, "application/json", out);
}

void handleAdminRestore() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  DynamicJsonDocument doc(MSG_LOAD_DOC_SIZE);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  // Wipe the poll pool too, since we're replacing the entire message set.
  for (int i = 0; i < MAX_POLLS; i++) freePoll(polls[i].msgId);
  // The pinned message id may not exist in the new set; clear it. Admin can
  // re-pin from the restored posts if needed.
  pinnedMsgId = 0;
  savePinned();
  msgCount = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (msgCount >= Config::MAX_MSGS) break;
    Message& m = msgs[msgCount];
    m.id          = o["id"] | nextMsgId;
    m.author      = (const char*)(o["author"] | "");
    m.type        = stringToMsgType((const char*)(o["type"] | "Notice"));
    m.text        = (const char*)(o["text"] | "");
    m.expires     = o["expires"];
    hexToToken(o["ownerToken"] | "", m.ownerToken);
    m.claimed     = o["claimed"] | false;
    m.claimedBy   = (const char*)(o["claimedBy"] | "");
    hexToToken(o["claimToken"] | "", m.claimToken);
    for (uint8_t k = 0; k < 4; k++) m.reactions[k] = 0;
    JsonArray rxn = o["reactions"];
    if (!rxn.isNull()) {
      for (uint8_t k = 0; k < 4 && k < rxn.size(); k++) {
        m.reactions[k] = (uint16_t)(rxn[k] | 0);
      }
    }
    m.authorColor = (uint8_t)(o["authorColor"] | 0);

    JsonArray opts  = o["options"];
    JsonArray votes = o["votes"];
    if (!opts.isNull() && m.type == MSG_POLL) {
      int pidx = allocPollIdx(m.id);
      if (pidx >= 0) {
        uint8_t k = 0;
        for (JsonVariant v : opts) {
          if (k >= 4) break;
          polls[pidx].opts[k]  = v.as<String>();
          polls[pidx].votes[k] = votes.isNull() ? 0 : (uint16_t)(votes[k] | 0);
          k++;
        }
        polls[pidx].optCount = k;
      }
    }

    if (m.id >= nextMsgId) nextMsgId = m.id + 1;
    msgCount++;
  }
  saveMessages();
  server.send(200, "text/plain", "restored " + String(msgCount) + " messages");
}

void handleAdminSetKey() {
  addCors();
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
  addCors();
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
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  saveMessages();
  msgsDirty = false;
  saveWall();
  wallDirty = false;
  saveTime();
  server.send(200, "text/plain", "flushed");
}

void handleAdminDeletePost() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (!server.hasArg("id")) { server.send(400, "text/plain", "missing id"); return; }
  uint16_t targetId = (uint16_t)server.arg("id").toInt();
  int idx = findMessageIdx(targetId);
  if (idx < 0) { server.send(404, "text/plain", "not found"); return; }
  if (msgs[idx].type == MSG_POLL) freePoll(msgs[idx].id);
  clearPinIfMatches(msgs[idx].id);
  for (int j = idx; j < msgCount - 1; j++) msgs[j] = msgs[j + 1];
  msgCount--;
  if (!msgsDirty) { msgsDirty = true; lastMsgDirtyTime = millis(); }
  server.send(200, "text/plain", "deleted");
}

// ── Pinned post ──────────────────────────────────────────────────────────────
void handleAdminPin() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  if (!server.hasArg("id")) { server.send(400, "text/plain", "missing id"); return; }
  uint16_t target = (uint16_t)server.arg("id").toInt();
  if (target == 0)          { server.send(400, "text/plain", "bad id"); return; }
  if (findMessageIdx(target) < 0) {
    server.send(404, "text/plain", "no such post");
    return;
  }
  pinnedMsgId = target;
  savePinned();
  server.send(200, "text/plain", "pinned");
}

void handleAdminUnpin() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  pinnedMsgId = 0;
  savePinned();
  server.send(200, "text/plain", "unpinned");
}

// ── Stats endpoint ───────────────────────────────────────────────────────────
// Superset of /api/health, with the in-RAM counters and a type breakdown.
// Auth-gated so cumulative activity isn't visible to general clients.
void handleAdminStats() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  DynamicJsonDocument doc(1024);

  // Counters since boot
  doc["posts_total"]     = stats_posts_total;
  doc["reactions_total"] = stats_reactions_total;
  doc["claims_total"]    = stats_claims_total;
  doc["votes_total"]     = stats_votes_total;
  doc["waves_total"]     = stats_waves_total;
  doc["strokes_total"]   = stats_strokes_total;

  // Current state
  doc["msg_count"]   = msgCount;
  doc["max_msgs"]    = Config::MAX_MSGS;
  doc["stroke_count"] = strokeCount;
  doc["max_strokes"] = MAX_STROKES;
  doc["pinned"]      = pinnedMsgId;

  // Type breakdown (current, not cumulative)
  unsigned long now = nowSecs();
  int byType[5] = {0, 0, 0, 0, 0};
  int claimedCount = 0, expiredCount = 0;
  for (int i = 0; i < msgCount; i++) {
    if (msgs[i].type < 5) byType[msgs[i].type]++;
    if (msgs[i].claimed)  claimedCount++;
    if (msgs[i].expires < now && msgs[i].id != pinnedMsgId) expiredCount++;
  }
  JsonObject t = doc.createNestedObject("by_type");
  t["notice"] = byType[MSG_NOTICE];
  t["offer"]  = byType[MSG_OFFER];
  t["need"]   = byType[MSG_NEED];
  t["event"]  = byType[MSG_EVENT];
  t["poll"]   = byType[MSG_POLL];
  doc["claimed"] = claimedCount;
  doc["expired"] = expiredCount;

  // System
  doc["free_heap"]     = ESP.getFreeHeap();
  doc["min_free_heap"] = ESP.getMinFreeHeap();
  doc["heap_size"]     = ESP.getHeapSize();
  doc["fs_used"]       = LittleFS.usedBytes();
  doc["fs_total"]      = LittleFS.totalBytes();
  doc["wifi_clients"]  = WiFi.softAPgetStationNum();
  doc["uptime_secs"]   = (millis() - bootMillis) / 1000;
  doc["uptime_str"]    = formatUptime();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAdminClear() {
  addCors();
  if (!checkKey()) { server.send(403, "text/plain", "forbidden"); return; }
  for (int i = 0; i < MAX_POLLS; i++) freePoll(polls[i].msgId);
  pinnedMsgId = 0;
  savePinned();
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
    loadPinned();
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
    addCors();
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
  server.on("/admin/pin",           handleAdminPin);
  server.on("/admin/unpin",         handleAdminUnpin);
  server.on("/admin/stats",         handleAdminStats);

  // CORS preflight catch-all. The browser sends OPTIONS before any
  // cross-origin request with a non-simple Content-Type or custom header. We
  // respond 204 + headers via addCors() so the actual request can proceed.
  // Registered for the AJAX endpoints; HTML page paths fall through to
  // onNotFound (which redirects to /, harmless for OPTIONS).
  for (const char* p : {
    "/info", "/messages", "/post", "/post/edit", "/post/delete",
    "/post/claim", "/post/unclaim", "/post/react", "/poll/vote",
    "/wave", "/wave/recent", "/wall/data", "/wall/stroke", "/wall/info",
    "/api/status", "/api/health", "/admin/auth",
    "/admin/identity/get", "/admin/identity/set", "/admin/time",
    "/admin/led/get", "/admin/led/set", "/admin/backup", "/admin/restore",
    "/admin/setkey", "/admin/flush", "/admin/clear", "/admin/wall/clear",
    "/admin/delete/post", "/admin/pin", "/admin/unpin", "/admin/stats",
  }) {
    server.on(p, HTTP_OPTIONS, handleOptions);
  }

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
