/*
 * Community_Hub_pages.h
 *
 * Web page content for the Community Hub.
 *
 * This file exists separately from Community_Hub.ino because the Arduino IDE's
 * preprocessor performs a ctags-based scan of .ino files to auto-generate
 * function prototypes, and large raw string literals containing JavaScript
 * can desync that scanner. Putting the raw strings in a .h file bypasses
 * the auto-prototype pass entirely.
 *
 * Drop this file next to Community_Hub.ino in the sketch folder.
 */

#pragma once
#include <Arduino.h>
#include <pgmspace.h>

// ============================================================================
// Main board page
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"PAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Community Hub</title>
<style>
:root {
  --bg:            #ede8de;
  --surface:       #faf8f3;
  --surface2:      #f0ebe0;
  --border:        #b0a080;
  --border-light:  #d4c9b0;
  --ink:           #2c2416;
  --ink-muted:     #7a6a55;
  --accent:        #8aad87;
  --accent-dark:   #31502f;
  --accent-light:  #c8ddc6;

  --c-notice-fg:  #6b5a3e; --c-notice-bg: #f0e8d8; --c-notice-bar: #a09070;
  --c-offer-fg:   #2d5c2a; --c-offer-bg:  #daeeda; --c-offer-bar:  #5a8a57;
  --c-need-fg:    #7a4a10; --c-need-bg:   #f5e8d0; --c-need-bar:   #c4813a;
  --c-event-fg:   #1e4f70; --c-event-bg:  #d8eaf5; --c-event-bar:  #4a88b0;
  --c-poll-fg:    #5d3a78; --c-poll-bg:   #e8dcf0; --c-poll-bar:   #8a5fb0;

  --radius: 6px;
  --shadow: 3px 4px 0 rgba(44,36,22,0.10);
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

body {
  background: var(--bg);
  color: var(--ink);
  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

/* Header */
.site-header {
  background: var(--accent-dark);
  padding: 14px 20px;
  display: flex;
  align-items: center;
  gap: 14px;
  border-bottom: 3px solid #1d3a1b;
  flex-wrap: wrap;
}
.header-title-block { flex: 1; min-width: 160px; }
.site-title { font-size: 20px; font-weight: bold; color: #d8edcf; line-height: 1.2; }
.site-sub   { font-size: 14px; color: var(--accent); margin-top: 2px; }
.header-meta {
  text-align: right; font-size: 11px; color: var(--accent); line-height: 1.7;
}

/* Post form */
.post-form { background: var(--surface); border-bottom: 2px solid var(--border); padding: 14px 20px; }
.form-row { display: flex; gap: 10px; align-items: flex-start; flex-wrap: wrap; }
.field { display: flex; flex-direction: column; gap: 3px; }
.field label {
  font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--ink-muted);
}
.field input, .field textarea {
  background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius);
  color: var(--ink); font-family: inherit; font-size: 14px; padding: 7px 10px; outline: none;
  transition: border-color .2s;
}
.field input:focus, .field textarea:focus { border-color: var(--accent-dark); }
.field-name { flex: 0 0 160px; }
.field-msg  { flex: 1 1 220px; }
.field textarea { resize: vertical; height: 90px; min-height: 60px; }
.char-hint { font-size: 10px; color: var(--ink-muted); text-align: right; height: 14px; }
.char-hint.warn { color: var(--c-need-bar); }

.field-cat { flex: 0 0 auto; }
.cat-btns { display: flex; gap: 5px; flex-wrap: wrap; padding-top: 1px; }
.cat-btn {
  background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius);
  color: var(--ink-muted); cursor: pointer; font-size: 11px; padding: 5px 10px;
  transition: all .15s; white-space: nowrap; font-family: inherit;
}
.cat-btn:hover { border-color: var(--border); color: var(--ink); background: var(--surface2); }
.cat-btn.active-Notice { background: var(--c-notice-bg); border-color: var(--c-notice-bar); color: var(--c-notice-fg); font-weight: bold; }
.cat-btn.active-Offer  { background: var(--c-offer-bg);  border-color: var(--c-offer-bar);  color: var(--c-offer-fg);  font-weight: bold; }
.cat-btn.active-Need   { background: var(--c-need-bg);   border-color: var(--c-need-bar);   color: var(--c-need-fg);   font-weight: bold; }
.cat-btn.active-Event  { background: var(--c-event-bg);  border-color: var(--c-event-bar);  color: var(--c-event-fg);  font-weight: bold; }
.cat-btn.active-Poll   { background: var(--c-poll-bg);   border-color: var(--c-poll-bar);   color: var(--c-poll-fg);   font-weight: bold; }

/* Poll-options input block (only visible when Poll is selected) */
#pollBlock { display: none; margin-top: 8px; gap: 6px; flex-direction: column; }
#pollBlock.show { display: flex; }
#pollBlock .opt-row { display: flex; gap: 6px; }
#pollBlock .opt-row input {
  flex: 1; background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius);
  color: var(--ink); font-family: inherit; font-size: 13px; padding: 6px 10px; outline: none;
}
#pollBlock .opt-row input:focus { border-color: var(--c-poll-bar); }

.expiry-row {
  display: flex; gap: 6px; align-items: center; margin-top: 8px; flex-wrap: wrap;
}
.expiry-label { font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--ink-muted); margin-right: 2px; }
.exp-btn {
  background: var(--bg); border: 1px solid var(--border-light); border-radius: 99px;
  color: var(--ink-muted); cursor: pointer; font-size: 11px; padding: 3px 11px;
  transition: all .15s; font-family: inherit;
}
.exp-btn:hover  { border-color: var(--border); color: var(--ink); }
.exp-btn.active { background: var(--accent-dark); border-color: var(--accent-dark); color: var(--accent-light); }

.post-btn {
  background: var(--accent-dark); border: 2px solid #1d3a1b; border-radius: var(--radius);
  color: var(--accent-light); cursor: pointer; font-size: 12px; font-weight: bold;
  letter-spacing: 1px; padding: 8px 20px; margin-top: 8px; transition: background .15s;
  white-space: nowrap; align-self: flex-end; font-family: inherit;
}
.post-btn:hover { background: #3d6438; }

/* Filter bar */
.filter-bar {
  background: var(--surface2); border-bottom: 1px solid var(--border-light);
  padding: 9px 20px; display: flex; align-items: center; gap: 6px; flex-wrap: wrap;
}
.filter-label { font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--ink-muted); }
.sep { flex: 1; }
.ftab {
  background: none; border: 1px solid var(--border-light); border-radius: 99px;
  color: var(--ink-muted); cursor: pointer; font-size: 11px; padding: 3px 12px;
  transition: all .15s; font-family: inherit;
}
.ftab:hover  { border-color: var(--accent-dark); color: var(--accent-dark); }
.ftab.active { background: var(--accent-dark); border-color: var(--accent-dark); color: var(--accent-light); }

/* Board */
.board { flex: 1; padding: 16px 20px; columns: 3 260px; gap: 14px; }

.card {
  display: inline-block; width: 100%; margin-bottom: 14px;
  background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
  box-shadow: var(--shadow); break-inside: avoid; overflow: hidden;
  animation: cardIn .2s ease;
}
@keyframes cardIn { from { opacity:0; transform:translateY(5px); } }
.card-stripe { height: 4px; }
.card-body { padding: 11px 13px 9px; }
.cat-badge {
  display: inline-block; font-size: 10px; font-weight: bold; letter-spacing: 1px;
  text-transform: uppercase; padding: 2px 9px; border-radius: 99px; margin-bottom: 7px;
}
.card-text { font-size: 14px; line-height: 1.6; color: var(--ink); word-break: break-word; }
.card-footer {
  display: flex; align-items: center; justify-content: space-between; gap: 8px;
  padding: 7px 13px; background: var(--surface2); border-top: 1px solid var(--border-light);
  font-size: 11px; color: var(--ink-muted);
}
.card-author { font-weight: bold; color: var(--ink); }
.card-expiry { font-size: 10px; }
.card-expiry.soon { color: var(--c-need-bar); }

.type-Notice .card-stripe { background: var(--c-notice-bar); }
.type-Notice .cat-badge   { background: var(--c-notice-bg); color: var(--c-notice-fg); }
.type-Offer  .card-stripe { background: var(--c-offer-bar);  }
.type-Offer  .cat-badge   { background: var(--c-offer-bg);  color: var(--c-offer-fg);  }
.type-Need   .card-stripe { background: var(--c-need-bar);  }
.type-Need   .cat-badge   { background: var(--c-need-bg);   color: var(--c-need-fg);   }
.type-Event  .card-stripe { background: var(--c-event-bar); }
.type-Event  .cat-badge   { background: var(--c-event-bg);  color: var(--c-event-fg);  }
.type-Poll   .card-stripe { background: var(--c-poll-bar);  }
.type-Poll   .cat-badge   { background: var(--c-poll-bg);   color: var(--c-poll-fg);   }

/* Claim badge */
.claim-banner {
  margin-top: 8px; padding: 6px 10px; background: var(--c-offer-bg);
  border: 1px solid var(--c-offer-bar); border-radius: var(--radius);
  font-size: 12px; color: var(--c-offer-fg); display: flex;
  align-items: center; justify-content: space-between; gap: 8px;
}
.type-Need .claim-banner { background: var(--c-need-bg); border-color: var(--c-need-bar); color: var(--c-need-fg); }

/* Inline action buttons (claim, edit, delete, vote) */
.card-actions { display: flex; gap: 6px; margin-top: 8px; flex-wrap: wrap; }
.act-btn {
  background: var(--surface2); border: 1px solid var(--border-light); border-radius: 99px;
  color: var(--ink-muted); cursor: pointer; font-size: 11px; padding: 3px 10px;
  transition: all .15s; font-family: inherit;
}
.act-btn:hover { border-color: var(--accent-dark); color: var(--accent-dark); }
.act-btn.danger:hover { border-color: #c0392b; color: #c0392b; }

/* Poll voting UI */
.poll-options { display: flex; flex-direction: column; gap: 5px; margin-top: 9px; }
.poll-opt {
  background: var(--bg); border: 1px solid var(--c-poll-bar); border-radius: var(--radius);
  color: var(--c-poll-fg); cursor: pointer; font-size: 13px; padding: 7px 11px;
  text-align: left; transition: all .15s; font-family: inherit;
}
.poll-opt:hover { background: var(--c-poll-bg); }
.poll-results { display: flex; flex-direction: column; gap: 6px; margin-top: 9px; }
.poll-result {
  position: relative; background: var(--bg); border: 1px solid var(--border-light);
  border-radius: var(--radius); padding: 6px 10px; font-size: 13px; overflow: hidden;
}
.poll-result.mine { border-color: var(--c-poll-bar); }
.poll-result .bar {
  position: absolute; left: 0; top: 0; bottom: 0; background: var(--c-poll-bg);
  z-index: 0; transition: width .4s;
}
.poll-result .label, .poll-result .count {
  position: relative; z-index: 1;
}
.poll-result .label { color: var(--ink); }
.poll-result .count {
  float: right; color: var(--ink-muted); font-size: 11px;
}
.poll-total { font-size: 10px; color: var(--ink-muted); margin-top: 4px; text-align: right; }

/* Inline edit */
.edit-area {
  width: 100%; background: var(--bg); border: 1px solid var(--accent-dark);
  border-radius: var(--radius); color: var(--ink); font-family: inherit; font-size: 14px;
  padding: 6px 8px; resize: vertical; min-height: 60px; outline: none; margin-top: 4px;
}

/* Empty state */
.empty {
  column-span: all; text-align: center; padding: 60px 20px; color: var(--ink-muted);
  font-size: 13px; line-height: 2.2;
}

/* Footer */
.site-footer {
  background: var(--surface2); border-top: 1px solid var(--border-light);
  padding: 7px 20px; display: flex; justify-content: space-between;
  font-size: 10px; color: var(--ink-muted);
}

/* ── Time-of-day tints, applied via data-tint on <body> ────────────────────── */
body[data-tint] { transition: background-color 1.5s ease; }
body[data-tint="morning"]   { background-color: #f3edda; }
body[data-tint="afternoon"] { background-color: #ede8de; }
body[data-tint="evening"]   { background-color: #ebdcd0; }
body[data-tint="night"]     { background-color: #d6d3cc; }

/* ── Presence pill + wave button (header) ─────────────────────────────────── */
.header-extras {
  display: flex; align-items: center; gap: 10px; flex-wrap: wrap;
  margin-top: 6px; justify-content: flex-end;
}
.neighbors-here {
  font-size: 11px; color: var(--accent-light);
  display: inline-flex; align-items: center; gap: 4px;
}
.wave-btn {
  background: var(--accent-light); border: 1px solid var(--accent);
  border-radius: 99px; padding: 3px 11px; color: var(--accent-dark);
  cursor: pointer; font-size: 12px; font-family: inherit; transition: all .15s;
}
.wave-btn:hover  { background: var(--accent); }
.wave-btn:active { transform: scale(0.93); }
.wave-btn:disabled { opacity: 0.5; cursor: default; }

/* Floating wave animation (appears for every connected client) */
.wave-float {
  position: fixed; bottom: 40px; pointer-events: none;
  text-align: center; z-index: 1000;
  animation: waveUp 3.5s ease-out forwards;
}
.wave-float .wave-icon { display: block; font-size: 36px; line-height: 1; }
.wave-float .wave-from {
  display: block; font-size: 11px; color: var(--ink-muted);
  margin-top: 2px; background: var(--surface); padding: 1px 6px;
  border-radius: 99px; border: 1px solid var(--border-light);
}
@keyframes waveUp {
  0%   { transform: translateY(20px) scale(0.5); opacity: 0; }
  15%  { transform: translateY(0)    scale(1.1); opacity: 1; }
  25%  { transform: translateY(-20px) scale(1); }
  100% { transform: translateY(-260px) scale(1); opacity: 0; }
}

/* ── Reactions row on cards ───────────────────────────────────────────────── */
.reactions { display: flex; gap: 4px; flex-wrap: wrap; margin-top: 9px; }
.rxn {
  background: var(--bg); border: 1px solid var(--border-light);
  border-radius: 99px; padding: 2px 9px; font-size: 13px;
  color: var(--ink-muted); cursor: pointer; transition: all .15s;
  font-family: inherit; line-height: 1.4;
}
.rxn:hover { border-color: var(--accent-dark); }
.rxn.did   { background: var(--accent-light); border-color: var(--accent-dark); color: var(--accent-dark); }
.rxn .count { font-size: 11px; margin-left: 2px; }

/* ── Name color picker in the post form ───────────────────────────────────── */
.color-picker {
  display: flex; gap: 5px; margin-top: 4px; flex-wrap: wrap;
  align-items: center;
}
.color-picker .pick-label {
  font-size: 10px; letter-spacing: 1px; text-transform: uppercase;
  color: var(--ink-muted); margin-right: 4px;
}
.color-swatch {
  width: 20px; height: 20px; border-radius: 50%; cursor: pointer;
  border: 2px solid transparent; transition: transform .15s;
  display: inline-block;
}
.color-swatch:hover  { transform: scale(1.15); }
.color-swatch.active { border-color: var(--ink); }

/* Author name color override (set inline via style attribute on the span) */
.card-author[data-c] { color: inherit; }

@media (max-width: 540px) {
  .board { columns: 1; padding: 12px; }
  .form-row { flex-direction: column; }
  .field-name { flex: 1 1 auto; }
  .post-btn { width: 100%; text-align: center; }
  .header-extras { width: 100%; justify-content: space-between; }
}
</style>
</head>
<body>

<header class="site-header">
  <div class="header-title-block">
    <div class="site-title" id="boardTitle">COMMUNITY HUB</div>
    <div class="site-sub"   id="boardTagline"></div>
  </div>
  <div class="header-meta">
    <div id="boardRules"></div>
    <div id="postCount">— posts</div>
    <div class="header-extras">
      <span class="neighbors-here" id="neighborsHere">🟢 —</span>
      <button class="wave-btn" id="waveBtn" onclick="sendWave()" title="Say hi to everyone here">wave 👋</button>
    </div>
  </div>
</header>

<div id="fullBanner" style="display:none;background:#a30000;color:#fff;
     text-align:center;padding:10px 16px;font-weight:600">
  📋 This board is currently full. Check back once some posts have expired. 📋
</div>

<div class="post-form">
  <div class="form-row">

    <div class="field field-name">
      <label>Your Name</label>
      <input id="nameIn" maxlength="24" placeholder="neighbor"
             autocomplete="off" spellcheck="false">
      <div class="char-hint" id="nameHint"></div>
      <div class="color-picker" id="colorPicker">
        <span class="pick-label">Color:</span>
        <!-- Filled by JS so the swatch backgrounds match the JS palette -->
      </div>
    </div>

    <div class="field field-cat">
      <label>Category</label>
      <div class="cat-btns">
        <button class="cat-btn" onclick="setType('Notice',this)">📌 Notice</button>
        <button class="cat-btn" onclick="setType('Offer', this)">🌱 Offer</button>
        <button class="cat-btn" onclick="setType('Need',  this)">🤝 Need</button>
        <button class="cat-btn" onclick="setType('Event', this)">📅 Event</button>
        <button class="cat-btn" onclick="setType('Poll',  this)">📊 Poll</button>
      </div>
      <div class="char-hint"></div>
    </div>

    <div class="field field-msg">
      <label id="msgLabel">Message</label>
      <textarea id="msgIn" maxlength="300"
                placeholder="What's on the board?"
                spellcheck="false"></textarea>
      <div class="char-hint" id="msgHint"></div>

      <div id="pollBlock">
        <label style="font-size:10px;letter-spacing:1px;text-transform:uppercase;color:var(--ink-muted)">
          Poll Options (2-4)
        </label>
        <div class="opt-row"><input id="opt0" maxlength="60" placeholder="Option 1"></div>
        <div class="opt-row"><input id="opt1" maxlength="60" placeholder="Option 2"></div>
        <div class="opt-row"><input id="opt2" maxlength="60" placeholder="Option 3 (optional)"></div>
        <div class="opt-row"><input id="opt3" maxlength="60" placeholder="Option 4 (optional)"></div>
      </div>
    </div>

  </div>

  <div class="expiry-row">
    <span class="expiry-label">Expires:</span>
    <button class="exp-btn" onclick="setExpiry(24,  this)">1 day</button>
    <button class="exp-btn" onclick="setExpiry(72,  this)">3 days</button>
    <button class="exp-btn active" onclick="setExpiry(168, this)">1 week</button>
    <button class="post-btn" id="postBtn" onclick="doPost()">POST</button>
  </div>
</div>

<div class="filter-bar">
  <span class="filter-label">Show:</span>
  <button class="ftab active" onclick="setFilter('',       this)">All</button>
  <button class="ftab"        onclick="setFilter('Notice', this)">📌 Notice</button>
  <button class="ftab"        onclick="setFilter('Offer',  this)">🌱 Offer</button>
  <button class="ftab"        onclick="setFilter('Need',   this)">🤝 Need</button>
  <button class="ftab"        onclick="setFilter('Event',  this)">📅 Event</button>
  <button class="ftab"        onclick="setFilter('Poll',   this)">📊 Poll</button>
  <span class="sep"></span>
  <button class="ftab active" id="sNew" onclick="setSort('new', this)">New</button>
  <button class="ftab"        id="sExp" onclick="setSort('exp', this)">Expiring</button>
</div>

<div class="board" id="board"></div>

<footer class="site-footer">
  <span id="boardFooter"></span>
  <span id="uptimeDisplay">—</span>
</footer>

<script>
// ── Local ownership maps ──────────────────────────────────────────────────────
// cn_owned   : { postId: ownerToken }  posts I created
// cn_claimed : { postId: claimToken }  posts I claimed
// cn_voted   : { postId: optionIndex } polls I voted in
function readMap(k) { try { return JSON.parse(localStorage.getItem(k) || '{}'); } catch { return {}; } }
function writeMap(k, m) { localStorage.setItem(k, JSON.stringify(m)); }
function getOwned()      { return readMap('cn_owned'); }
function getClaimed()    { return readMap('cn_claimed'); }
function getVoted()      { return readMap('cn_voted'); }
function rememberOwned(id, token)   { const m = getOwned();   m[id] = token; writeMap('cn_owned', m); }
function rememberClaimed(id, token) { const m = getClaimed(); m[id] = token; writeMap('cn_claimed', m); }
function rememberVoted(id, opt)     { const m = getVoted();   m[id] = opt;   writeMap('cn_voted', m); }
function forgetOwned(id)   { const m = getOwned();   delete m[id]; writeMap('cn_owned', m); }
function forgetClaimed(id) { const m = getClaimed(); delete m[id]; writeMap('cn_claimed', m); }

// Periodic cleanup: drop tokens for posts that no longer exist or expired.
function pruneLocalMaps(currentIds) {
  const live = new Set(currentIds);
  ['cn_owned', 'cn_claimed', 'cn_voted', 'cn_reacted'].forEach(k => {
    const m = readMap(k);
    let dirty = false;
    Object.keys(m).forEach(id => { if (!live.has(Number(id))) { delete m[id]; dirty = true; } });
    if (dirty) writeMap(k, m);
  });
}

// ── Status banner ─────────────────────────────────────────────────────────────
function checkBoardStatus() {
  fetch('/api/status').then(r => r.json()).then(d => {
    const banner = document.getElementById('fullBanner');
    const btn    = document.getElementById('postBtn');
    if (d.full) { banner.style.display = 'block'; if (btn) btn.disabled = true; }
    else        { banner.style.display = 'none';  if (btn) btn.disabled = false; }
  }).catch(() => {});
}

// ── XSS-safe escaping ─────────────────────────────────────────────────────────
function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
                  .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

function timeLeft(expSecs) {
  const left = expSecs - Math.floor(Date.now() / 1000);
  if (left <= 0)    return { label: 'expired',   soon: true  };
  if (left < 3600)  return { label: Math.floor(left / 60)    + 'm left', soon: true  };
  if (left < 86400) return { label: Math.floor(left / 3600)  + 'h left', soon: left < 10800 };
  return { label: Math.floor(left / 86400) + 'd left', soon: false };
}

// ── State ─────────────────────────────────────────────────────────────────────
let currentType   = 'Notice';
let currentExpiry = 168;
let activeFilter  = '';
let activeSort    = 'new';
let lastData      = [];

// Name color palette. Index 0 = default ink, 1..7 = picks.
// Indices are persisted server-side as uint8_t, so don't shuffle these.
const NAME_COLORS = [
  'var(--ink)',  // 0 default
  '#4a6741',     // 1 forest
  '#a05a2e',     // 2 sienna
  '#5a7a98',     // 3 dusty blue
  '#7a4a78',     // 4 aubergine
  '#b8893a',     // 5 ochre
  '#5a5550',     // 6 slate
  '#3d6438'      // 7 herb
];
let myColor = Math.max(0, Math.min(7, Number(localStorage.getItem('cn_color') || '0')));

// Reaction definitions. Indices are persisted server-side; don't shuffle.
const REACTIONS = [
  { emoji: '👍', label: 'thanks' },
  { emoji: '🙋', label: 'me too' },
  { emoji: '🌻', label: 'nice'   },
  { emoji: '👀', label: 'noted'  }
];

// localStorage maps for reactions and seen wave id
function getReacted()      { try { return JSON.parse(localStorage.getItem('cn_reacted') || '{}'); } catch { return {}; } }
function rememberReacted(id, type) {
  const m = getReacted();
  if (!m[id]) m[id] = [];
  if (!m[id].includes(type)) m[id].push(type);
  localStorage.setItem('cn_reacted', JSON.stringify(m));
}
function hasReacted(id, type) {
  const m = getReacted();
  return !!(m[id] && m[id].includes(type));
}
let lastWaveId = 0;

const savedName = localStorage.getItem('cn_name');
if (savedName) document.getElementById('nameIn').value = savedName;

document.getElementById('nameIn').addEventListener('input', function () {
  localStorage.setItem('cn_name', this.value);
  const h = document.getElementById('nameHint');
  const n = this.value.length;
  h.textContent = n > 18 ? n + '/24' : '';
  h.className   = 'char-hint' + (n > 20 ? ' warn' : '');
});

document.getElementById('msgIn').addEventListener('input', function () {
  const h = document.getElementById('msgHint');
  const n = this.value.length;
  h.textContent = n > 240 ? n + '/300' : '';
  h.className   = 'char-hint' + (n > 270 ? ' warn' : '');
});

document.getElementById('msgIn').addEventListener('keydown', function (e) {
  if (e.key === 'Enter' && !e.shiftKey && currentType !== 'Poll') {
    e.preventDefault(); doPost();
  }
});

// ── Category selection ────────────────────────────────────────────────────────
function setType(t, btn) {
  currentType = t;
  document.querySelectorAll('.cat-btn').forEach(b => { b.className = 'cat-btn'; });
  btn.classList.add('active-' + t);
  const pollBlock = document.getElementById('pollBlock');
  const msgLabel  = document.getElementById('msgLabel');
  const msgIn     = document.getElementById('msgIn');
  if (t === 'Poll') {
    pollBlock.classList.add('show');
    msgLabel.textContent = 'Question';
    msgIn.placeholder    = 'What should we decide?';
  } else {
    pollBlock.classList.remove('show');
    msgLabel.textContent = 'Message';
    msgIn.placeholder    = "What's on the board?";
  }
}
document.querySelector('.cat-btn').classList.add('active-Notice');

function setExpiry(h, btn) {
  currentExpiry = h;
  document.querySelectorAll('.exp-btn').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
}

function setFilter(f, btn) {
  activeFilter = f;
  document.querySelectorAll('.filter-bar .ftab').forEach(b => {
    if (b.id === 'sNew' || b.id === 'sExp') return;
    b.classList.remove('active');
  });
  btn.classList.add('active');
  if (lastData.length) render(lastData);
}

function setSort(s, btn) {
  activeSort = s;
  document.getElementById('sNew').classList.toggle('active', s === 'new');
  document.getElementById('sExp').classList.toggle('active', s === 'exp');
  load();
}

// ── Data ──────────────────────────────────────────────────────────────────────
async function load() {
  try {
    checkBoardStatus();
    const r  = await fetch('/messages');
    lastData = await r.json();
    pruneLocalMaps(lastData.map(m => m.id));
    render(lastData);
  } catch (_) { /* offline; keep current view */ }
}

function renderPollBlock(m) {
  const voted = getVoted();
  const myVote = voted[m.id];
  const total = (m.votes || []).reduce((a, b) => a + b, 0);
  if (myVote === undefined) {
    return '<div class="poll-options">' +
      m.options.map((o, i) =>
        `<button class="poll-opt" onclick="doVote(${m.id}, ${i})">${esc(o)}</button>`
      ).join('') +
    '</div>';
  }
  return '<div class="poll-results">' +
    m.options.map((o, i) => {
      const v = (m.votes && m.votes[i]) || 0;
      const pct = total > 0 ? Math.round((v / total) * 100) : 0;
      const mine = (i === myVote) ? ' mine' : '';
      const check = (i === myVote) ? ' ✓' : '';
      return `<div class="poll-result${mine}">
        <div class="bar" style="width:${pct}%"></div>
        <span class="label">${esc(o)}${check}</span>
        <span class="count">${v} (${pct}%)</span>
      </div>`;
    }).join('') +
    `<div class="poll-total">${total} vote${total !== 1 ? 's' : ''}</div>` +
  '</div>';
}

function renderActions(m) {
  const owned   = getOwned();
  const claimed = getClaimed();
  const isMine  = !!owned[m.id];
  const iClaimed = !!claimed[m.id];
  const claimable = (m.type === 'Offer' || m.type === 'Need') && !m.claimed;
  const out = [];
  if (claimable) {
    out.push(`<button class="act-btn" onclick="doClaim(${m.id})">I'll take it</button>`);
  }
  if (m.claimed && (iClaimed || isMine)) {
    out.push(`<button class="act-btn" onclick="doUnclaim(${m.id})">Unclaim</button>`);
  }
  if (isMine) {
    out.push(`<button class="act-btn"        onclick="beginEdit(${m.id})">Edit</button>`);
    out.push(`<button class="act-btn danger" onclick="doDelete(${m.id})">Delete</button>`);
  }
  if (out.length === 0) return '';
  return '<div class="card-actions">' + out.join('') + '</div>';
}

function renderReactions(m) {
  const counts = m.reactions || [0, 0, 0, 0];
  return '<div class="reactions">' +
    REACTIONS.map((r, i) => {
      const did = hasReacted(m.id, i) ? ' did' : '';
      const c   = counts[i] || 0;
      const cs  = c > 0 ? `<span class="count">${c}</span>` : '';
      return `<button class="rxn${did}" onclick="doReact(${m.id}, ${i})" title="${r.label}">${r.emoji}${cs}</button>`;
    }).join('') +
  '</div>';
}

function render(data) {
  let filtered = activeFilter ? data.filter(m => m.type === activeFilter) : data;
  if (activeSort === 'exp') {
    filtered = [...filtered].sort((a, b) => a.expires - b.expires);
  }

  document.getElementById('postCount').textContent =
    data.length + ' post' + (data.length !== 1 ? 's' : '');

  const board = document.getElementById('board');
  if (filtered.length === 0) {
    board.innerHTML = '<div class="empty">Nothing here yet.<br>Be the first to post.</div>';
    return;
  }

  board.innerHTML = filtered.map(m => {
    const tl     = timeLeft(m.expires);
    const isPoll = m.type === 'Poll' && Array.isArray(m.options) && m.options.length >= 2;
    const claimBanner = m.claimed
      ? `<div class="claim-banner"><span>✓ Claimed by <b>${esc(m.claimedBy || 'someone')}</b></span></div>`
      : '';
    const colorIdx = Math.max(0, Math.min(7, Number(m.authorColor || 0)));
    const colorStyle = colorIdx > 0 ? ` style="color:${NAME_COLORS[colorIdx]}"` : '';
    return `
<div class="card type-${esc(m.type)}" id="card-${m.id}">
  <div class="card-stripe"></div>
  <div class="card-body">
    <span class="cat-badge">${esc(m.type)}</span>
    <div class="card-text" id="text-${m.id}">${esc(m.text)}</div>
    ${isPoll ? renderPollBlock(m) : ''}
    ${claimBanner}
    ${renderReactions(m)}
    ${renderActions(m)}
  </div>
  <div class="card-footer">
    <span class="card-author"${colorStyle}>${esc(m.author)}</span>
    <span class="card-expiry${tl.soon ? ' soon' : ''}">${tl.label}</span>
  </div>
</div>`;
  }).join('');
}

// ── Post ──────────────────────────────────────────────────────────────────────
async function doPost() {
  checkBoardStatus();
  const n = document.getElementById('nameIn').value.trim() || 'neighbor';
  const t = document.getElementById('msgIn').value.trim();
  if (!t) return;

  let options = null;
  if (currentType === 'Poll') {
    options = [0,1,2,3]
      .map(i => document.getElementById('opt' + i).value.trim())
      .filter(s => s.length > 0);
    if (options.length < 2) {
      alert('Polls need at least 2 options.');
      return;
    }
  }

  localStorage.setItem('cn_name', n);
  document.getElementById('nameIn').value = n;

  const body = { author: n, type: currentType, text: t, expiry: currentExpiry };
  if (options) body.options = options;
  if (myColor > 0) body.authorColor = myColor;

  const r = await fetch('/post', { method: 'POST', body: JSON.stringify(body) });
  if (!r.ok) { alert('Post failed: ' + await r.text()); return; }
  const data = await r.json();
  if (data && data.id && data.token) {
    rememberOwned(data.id, data.token);
  }

  document.getElementById('msgIn').value = '';
  document.getElementById('msgHint').textContent = '';
  [0,1,2,3].forEach(i => { document.getElementById('opt' + i).value = ''; });
  load();
}

// ── Edit / delete ─────────────────────────────────────────────────────────────
function beginEdit(id) {
  const m = lastData.find(x => x.id === id);
  if (!m) return;
  const textEl = document.getElementById('text-' + id);
  textEl.innerHTML = `
    <textarea class="edit-area" id="edit-${id}" maxlength="300">${esc(m.text)}</textarea>
    <div class="card-actions" style="margin-top:6px">
      <button class="act-btn"        onclick="saveEdit(${id})">Save</button>
      <button class="act-btn danger" onclick="load()">Cancel</button>
    </div>`;
}

async function saveEdit(id) {
  const token = getOwned()[id];
  if (!token) return;
  const newText = document.getElementById('edit-' + id).value.trim();
  if (!newText) { alert('Cannot save empty message.'); return; }
  const r = await fetch('/post/edit', {
    method: 'POST',
    body: JSON.stringify({ id, token, text: newText })
  });
  if (!r.ok) { alert('Edit failed: ' + await r.text()); return; }
  load();
}

async function doDelete(id) {
  if (!confirm('Delete this post?')) return;
  const token = getOwned()[id];
  if (!token) return;
  const r = await fetch('/post/delete', {
    method: 'POST',
    body: JSON.stringify({ id, token })
  });
  if (!r.ok) { alert('Delete failed: ' + await r.text()); return; }
  forgetOwned(id);
  load();
}

// ── Claim / unclaim ───────────────────────────────────────────────────────────
async function doClaim(id) {
  const name = (document.getElementById('nameIn').value.trim() || 'neighbor');
  const r = await fetch('/post/claim', {
    method: 'POST',
    body: JSON.stringify({ id, name })
  });
  if (!r.ok) { alert('Claim failed: ' + await r.text()); return; }
  const data = await r.json();
  if (data && data.token) rememberClaimed(id, data.token);
  load();
}

async function doUnclaim(id) {
  // Either the claim token or the owner token works server-side.
  const token = getClaimed()[id] || getOwned()[id];
  if (!token) { alert('You did not claim this post.'); return; }
  const r = await fetch('/post/unclaim', {
    method: 'POST',
    body: JSON.stringify({ id, token })
  });
  if (!r.ok) { alert('Unclaim failed: ' + await r.text()); return; }
  forgetClaimed(id);
  load();
}

// ── Polls ─────────────────────────────────────────────────────────────────────
async function doVote(id, option) {
  if (getVoted()[id] !== undefined) return;  // already voted
  const r = await fetch('/poll/vote', {
    method: 'POST',
    body: JSON.stringify({ id, option })
  });
  if (!r.ok) { alert('Vote failed: ' + await r.text()); return; }
  rememberVoted(id, option);
  load();
}

// ── Reactions ─────────────────────────────────────────────────────────────────
async function doReact(id, type) {
  if (hasReacted(id, type)) return;  // already reacted this way
  const r = await fetch('/post/react', {
    method: 'POST',
    body: JSON.stringify({ id, type })
  });
  if (!r.ok) return;
  rememberReacted(id, type);
  // Optimistic local bump so the count moves immediately, then full reload
  const m = lastData.find(x => x.id === id);
  if (m) {
    m.reactions = m.reactions || [0,0,0,0];
    m.reactions[type] = (m.reactions[type] || 0) + 1;
    render(lastData);
  }
}

// ── Time-aware greeting + background tint ─────────────────────────────────────
function applyGreeting() {
  const h = new Date().getHours();
  let line, tint;
  if (h < 6)       { line = 'Quiet night at the Hub';  tint = 'night'; }
  else if (h < 11) { line = 'Good morning, neighbors'; tint = 'morning'; }
  else if (h < 17) { line = 'Afternoon at the Hub';    tint = 'afternoon'; }
  else if (h < 21) { line = 'Evening at the Hub';      tint = 'evening'; }
  else             { line = 'Quiet night at the Hub';  tint = 'night'; }
  const tagEl = document.getElementById('boardTagline');
  if (tagEl) tagEl.textContent = line;
  document.body.dataset.tint = tint;
}

// ── Presence indicator (uses /api/health which already has wifi_clients) ──────
async function updatePresence() {
  try {
    const r = await fetch('/api/health');
    const d = await r.json();
    const n = d.wifi_clients || 0;
    const el = document.getElementById('neighborsHere');
    if (!el) return;
    if (n <= 1) el.textContent = '🟢 just you here';
    else        el.textContent = '🟢 ' + n + ' neighbors here';
  } catch (_) { /* offline, leave previous text in place */ }
}

// ── Wave ──────────────────────────────────────────────────────────────────────
async function sendWave() {
  const btn = document.getElementById('waveBtn');
  if (btn) { btn.disabled = true; setTimeout(() => { btn.disabled = false; }, 2500); }
  try {
    await fetch('/wave', {
      method: 'POST',
      body: JSON.stringify({
        icon: '👋',
        from: localStorage.getItem('cn_name') || 'neighbor'
      })
    });
  } catch (_) {}
}

async function checkWaves() {
  if (document.hidden) return;  // don't poll when tab is backgrounded
  try {
    const r = await fetch('/wave/recent?since=' + lastWaveId);
    const data = await r.json();
    data.forEach(w => {
      if (w.id > lastWaveId) lastWaveId = w.id;
      animateWave(w.icon, w.from);
    });
  } catch (_) {}
}

function animateWave(icon, from) {
  const el = document.createElement('div');
  el.className = 'wave-float';
  // Random horizontal position (10..90vw) so multiple waves don't stack
  el.style.left = (10 + Math.random() * 80) + 'vw';
  el.innerHTML = `<span class="wave-icon">${esc(icon)}</span><span class="wave-from">${esc(from)}</span>`;
  document.body.appendChild(el);
  setTimeout(() => el.remove(), 3600);
}

// ── Name color picker ─────────────────────────────────────────────────────────
function buildColorPicker() {
  const host = document.getElementById('colorPicker');
  if (!host) return;
  NAME_COLORS.forEach((col, idx) => {
    const sw = document.createElement('span');
    sw.className = 'color-swatch' + (idx === myColor ? ' active' : '');
    // Index 0 is "default ink" - render it as the actual ink color
    sw.style.background = (idx === 0) ? '#2c2416' : col;
    sw.title = (idx === 0) ? 'default' : ('color ' + idx);
    sw.onclick = () => {
      myColor = idx;
      localStorage.setItem('cn_color', String(idx));
      host.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('active'));
      sw.classList.add('active');
    };
    host.appendChild(sw);
  });
}

// ── Board info ────────────────────────────────────────────────────────────────
function loadInfo() {
  fetch('/info').then(r => r.json()).then(d => {
    document.getElementById('boardTitle').textContent   = d.icon + '  ' + d.name;
    document.getElementById('boardRules').textContent   = d.rules;
    document.getElementById('boardFooter').textContent  = d.footer;
    document.getElementById('uptimeDisplay').textContent = d.uptime;
    // Tagline is owned by applyGreeting() now; the admin-set tagline is no
    // longer shown on the main board (it's still configurable for compatibility).
    applyGreeting();
  });
}

buildColorPicker();
applyGreeting();
updatePresence();

setInterval(load,            60000);
setInterval(loadInfo,        60000);
setInterval(applyGreeting,   60000);  // re-check the hour for greeting and tint
setInterval(updatePresence,  15000);  // refresh neighbor count
setInterval(checkWaves,       4000);  // poll for waves
load();
loadInfo();
checkBoardStatus();
</script>
</body>
</html>
)PAGE";

// ============================================================================
// Admin page - head (HTML + CSS + opening <script>)
// ============================================================================
const char ADMIN_PAGE_HEAD[] PROGMEM = R"PAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin — Community Hub</title>
<style>
:root {
  --bg: #ede8de; --surface: #faf8f3; --surface2: #f0ebe0;
  --border: #b0a080; --border-light: #d4c9b0;
  --ink: #2c2416; --ink-muted: #7a6a55;
  --accent-dark: #31502f; --accent-light: #c8ddc6;
  --danger: #c0392b; --radius: 6px;
}
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: var(--bg); color: var(--ink);
  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
  min-height: 100vh; display: flex; flex-direction: column;
}
.site-header { background: var(--accent-dark); padding: 14px 20px; border-bottom: 3px solid #1d3a1b; }
.site-title  { font-size: 18px; font-weight: bold; letter-spacing: 3px; color: #d8edcf; }
.site-sub    { font-size: 10px; letter-spacing: 2px; color: #8aad87; margin-top: 2px; }

#gate {
  max-width: 360px; margin: 80px auto 0; background: var(--surface);
  border: 1px solid var(--border); border-radius: var(--radius);
  padding: 28px 28px 24px; box-shadow: 3px 4px 0 rgba(44,36,22,.1);
}
#gate h2 {
  font-size: 13px; letter-spacing: 2px; text-transform: uppercase;
  color: var(--ink-muted); margin-bottom: 14px;
}
#gate input {
  width: 100%; background: var(--bg); border: 1px solid var(--border);
  border-radius: var(--radius); color: var(--ink); font-family: inherit;
  font-size: 14px; padding: 8px 12px; outline: none; margin-bottom: 10px;
}
#gate input:focus { border-color: var(--accent-dark); }
#gate button {
  width: 100%; background: var(--accent-dark); border: 2px solid #1d3a1b;
  border-radius: var(--radius); color: var(--accent-light); cursor: pointer;
  font-family: inherit; font-size: 12px; font-weight: bold; letter-spacing: 1px; padding: 9px;
}
#gate button:hover { background: #3d6438; }
#gate .error { font-size: 11px; color: var(--danger); margin-top: 8px; text-align: center; min-height: 16px; }

#panel { display: none; padding: 24px 20px; max-width: 640px; }
.section {
  background: var(--surface); border: 1px solid var(--border);
  border-radius: var(--radius); margin-bottom: 18px; overflow: hidden;
}
.section-head {
  background: var(--surface2); border-bottom: 1px solid var(--border-light);
  padding: 8px 14px; font-size: 12px; letter-spacing: 1px; font-weight: bolder;
  text-transform: uppercase; color: var(--ink-muted);
}
.section-body { padding: 14px; display: flex; flex-direction: column; gap: 10px; }
.row { display: flex; gap: 8px; align-items: flex-end; flex-wrap: wrap; }
.fld { display: flex; flex-direction: column; gap: 3px; }
.fld label { font-size: 10px; letter-spacing: 1px; text-transform: uppercase; color: var(--ink-muted); }
.fld input[type="text"], .fld input[type="number"], .fld input[type="datetime-local"] {
  background: var(--bg); border: 1px solid var(--border); border-radius: var(--radius);
  color: var(--ink); font-family: inherit; font-size: 13px; padding: 6px 10px;
  outline: none; width: 110px;
}
.fld input[type="text"], .fld input[type="datetime-local"] { width: 200px; }
.fld input[type="number"] { width: 80px; }
.fld input:focus { border-color: var(--accent-dark); }
.fld input[type="range"] { width: 140px; accent-color: var(--accent-dark); margin-top: 4px; }
.range-val { font-size: 12px; color: var(--ink); min-width: 28px; }

.btn {
  background: var(--accent-dark); border: 1px solid #1d3a1b; border-radius: var(--radius);
  color: var(--accent-light); cursor: pointer; font-family: inherit; font-size: 11px;
  font-weight: bold; letter-spacing: 1px; padding: 7px 14px; transition: background .15s;
  white-space: nowrap;
}
.btn:hover { background: #3d6438; }
.btn.danger { background: #8b1a10; border-color: #6b1208; }
.btn.danger:hover { background: var(--danger); }

.feedback { font-size: 11px; color: var(--accent-dark); min-height: 16px; }
textarea.restore-area {
  width: 100%; background: var(--bg); border: 1px solid var(--border);
  border-radius: var(--radius); color: var(--ink); font-family: inherit; font-size: 11px;
  padding: 8px; resize: vertical; height: 120px; outline: none;
}
textarea.restore-area:focus { border-color: var(--accent-dark); }

.post-list { display: flex; flex-direction: column; gap: 6px; margin-top: 4px; }
.post-row {
  display: flex; align-items: center; gap: 10px; background: var(--bg);
  border: 1px solid var(--border-light); border-radius: var(--radius);
  padding: 8px 10px; font-size: 12px;
}
.post-row-info { flex: 1; min-width: 0; }
.post-row-meta { font-size: 10px; color: var(--ink-muted); margin-bottom: 2px; }
.post-row-text { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; color: var(--ink); }
.post-empty { font-size: 12px; color: var(--ink-muted); font-style: italic; padding: 8px 0; }
</style>
</head>
<body>

<header class="site-header">
  <div class="site-title">COMMUNITY HUB</div>
  <div class="site-sub">ADMIN PANEL</div>
</header>

<div id="gate">
  <h2>Admin Access</h2>
  <input type="password" id="keyIn" placeholder="Enter admin key" autocomplete="off">
  <button onclick="tryLogin()">Unlock 🔑</button>
  <div class="error" id="gateErr"></div>
</div>

<div id="panel">

  <div class="section">
    <div class="section-head">Admin Key</div>
    <div class="section-body">
      <div class="row">
        <div class="fld">
          <label>New Key (min 4 characters)</label>
          <input type="password" id="newKey" maxlength="64" autocomplete="new-password"
                 placeholder="Enter new key" style="width:200px">
        </div>
        <div class="fld">
          <label>Confirm</label>
          <input type="password" id="newKeyConfirm" maxlength="64"
                 placeholder="Confirm new key" style="width:200px">
        </div>
        <button class="btn" onclick="doSetKey()">Change Key</button>
      </div>
      <div class="feedback" id="keyFb"></div>
    </div>
  </div>

  <div class="section">
    <div class="section-head">Board Identity</div>
    <div class="section-body">
      <div class="row">
        <div class="fld" style="flex:0 0 60px">
          <label>Icon</label>
          <input type="text" id="idIcon" maxlength="4" style="width:60px;font-size:20px;text-align:center;padding:4px 6px;">
        </div>
        <div class="fld" style="flex:1 1 200px">
          <label>Board Name</label>
          <input type="text" id="idName" maxlength="48" style="width:100%">
        </div>
      </div>
      <div class="fld" style="flex:1">
        <label>Tagline</label>
        <input type="text" id="idTagline" maxlength="80" style="width:100%">
      </div>
      <div class="fld" style="flex:1">
        <label>Rules</label>
        <input type="text" id="idRules" maxlength="80" style="width:100%">
      </div>
      <div class="fld" style="flex:1">
        <label>Footer</label>
        <input type="text" id="idFooter" maxlength="80" style="width:100%">
      </div>
      <div class="row">
        <button class="btn" onclick="doIdentity()">Save Identity</button>
      </div>
      <div class="feedback" id="idFb"></div>
    </div>
  </div>

  <div class="section">
    <div class="section-head">Set Time</div>
    <div class="section-body">
      <div class="row">
        <div class="fld">
          <label>Date &amp; Time</label>
          <input type="datetime-local" id="timeIn">
        </div>
        <button class="btn" onclick="doTime()">Set Time</button>
      </div>
      <div class="feedback" id="timeFb"></div>
    </div>
  </div>

  <div class="section">
  <div class="section-head">LED Settings</div>
  <div class="section-body">
    <div class="row">
      <div class="fld">
        <label>Day Brightness (0–100)</label>
        <input type="range" id="ledDayBr" min="0" max="100" value="80"
               oninput="document.getElementById('ledDayBrVal').textContent=this.value">
        <span class="range-val" id="ledDayBrVal">80</span>
      </div>
      <div class="fld">
        <label>Night Brightness (0–100)</label>
        <input type="range" id="ledNightBr" min="0" max="100" value="20"
               oninput="document.getElementById('ledNightBrVal').textContent=this.value">
        <span class="range-val" id="ledNightBrVal">20</span>
      </div>
    </div>
    <div class="row">
      <div class="fld">
        <label>Day Start (hour 0-23)</label>
        <input type="number" id="ledDayStart" min="0" max="23" value="7">
      </div>
      <div class="fld">
        <label>Night Start (hour 0-23)</label>
        <input type="number" id="ledNightStart" min="0" max="23" value="20">
      </div>
      <div class="fld">
        <label>GPIO Pin</label>
        <input type="number" id="ledPin" min="0" max="48" value="4" style="width:70px">
      </div>
    </div>
    <div class="row" style="gap:18px;margin-top:4px">
      <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
        <input type="checkbox" id="ledEnabled"  onchange="updateLedToggles()"> LED Enabled
      </label>
      <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
        <input type="checkbox" id="ledPulse"    onchange="updateLedToggles()"> Pulsing
      </label>
      <label style="display:flex;align-items:center;gap:6px;font-size:13px;cursor:pointer">
        <input type="checkbox" id="ledActivity"> Activity Mode
      </label>
    </div>
    <div class="row">
      <button class="btn" onclick="doLed()">Save LED</button>
    </div>
    <div class="feedback" id="ledFb"></div>
  </div>
</div>

  <div class="section">
    <div class="section-head">Manage Posts</div>
    <div class="section-body">
      <div class="row">
        <button class="btn" onclick="loadPostList()">Refresh List</button>
        <button class="btn danger" onclick="confirmClear()">Clear All Posts</button>
        <button class="btn"        onclick="doAction('/admin/flush',  'backupFb', false)">Force Save</button>
      </div>
      <div id="postListFb" class="feedback"></div>
      <div id="postList"></div>
    </div>
  </div>

  <div class="section">
    <div class="section-head">Backups</div>
    <div class="section-body">
      <div class="row">
        <button class="btn"        onclick="doAction('/admin/backup', 'backupFb', true)">Download Backup</button>
      </div>
      <div class="feedback" id="backupFb"></div>
      <div class="fld"><label>Restore Backup:</label></div>
      <textarea class="restore-area" id="restoreIn"
                placeholder="Paste backup JSON here…"></textarea>
      <div class="row">
        <button class="btn" onclick="doRestore()">Restore</button>
      </div>
      <div class="feedback" id="restoreFb"></div>
    </div>
  </div>

  <div class="section">
    <div class="section-head">Firmware Update (OTA)</div>
    <div class="section-body">
      <div class="fld">
        <label>Binary (.bin file)</label>
        <input type="file" id="otaFile" accept=".bin"
               style="font-size:12px;color:var(--ink)">
      </div>
      <div class="row" style="margin-top:4px">
        <button class="btn danger" onclick="doOTA()">Upload &amp; Reboot</button>
      </div>
      <div id="otaProgress" style="display:none;margin-top:8px">
        <div style="background:var(--border-light);border-radius:99px;height:8px;overflow:hidden">
          <div id="otaBar" style="background:var(--accent-dark);height:100%;width:0%;transition:width .2s"></div>
        </div>
        <div class="feedback" id="otaFb" style="margin-top:6px"></div>
      </div>
    </div>
  </div>

</div>

<script>
)PAGE";

// ============================================================================
// Admin page - tail (script body + closing tags)
// ============================================================================
const char ADMIN_PAGE_TAIL[] PROGMEM = R"PAGE(
function tryLogin() {
  const val = document.getElementById('keyIn').value;
  if (!val) return;
  fetch('/admin/auth', { method: 'POST', body: JSON.stringify({ key: val }) })
  .then(r => { if (!r.ok) throw new Error('forbidden'); return r.text(); })
  .then(token => {
    SESSION_TOKEN = token;
    document.getElementById('gate').style.display  = 'none';
    document.getElementById('panel').style.display = 'block';
    const now = new Date(); now.setSeconds(0, 0);
    document.getElementById('timeIn').value = now.toISOString().slice(0, 16);
    loadLedValues();
    loadPostList();
  })
  .catch(() => { document.getElementById('gateErr').textContent = 'Incorrect key.'; });
}
document.getElementById('keyIn').addEventListener('keydown', e => { if (e.key === 'Enter') tryLogin(); });

function api(path)  { return path + (path.includes('?') ? '&' : '?') + 'token=' + encodeURIComponent(SESSION_TOKEN); }
function apiFetch(url, options) {
  return fetch(url, options).then(r => {
    if (r.status === 403) {
      SESSION_TOKEN = '';
      document.getElementById('panel').style.display = 'none';
      document.getElementById('gate').style.display  = 'block';
      document.getElementById('gateErr').textContent = 'Session expired. Please log in again.';
      document.getElementById('keyIn').value = '';
      throw new Error('session expired');
    }
    return r;
  });
}
function fb(id, msg) {
  const el = document.getElementById(id);
  el.textContent = msg;
  setTimeout(() => { el.textContent = ''; }, 4000);
}

function doSetKey() {
  const n = document.getElementById('newKey').value;
  const c = document.getElementById('newKeyConfirm').value;
  if (n.length < 4)  { fb('keyFb', '✗ Key must be at least 4 characters'); return; }
  if (n !== c)       { fb('keyFb', '✗ Keys do not match'); return; }
  apiFetch(api('/admin/setkey') + '&newkey=' + encodeURIComponent(n))
    .then(r => r.text())
    .then(msg => { fb('keyFb', '✓ ' + msg); setTimeout(() => location.reload(), 1500); })
    .catch(() => fb('keyFb', '✗ Request failed'));
}

function doIdentity() {
  const params = new URLSearchParams({
    name:    document.getElementById('idName').value.trim(),
    icon:    document.getElementById('idIcon').value.trim(),
    tagline: document.getElementById('idTagline').value.trim(),
    rules:   document.getElementById('idRules').value.trim(),
    footer:  document.getElementById('idFooter').value.trim()
  });
  apiFetch(api('/admin/identity/set') + '&' + params.toString())
    .then(r => r.text()).then(msg => fb('idFb', '✓ ' + msg))
    .catch(() => fb('idFb', '✗ Request failed'));
}

function doTime() {
  const raw = document.getElementById('timeIn').value;
  if (!raw) { fb('timeFb', '✗ Please pick a date and time'); return; }
  const [date, time] = raw.split('T');
  const [y, m, d]   = date.split('-');
  const formatted   = d + m + y + '-' + time.replace(':', '');
  apiFetch(api('/admin/time') + '&time=' + formatted)
    .then(r => r.text()).then(msg => fb('timeFb', '✓ ' + msg))
    .catch(() => fb('timeFb', '✗ Request failed'));
}

function loadLedValues() {
  apiFetch(api('/admin/led/get'))
    .then(r => r.json())
    .then(d => {
      document.getElementById('ledDayBr').value            = d.day_br;
      document.getElementById('ledDayBrVal').textContent   = d.day_br;
      document.getElementById('ledNightBr').value          = d.night_br;
      document.getElementById('ledNightBrVal').textContent = d.night_br;
      document.getElementById('ledDayStart').value         = d.day_st;
      document.getElementById('ledNightStart').value       = d.night_st;
      document.getElementById('ledPin').value              = d.pin;
      document.getElementById('ledEnabled').checked        = d.enabled;
      document.getElementById('ledPulse').checked          = d.pulse;
      document.getElementById('ledActivity').checked       = d.activity;
      updateLedToggles();
    }).catch(() => {});
}
function updateLedToggles() {
  const enabled = document.getElementById('ledEnabled').checked;
  const pulse   = document.getElementById('ledPulse').checked;
  document.getElementById('ledPulse').disabled    = !enabled;
  document.getElementById('ledActivity').disabled = !enabled || !pulse;
}
function doLed() {
  const params = new URLSearchParams({
    day_br:   document.getElementById('ledDayBr').value,
    night_br: document.getElementById('ledNightBr').value,
    day_st:   document.getElementById('ledDayStart').value,
    night_st: document.getElementById('ledNightStart').value,
    pin:      document.getElementById('ledPin').value,
    enabled:  document.getElementById('ledEnabled').checked  ? '1' : '0',
    pulse:    document.getElementById('ledPulse').checked    ? '1' : '0',
    activity: document.getElementById('ledActivity').checked ? '1' : '0'
  });
  apiFetch(api('/admin/led/set') + '&' + params.toString())
    .then(r => r.text()).then(msg => fb('ledFb', '✓ ' + msg))
    .catch(() => fb('ledFb', '✗ Request failed'));
}

async function doAction(path, fbId, isDownload) {
  const r   = await apiFetch(api(path));
  const txt = await r.text();
  if (isDownload) {
    const a = document.createElement('a');
    a.href     = 'data:application/json,' + encodeURIComponent(txt);
    a.download = 'community_hub_backup.json';
    a.click();
    fb(fbId, '✓ Download started');
  } else { fb(fbId, '✓ ' + txt); }
}
function confirmClear() {
  if (!confirm('Delete ALL posts? This cannot be undone.')) return;
  apiFetch(api('/admin/clear'))
    .then(r => r.text()).then(msg => fb('backupFb', '✓ ' + msg))
    .catch(() => fb('backupFb', '✗ Failed'));
}

function esc(s) { return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function timeLeftShort(exp) {
  const left = exp - Math.floor(Date.now() / 1000);
  if (left <= 0)     return 'expired';
  if (left < 3600)   return Math.floor(left/60) + 'm';
  if (left < 86400)  return Math.floor(left/3600) + 'h';
  return Math.floor(left/86400) + 'd';
}
async function loadPostList() {
  const container = document.getElementById('postList');
  container.innerHTML = '<div class="post-empty">Loading…</div>';
  try {
    const r    = await fetch('/messages');
    const data = await r.json();
    if (data.length === 0) {
      container.innerHTML = '<div class="post-empty">No active posts.</div>';
      return;
    }
    container.innerHTML = '<div class="post-list">' +
      data.map(m => {
        const claim = m.claimed ? ' · claimed by ' + esc(m.claimedBy || 'someone') : '';
        return `
<div class="post-row" id="pr-${m.id}">
  <div class="post-row-info">
    <div class="post-row-meta">${esc(m.type)} · ${esc(m.author)} · ${timeLeftShort(m.expires)} left${claim}</div>
    <div class="post-row-text">${esc(m.text)}</div>
  </div>
  <button class="btn danger" onclick="deletePost(${m.id})">Delete</button>
</div>`; }).join('') + '</div>';
  } catch (_) {
    container.innerHTML = '<div class="post-empty">Failed to load posts.</div>';
  }
}
async function deletePost(id) {
  if (!confirm('Delete this post?')) return;
  const r = await apiFetch(api('/admin/delete/post') + '&id=' + id);
  if (r.ok) {
    const row = document.getElementById('pr-' + id);
    if (row) row.remove();
    fb('postListFb', '✓ Post deleted');
  } else { fb('postListFb', '✗ Delete failed'); }
}

function doOTA() {
  const fileInput = document.getElementById('otaFile');
  if (!fileInput.files.length) {
    fb('otaFb', '✗ Please select a .bin file first');
    document.getElementById('otaProgress').style.display = 'block';
    return;
  }
  const file = fileInput.files[0];
  if (!file.name.endsWith('.bin')) {
    fb('otaFb', '✗ File must be a .bin firmware file');
    document.getElementById('otaProgress').style.display = 'block';
    return;
  }
  if (!confirm('Upload ' + file.name + ' and reboot?\n\nDo not close this page until complete.')) return;
  document.getElementById('otaProgress').style.display = 'block';
  document.getElementById('otaBar').style.width = '0%';
  fb('otaFb', 'Uploading…');
  const formData = new FormData();
  formData.append('firmware', file);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', api('/admin/ota'));
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      document.getElementById('otaBar').style.width = pct + '%';
      fb('otaFb', 'Uploading… ' + pct + '%');
    }
  };
  xhr.onload = () => {
    document.getElementById('otaBar').style.width = '100%';
    if (xhr.status === 200) { fb('otaFb', '✓ ' + xhr.responseText + ' — connection will drop shortly'); }
    else { fb('otaFb', '✗ Upload failed: ' + xhr.responseText); }
  };
  xhr.onerror = () => fb('otaFb', '✗ Connection lost — board may be rebooting');
  xhr.send(formData);
}

function doRestore() {
  const body = document.getElementById('restoreIn').value.trim();
  if (!body) return;
  apiFetch(api('/admin/restore'), { method: 'POST', body })
    .then(r => r.text()).then(msg => fb('restoreFb', '✓ ' + msg))
    .catch(() => fb('restoreFb', '✗ Failed'));
}
</script>
</body>
</html>
)PAGE";
