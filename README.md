# Project Lantern (fork of Community Hub)

Fork of https://github.com/SonicDH/Community-Hub

This document covers everything added, changed, or restructured between the
original `Community_Hub.ino` and the current state of the sketch. It does not
re-document features that were present in the original.

Changes are grouped roughly chronologically: the original feature additions
that established this fork, then a performance refactor pass, then an admin
polish pass, then a handful of bug fixes uncovered while testing on real
hardware and devices.

## Sketch Layout

The original sketch was a single `.ino` file. The current build is split into
four files that all live in the `Project_Lantern/` sketch folder, plus one
helper script under `tools/`:

```
Project_Lantern/
    Project_Lantern.ino           (~2100 lines, all C++ and runtime logic)
    Community_Hub_pages.h         (~2350 lines, all HTML / CSS / JS source)
    Community_Hub_pages_gz.h      (~1700 lines, generated; gzipped page blobs)
    tools/
        build_gz_pages.py         (regenerates the _gz.h header from the source pages.h)
```

The `.ino` and `_pages.h` split exists because the Arduino IDE preprocessor
scans `.ino` files with a ctags-based parser to auto-generate function
prototypes, and that scanner desyncs on large raw string literals containing
JavaScript. Once it loses sync, it starts treating content inside
`R"PAGE(...)PAGE"` as if it were C++ code, producing errors like
`'function' does not name a type`. Headers are fed straight to the C++
compiler, which handles raw strings per the standard.

The source pages header holds four PROGMEM string constants:

- `INDEX_HTML`, the public bulletin board page
- `ADMIN_PAGE_HEAD`, the admin page up through the start of the runtime script
- `ADMIN_PAGE_TAIL`, the admin page from the end of the injected JS through
  the closing tags
- `WALL_HTML`, the shared graffiti canvas page

The `_gz.h` header holds gzipped binary blobs of those same constants, served
directly with `Content-Encoding: gzip`. See the "Gzip page serving" section
below. If you add new raw-string content in the future, put it in
`Community_Hub_pages.h` from the start and re-run the build script.

The `buildAdminPage()` function in the `.ino` assembles the admin page by
concatenating `FPSTR(ADMIN_PAGE_HEAD)`, a few lines of JS that inject runtime
values like the current identity fields, and `FPSTR(ADMIN_PAGE_TAIL)`. The
main board page and wall page are served directly from their gzipped blobs.

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

The admin panel adds its own intervals when logged in:

- `loadPostList()`, post list refresh, on demand
- `loadStats()`, stats tile grid, on login and on demand

# Performance Refactor

A pass focused on tightening RAM use, eliminating large transient allocations,
and shrinking on-the-wire payload. None of the changes alter the on-disk JSON
shape or the public API; old `/msgs.json` backups continue to load cleanly.

## Tightened Message Struct

The original `Message` struct used `String` for every text field including
fixed-length values, and embedded poll data in every message regardless of
whether the message was a poll. Across 200 message slots that wasted around
20 KB of heap on String overhead and unused poll fields.

The current layout:

- `type` is a `uint8_t` enum (`MsgType`: `MSG_NOTICE`, `MSG_OFFER`, `MSG_NEED`,
  `MSG_EVENT`, `MSG_POLL`). Converted to and from the human-readable string at
  the wire boundary only.
- `ownerToken` and `claimToken` are `uint8_t[8]` (8 raw bytes) instead of
  16-character hex Strings. Hex encoded/decoded only at the wire. Helpers
  `generateBinaryToken`, `tokenToHex`, `hexToToken`, `tokenIsZero`, and
  `tokenEqualsHex` handle the conversions.
- Poll fields moved out of `Message` into a sparse parallel pool
  (`PollData polls[MAX_POLLS]`, default 30 slots) keyed by message id.
  Non-poll messages no longer carry four empty `String` fields plus an
  options array.

`author`, `text`, and `claimedBy` remain Strings since they're variable-length
and most messages don't fill the maximum length.

Estimated saving across 200 messages, around 15 to 18 KB.

The poll pool is bounded. If 30 polls are active simultaneously, a 31st
`Poll`-typed post returns 503 ("board full") rather than partial-creating.
Realistically not reachable on a neighborhood board.

Helper functions `findPollIdx(uint16_t) -> int` and
`allocPollIdx(uint16_t) -> int` return slot indices rather than `PollData*`
because the Arduino IDE preprocessor injects function prototypes above
user-defined struct definitions, and a function whose signature mentions
`PollData` would fail with "PollData does not name a type". Same trick the
original `addMessage` used to avoid returning `Message` by value.

## Streamed `/messages` Response

The original `handleMessages()` built a 20 KB `JsonDocument` in RAM, then
serialized it. The current implementation streams chunk-by-chunk via
`server.sendContent()`: each message is built in its own ~1 KB document,
appended to a 1.5 KB buffer, and flushed whenever the buffer crosses 1 KB.
Peak transient RAM stays around 1 to 2 KB regardless of board size.

Same pattern was already in use by `handleWallGet()` for the same reason.

## Gzip Page Serving

`INDEX_HTML` and `WALL_HTML` are now served pre-compressed with
`Content-Encoding: gzip`. Compression ratios in practice:

- `INDEX_HTML`: ~42 KB raw -> ~11 KB gzipped (around 74% smaller)
- `WALL_HTML`: ~18 KB raw -> ~6 KB gzipped (around 67% smaller)

The browser decompresses transparently. The admin page stays uncompressed
because it's built dynamically (HEAD + injected JS + TAIL), and the
host loads it rarely enough that gzipping a dynamic response wouldn't pay
off the added complexity.

The gzipped blobs live in `Community_Hub_pages_gz.h` as PROGMEM `uint8_t`
arrays with companion `_LEN` constants. To regenerate after editing the
source pages, run from the sketch folder:

```
python3 tools/build_gz_pages.py
```

The script reads each `const char NAME[] PROGMEM = R"PAGE(...)PAGE";` block
from `Community_Hub_pages.h` and emits a matching `NAME_GZ[]` and `NAME_GZ_LEN`
in `Community_Hub_pages_gz.h`. It prints a compression-ratio summary on each
run. Re-flash after running.

The `.ino` includes both headers, but only `_gz.h` constants are referenced
in production response paths. Keeping the raw source pages around makes
diffs readable when editing.

# Admin Polish

## Pinned Posts

The admin can pin one post at a time to surface it above the regular board.
State is a single `uint16_t pinnedMsgId` global, persisted to `/pinned.json`,
with `0` meaning nothing is pinned.

Pinned posts are exempt from the normal expiry filter in `handleMessages()`,
so a pinned event doesn't disappear at 72h. They can still be edited,
deleted, claimed, and reacted to like any other post. Pin is metadata,
not status.

The pin auto-clears when its target post is removed by any path: owner
delete, admin delete, board-full eviction, admin restore, or admin clear-all.
A `clearPinIfMatches(uint16_t)` helper is wired into each of those sites so
`pinnedMsgId` never references a non-existent message.

The main board renders the pinned post in a separate `#pinnedSlot` div above
the post grid, with a warm gold border and a "📌 Pinned" ribbon. The card body
keeps its category color underneath so the pinned post still reads as Notice
or Offer or Event. The pinned card is de-duplicated from the regular grid so
it doesn't appear twice.

`/info` exposes the current `pinnedMsgId` so the main board can update its
pinned slot on the next `loadInfo()` cycle (every 60 seconds) without
fetching messages again.

### API

```
GET /admin/pin?token=...&id=N
    Pins post N.
    400 if id is missing or 0.
    404 if no such post.
    200 "pinned" on success.

GET /admin/unpin?token=...
    Clears the pin.
    200 "unpinned".
```

The admin UI surfaces a Pin/Unpin button on every row in "Manage Posts",
plus a status banner at the top of the section showing what's currently
pinned. The pinned row floats to the top of the admin list with a gold
tint and a small badge.

## Stats Panel

A new "Stats" section in the admin panel shows in-RAM activity counters
plus current state. Counters are reset on reboot (the "since boot" framing
matches the uptime field and avoids the complexity of persisting them).

The counters tracked, all `uint32_t` globals:

- `stats_posts_total`, total successful posts created
- `stats_reactions_total`, total reactions added
- `stats_claims_total`, total claims made on Offers and Needs
- `stats_votes_total`, total poll votes cast
- `stats_waves_total`, total waves sent
- `stats_strokes_total`, total wall strokes drawn

Increments live next to the relevant state mutations in `addMessage`,
`handlePostReact`, `handlePostClaim`, `handlePollVote`, `handleWavePost`,
and `handleWallStroke`.

### API

```
GET /admin/stats?token=...
    200: {
      posts_total, reactions_total, claims_total, votes_total,
      waves_total, strokes_total,                        (since boot)
      msg_count, max_msgs,                               (current message state)
      stroke_count, max_strokes,                         (current wall state)
      pinned,                                            (currently pinned msg id, 0 = none)
      by_type: { notice, offer, need, event, poll },     (current count per type)
      claimed, expired,                                  (current derived counts)
      free_heap, min_free_heap, heap_size,               (memory, in bytes)
      fs_used, fs_total,                                 (storage, in bytes)
      wifi_clients,                                      (devices currently associated)
      uptime_secs, uptime_str                            (both forms)
    }
```

The admin UI renders this as a grid of tiles with auto-flow layout. Each
tile shows a label, a big number, and an optional subtitle. A wide tile
at the bottom breaks down current posts by type with inline counts.

Loaded automatically on successful login. Manual "Refresh" button beside it
shows the time of the last update in small text.

# Bug Fixes

## Wall Going Blank After Navigation

If the board's clock was unset when a stroke was drawn, the stroke's
`createdEpoch` was a small number (seconds since boot) but
`opacityForAge()` on the client compared against `Date.now() / 1000`, which
is a real Unix epoch in the billions. The computed age was about 54 years,
`opacityForAge` returned 0, and the wall rendered blank on the next
`renderAll` cycle (after navigating away and back, or after the periodic
poll replaced the strokes array).

The optimistic `appendStroke` after a successful POST hardcoded
`opacity="1"`, which is why the bug only surfaced after the next full
render rather than immediately.

A secondary bug on the server: `pruneStrokes` did
`now - createdEpoch <= WALL_LIFETIME_SECS`, which underflowed
catastrophically as an unsigned subtraction if the clock was set after
strokes were drawn (`createdEpoch > now`). Every stroke became 4 billion
seconds old and got pruned.

The fix aligns both sides to server time:

- `/wall/data`, `/wall/info`, and `/wall/stroke` now emit an `X-Server-Now`
  response header carrying the board's current `nowSecs()`.
- `/wall/stroke` POST response also includes the stroke's `t` (its server
  `createdEpoch`) so the optimistic local insert uses the same frame.
- The client maintains `serverEpochOffsetSecs` and a `haveServerOffset`
  flag, updated from the header on every `/wall/data` or `/wall/stroke`
  response. `serverNowSec()` returns the browser's clock plus that offset.
- `opacityForAge()` now uses `serverNowSec()` instead of `Date.now()`.
- `pruneStrokes()` got an underflow guard: strokes whose `createdEpoch > now`
  are treated as fresh (age 0) instead of effectively-infinitely-old.

A similar time-reference issue likely affects the main board's "X minutes
ago" labels and message expiry countdowns. Not yet patched.

## iOS Double-Tap on Interactive Elements

On iPhone/iPad, links and buttons required two taps to fire. Two root
causes, both addressed:

The hover layer. Any element with a `:hover` style on iOS consumes the
first tap to show the hover state; the second tap fires the click. All 17
`:hover` rules across the three pages were wrapped in
`@media (hover: hover) and (pointer: fine) { ... }` so they only apply on
devices with real hover capability. Desktop browsers behave identically;
touch-only devices get no hover state to consume.

The gesture-detection layer. Even after the hover fix, iOS Safari delays
click delivery on tappable elements while it checks for double-tap-to-zoom
and other gestures. A `<style>` block was injected at the top of each page:

```css
a, button {
  touch-action: manipulation;
  -webkit-tap-highlight-color: transparent;
}
```

`touch-action: manipulation` explicitly disables double-tap zoom on the
targeted elements, telling iOS to deliver clicks immediately.

## Safari 18 "Fetch API cannot load" on Wave Poll

Safari 18 has a known regression where reusing a kept-alive TCP connection
that the server has already closed produces the error
`Fetch API cannot load X due to access control checks` even when CORS is
correctly configured. The error message is misleading; the actual cause is
the keep-alive race.

`/wave/recent` polls every 4 seconds, right in the window where the ESP32
has typically torn down the connection on its end but Safari thinks it's
still alive. Other endpoints (15s+ poll cadence) are outside the window and
don't trip the bug.

`handleWaveRecent` now sends `Connection: close` so each poll opens a fresh
TCP connection, sidestepping the race entirely. The other endpoints retain
keep-alive since the trade-off (TCP handshake on every poll vs. occasional
errors) only pays off at this cadence.

Reference: https://discussions.apple.com/thread/256112607

# Configuration Notes (Added)

## Poll Pool Size

```cpp
#define MAX_POLLS 30
```

Number of simultaneous polls supported. If exhausted, new `Poll`-typed posts
return 503 "board full" until an existing poll is deleted or expires. The
pool is sparse and shared across `MAX_MSGS`, so it doesn't need to scale with
the message count ceiling.

## Pinned Post

State lives in `/pinned.json`:

```json
{ "id": 42 }
```

Loaded on boot by `loadPinned()`. `0` means nothing is pinned.

## Build Script Dependencies

`tools/build_gz_pages.py` uses only the Python standard library (`gzip`,
`re`, `pathlib`). Python 3.6+. No `pip install` required.

## License

GPL-3.0
