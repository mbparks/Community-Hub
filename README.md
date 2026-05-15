# Community Hub, Changes Since Initial Sketch

This document covers everything added, changed, or restructured between the
original `Community_Hub.ino` and the current state of the sketch. It does not
re-document features that were present in the original.

## Sketch Layout Change

The original sketch was a single `.ino` file. The current build is split into
two files that both live in the `Community_Hub/` sketch folder:

```
Community_Hub/
    Community_Hub.ino         (~1270 lines, all C++ and runtime logic)
    Community_Hub_pages.h     (~1480 lines, all HTML / CSS / JS content)
```

The split exists because the Arduino IDE preprocessor scans `.ino` files with a
ctags-based parser to auto-generate function prototypes, and that scanner
desyncs on large raw string literals containing JavaScript. Once it loses sync,
it starts treating content inside `R"rawliteral(...)rawliteral"` as if it were
C++ code, producing errors like `'function' does not name a type`. Headers are
fed straight to the C++ compiler, which handles raw strings per the standard.

The header file holds three PROGMEM string constants:

- `INDEX_HTML`, the public bulletin board page
- `ADMIN_PAGE_HEAD`, the admin page up through the start of the runtime script
- `ADMIN_PAGE_TAIL`, the admin page from the end of the injected JS through
  the closing tags

The `buildAdminPage()` function in the `.ino` assembles the admin page by
concatenating `FPSTR(ADMIN_PAGE_HEAD)`, a few lines of JS that inject runtime
values like the current identity fields, and `FPSTR(ADMIN_PAGE_TAIL)`. The
main board page is served directly from `INDEX_HTML` via
`server.send_P(...)`.

If you add new raw-string content in the future, put it in the header file
from the start.

## Bug Fix, SSID Not Broadcasting

The original sketch set `AP_PASS = "sun123"`, which is six characters. WPA2
requires the passphrase to be either empty (for an open network) or at least
eight characters. The Arduino wrapper around `WiFi.softAP()` silently rejects
invalid passwords, with the result that the configured SSID never actually
gets broadcast.

The current default is `AP_PASS = "sunbury123"` (ten characters), and
`setup()` now captures the return value of `WiFi.softAP()` and logs a warning
if it returns false, so this failure mode is visible in the serial console.

## New Features

### Per-Post Ownership Tokens

When someone creates a post, the server mints a 16-character hex token
(`generateShortToken()`, 64 bits from `esp_random()`), stores it as
`Message.ownerToken`, and returns it once to the creating browser. The
browser stashes it in `localStorage` under `cn_owned`. The token never
appears in `/messages` or any other public response.

Subsequent requests to `/post/edit` and `/post/delete` accept the token as
proof of authorship. Lose the localStorage entry, lose the ability to edit
or delete that post. Anyone with access to that browser can also edit or
delete the post. For a neighborhood board this is the right trust level.

### Claim and Unclaim on Offer and Need

`Offer` and `Need` posts can be marked claimed by anyone (no authorship
required). The claim generates a separate 16-char `claimToken` returned only
to the claimer, who stores it under `cn_claimed`. Either the claim token or
the original owner token can unclaim. The post displays a "Claimed by NAME"
banner in the matching category color.

`Notice`, `Event`, and `Poll` posts are not claimable, the server returns
400 for those.

### Polls as a Fifth Category

`Poll` is a new post type with the following data:

- The post text is the question
- 2 to 4 options, sanitized to 60 characters each
- A `uint16_t` vote counter per option, capped at 65535

Voting is unauthenticated but tracked client-side in `cn_voted` to prevent a
single browser from voting twice. Clearing localStorage allows revoting, the
same leaky-but-fine pattern used elsewhere.

The poll category uses a muted purple palette (`--c-poll-*` CSS variables).
The card renders option buttons before you vote, then horizontal bars with
percentages and a checkmark next to your choice after you vote.

### Reactions on Every Post

Four reaction counters per message, persisted as `uint16_t reactions[4]`:

- 👍 thanks
- 🙋 me too
- 🌻 nice
- 👀 noted

The indices are persisted server-side, so the order in the `REACTIONS`
array in `Community_Hub_pages.h` must not be shuffled without a data
migration. Reactions are stored in `cn_reacted` localStorage as
`{ postId: [reactionIndex, ...] }` to prevent the same browser from
double-counting. The UI optimistically increments the count on tap, then
reconciles to the server's authoritative count on the next `load()` cycle.

### Wave Broadcasts

A "wave 👋" button in the header sends a transient hello to every
currently-connected client. Implementation uses a 20-entry ring buffer in
RAM (`waves[]`), not WebSockets or SSE (the built-in `WebServer` does not
support those cleanly).

Each wave has a monotonic `id` and a creation `millis()` timestamp. Clients
poll `GET /wave/recent?since=<lastSeenId>` every four seconds (only when the
tab is visible, gated by `document.hidden`). New waves are animated as a
floating emoji that drifts upward and fades over 3.5 seconds. Waves older
than 10 seconds are pruned server-side so latecomers do not see stale
activity.

The button disables itself for 2.5 seconds after each press as a soft
client-side rate limit. There is no server-side rate limit.

### Personal Name Color

A small color picker under the name field. Eight choices (default ink plus
seven muted palette colors), stored in `cn_color` as a `uint8_t` index 0
through 7. The color is sent with each post as `authorColor` and persisted
per-message. Past posts keep whatever color the author had at the time of
posting.

The palette in `NAME_COLORS` in `Community_Hub_pages.h` must stay aligned
with the server, since indices are stored as raw bytes. Adding new colors
is safe, reordering existing ones is not.

### Live Neighbors Indicator

A "🟢 N neighbors here" pill in the header, sourced from
`WiFi.softAPgetStationNum()` via the existing `/api/health` endpoint.
Refreshes every 15 seconds. Renders "🟢 just you here" when N is 1.

### Time-Aware Greeting and Background Tint

The site tagline rotates by browser local hour:

- 06:00 to 10:59, "Good morning, neighbors"
- 11:00 to 16:59, "Afternoon at the Hub"
- 17:00 to 20:59, "Evening at the Hub"
- All other hours, "Quiet night at the Hub"

The `<body>` element gets a `data-tint` attribute matching the period
(`morning`, `afternoon`, `evening`, `night`), and CSS applies a subtle
background-color shift with a 1.5-second transition. Implementation is
purely client-side and uses the visitor's device clock, so it works even
when the board's own time has not been set.

The admin's static `tagline` identity field is still stored and editable
in the admin panel, but the main board ignores it in favor of the
time-aware greeting. Restore the original behavior by replacing
`applyGreeting()` in `loadInfo()` with the previous tagline assignment.

### /api/health Endpoint

Public, unauthenticated. Returns a JSON object useful for external
monitoring, an e-paper companion device, or troubleshooting memory pressure
after the schema additions. Fields:

- `free_heap`, `min_free_heap`, `heap_size`, current and watermark heap
- `msg_count`, `max_msgs`, current message count and ceiling
- `claimed_count`, `poll_count`, `expired_count`, derived counts
- `uptime_secs`, `uptime_str`, both forms
- `fs_used`, `fs_total`, LittleFS usage
- `wifi_clients`, number of devices connected to the AP
- `last_post_secs_ago`, omitted if no posts since boot
- `msgs_dirty`, `time_set`, internal state flags

To gate this endpoint behind admin auth, add `if (!checkKey()) { ... }` as
the first line of `handleHealth()` and call it with `?token=...`.

## Data Model Additions

`struct Message` gained the following fields beyond the original five:

```cpp
String   ownerToken;       // 16 hex chars, never sent in /messages
bool     claimed;
String   claimedBy;
String   claimToken;       // 16 hex chars, returned only to the claimer
uint8_t  pollOptCount;     // 0 for non-polls
String   pollOpts[4];
uint16_t pollVotes[4];
uint16_t reactions[4];     // thanks, me too, nice, noted
uint8_t  authorColor;      // palette index, 0 = default ink
```

Total RAM overhead per message: about 100 bytes for empty Strings plus the
fixed-size members, so roughly 20 KB for the worst case of 200 messages.
Free heap on ESP32 WROOM after WiFi AP startup typically sits around 150
to 180 KB, so this is comfortable. Track `min_free_heap` from `/api/health`
in the field if the board sees heavy use.

A new wave subsystem lives alongside messages:

```cpp
struct WaveEntry { uint32_t id; unsigned long createdMs; String icon; String from; };
WaveEntry waves[20];
```

About 1 KB of additional RAM for the ring buffer.

## API Additions

All POST endpoints expect a JSON body unless noted.

```
POST /post/edit
    Body:  { id, token, text }
    Auth:  owner token must match
    200:   "ok"

POST /post/delete
    Body:  { id, token }
    Auth:  owner token must match
    200:   "ok"

POST /post/claim
    Body:  { id, name }
    200:   { token }   (the claim token, store client-side)

POST /post/unclaim
    Body:  { id, token }
    Auth:  claim token or owner token
    200:   "ok"

POST /post/react
    Body:  { id, type }     (type is 0..3)
    200:   "ok"

POST /poll/vote
    Body:  { id, option }   (option is 0..pollOptCount-1)
    200:   "ok"

POST /wave
    Body:  { icon, from }
    200:   { id }

GET  /wave/recent?since=N
    200:   [ { id, icon, from }, ... ]

GET  /api/health
    200:   (see fields above)
```

The existing `POST /post` endpoint changed its response shape. It used to
return `"ok"`. It now returns `{ id, token }` so the creator's browser can
remember the owner token. Old clients that only check `response.ok` will
continue to work, the change is additive.

## Client-Side localStorage Keys

The board page reads and writes the following keys:

- `cn_name`, your display name (unchanged from original)
- `cn_color`, your selected name color index, 0 to 7
- `cn_owned`, map of `{ postId: ownerToken }` for posts you created
- `cn_claimed`, map of `{ postId: claimToken }` for posts you claimed
- `cn_voted`, map of `{ postId: optionIndex }` for polls you voted in
- `cn_reacted`, map of `{ postId: [reactionIndex, ...] }` for reactions you made

A periodic `pruneLocalMaps()` pass on every board refresh deletes entries
for post IDs that no longer appear in `/messages`, so these maps do not
grow unbounded.

## Persistence Format Change

`saveMessages()` was rewritten to stream one message at a time directly to
the file rather than building a single large `DynamicJsonDocument` in RAM.
This avoids a heap spike of roughly 100 KB on every save. `loadMessages()`
still parses the file in one pass and uses a 100 KB document
(`MSG_LOAD_DOC_SIZE`), which is the practical ceiling for the current
worst-case schema.

The on-disk JSON shape is a superset of the original. Old `/msgs.json`
files load cleanly. Posts loaded from old files get empty `ownerToken`
(their original authors will not be able to edit or delete them through
the new UI), `claimed = false`, `authorColor = 0`, and all reaction
counters at zero. Polls in old backups continue to work since polls did not
exist before this set of changes.

`handleAdminRestore()` was updated to read all the new fields with sensible
defaults when missing, so backup files saved before these changes restore
cleanly.

## Configuration Notes

The fixed-size buffer for parsing `/msgs.json` is defined as:

```cpp
#define MSG_LOAD_DOC_SIZE 102400
```

If you reduce `Config::MAX_MSGS` below 200, you can shrink this buffer
proportionally. If you ever migrate to ArduinoJson v7, this constant goes
away entirely (v7's `JsonDocument` grows dynamically).

The wave ring buffer size and TTL:

```cpp
#define MAX_WAVES   20
#define WAVE_TTL_MS 10000UL
```

The reaction set and its indices live in `Community_Hub_pages.h`:

```js
const REACTIONS = [
  { emoji: '👍', label: 'thanks' },
  { emoji: '🙋', label: 'me too' },
  { emoji: '🌻', label: 'nice'   },
  { emoji: '👀', label: 'noted'  }
];
```

Changing the emoji or label is safe. Reordering, removing, or inserting in
the middle is not, without a migration that rewrites the stored
`reactions[]` arrays.

The name color palette similarly lives in `Community_Hub_pages.h`:

```js
const NAME_COLORS = [
  'var(--ink)', '#4a6741', '#a05a2e', '#5a7a98',
  '#7a4a78',    '#b8893a', '#5a5550', '#3d6438'
];
```

Same migration caveats apply.

## Polling Cadences

Five intervals run on the main board page:

- `load()`, full message refresh, every 60 seconds
- `loadInfo()`, board identity and uptime, every 60 seconds
- `applyGreeting()`, re-check the hour for greeting and tint, every 60 seconds
- `updatePresence()`, neighbor count, every 15 seconds
- `checkWaves()`, wave poll, every 4 seconds (paused when tab is hidden)

Reactions and votes also trigger an immediate optimistic UI update plus a
full `load()` cycle.

## License

GPL-3.0
