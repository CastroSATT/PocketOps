#!/usr/bin/env python3
"""Split data/www/_extracted_script.js into ES modules + slim index.html."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "data" / "www"
SCRIPT = (ROOT / "_extracted_script.js").read_text(encoding="utf-8")
BODY = (ROOT / "_extracted_body.html").read_text(encoding="utf-8")
JS = ROOT / "js"
JS.mkdir(parents=True, exist_ok=True)


def between(start: str, end: str) -> str:
    a = SCRIPT.index(start)
    b = SCRIPT.index(end)
    return SCRIPT[a:b]


def export_fns(src: str, names) -> str:
    out = src
    for name in names:
        out = out.replace(f"async function {name}", f"export async function {name}", 1)
        # Avoid turning "export async function X" into "export async export function X"
        out = out.replace(f"\nfunction {name}", f"\nexport function {name}", 1)
        if out.startswith(f"function {name}"):
            out = "export " + out
    return out


# --- util.js ---
dir_meta = between("const DIR_META=[", "const CFG={")
util = '''export const statusEl = document.getElementById('status');
export const sdBadge = document.getElementById('sd-badge');
export const textEl = document.getElementById('text');
export const cfgMeta = document.getElementById('cfg-meta');
export const cfgMsg = document.getElementById('cfg-msg');
export const cfgFields = document.getElementById('cfg-fields');
export const dirMetaFields = document.getElementById('dir-meta-fields');
export const dirFolderList = document.getElementById('dir-folder-list');
export const sdExpose = document.getElementById('sd-expose');
export const sdInfo = document.getElementById('sd-info');

export const state = { dirty: false, currentCfg: 'network' };

export function setMsg(t, kind) {
  cfgMsg.textContent = t || '';
  cfgMsg.className = kind || '';
}
export function markDirty() {
  state.dirty = true;
  setMsg('Unsaved changes', 'warn');
}
export function clearDirty() { state.dirty = false; }

export function parseIni(text) {
  const kv = {};
  const paths = [];
  String(text || '').split(/\\r?\\n/).forEach(line => {
    const t = line.trim();
    if (!t || t.startsWith(';') || t.startsWith('#') || t.startsWith('[')) return;
    const eq = t.indexOf('=');
    if (eq > 0) kv[t.slice(0, eq).trim()] = t.slice(eq + 1).trim();
    else if (t.startsWith('/')) paths.push(t);
  });
  return { kv, paths };
}

export function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
}
export function cssEsc(s) {
  if (window.CSS && typeof CSS.escape === 'function') return CSS.escape(String(s || ''));
  return String(s || '').replace(/[^a-zA-Z0-9_-]/g, ch => '\\\\' + ch);
}

export function fieldHtml(f, value) {
  const type = f.type || 'text';
  const v = value || '';
  if (type === 'select') {
    const opts = (f.options || []).map(o => {
      const ov = o.v != null ? o.v : o;
      const ot = o.t != null ? o.t : ov;
      return `<option value="${esc(ov)}"${String(ov) === String(v) ? ' selected' : ''}>${esc(ot)}</option>`;
    }).join('');
    return `<div class="field"><label for="f-${f.key}">${f.label}</label>` +
      `<select id="f-${f.key}" data-key="${f.key}">${opts}</select></div>`;
  }
  if (type === 'checkbox') {
    const on = v === '1' || v === 'true' || v === 'on' || v === true;
    return `<div class="field"><label class="wifi-check" for="f-${f.key}">` +
      `<input id="f-${f.key}" data-key="${f.key}" type="checkbox"${on ? ' checked' : ''}/> ${esc(f.label)}</label></div>`;
  }
  if (type === 'password') {
    return `<div class="field"><label for="f-${f.key}">${f.label}</label>` +
      `<div class="pass-wrap">` +
      `<input id="f-${f.key}" data-key="${f.key}" type="password" value="${esc(v)}" autocomplete="off" spellcheck="false"/>` +
      `<button type="button" class="pass-toggle" data-pass="f-${f.key}" aria-label="Show password">Show</button>` +
      `</div></div>`;
  }
  return `<div class="field"><label for="f-${f.key}">${f.label}</label>` +
    `<input id="f-${f.key}" data-key="${f.key}" type="${type}" value="${esc(v)}"/></div>`;
}

export function bindPassToggles(root) {
  (root || document).querySelectorAll('.pass-toggle').forEach(btn => {
    if (btn.dataset.bound) return;
    btn.dataset.bound = '1';
    btn.addEventListener('click', () => {
      const id = btn.getAttribute('data-pass');
      const input = id ? document.getElementById(id) : null;
      if (!input) return;
      const show = input.type === 'password';
      input.type = show ? 'text' : 'password';
      btn.textContent = show ? 'Hide' : 'Show';
      btn.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
    });
  });
}

export function bindDirty(root) {
  root.querySelectorAll('input,textarea,select').forEach(el => {
    if (el.id === 'f-wifi_mode') return;
    el.addEventListener('input', markDirty);
    el.addEventListener('change', markDirty);
  });
  bindPassToggles(root);
}

export function grabField(k) {
  const el = document.getElementById('f-' + k);
  if (!el) return '';
  if (el.type === 'checkbox') return el.checked ? '1' : '0';
  return el.value.trim();
}

export function showView(mode) {
  const fields = mode === 'fields' || mode === 'network' || mode === 'info';
  document.getElementById('view-fields').classList.toggle('active', fields);
  document.getElementById('view-sd').classList.toggle('active', mode === 'sd');
  const save = document.getElementById('cfg-save');
  const reload = document.getElementById('cfg-reload');
  if (save) save.style.display = mode === 'info' ? 'none' : '';
  if (reload) reload.style.display = mode === 'info' ? 'none' : '';
}

function signalInfo(rssi) {
  const r = Number(rssi);
  if (!Number.isFinite(r)) return null;
  let bars = 1, label = 'Poor', cls = 'weak';
  if (r >= -50) { bars = 4; label = 'Excellent'; cls = ''; }
  else if (r >= -60) { bars = 3; label = 'Good'; cls = ''; }
  else if (r >= -70) { bars = 2; label = 'Fair'; cls = 'fair'; }
  else if (r >= -80) { bars = 1; label = 'Weak'; cls = 'weak'; }
  return { bars, label, cls, rssi: r };
}

export function signalBarsHtml(rssi) {
  const s = signalInfo(rssi);
  if (!s) return '';
  const bars = [1, 2, 3, 4].map(i => `<i class="${i <= s.bars ? 'on' : ''}"></i>`).join('');
  return `<span class="sig-bars ${s.cls}" title="${s.rssi} dBm">${bars}</span>`;
}

export function wifiLockHtml(secure) {
  if (secure) {
    return '<span class="wifi-lock" title="Password required" aria-label="Password required">' +
      '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="currentColor" d="M5 7V5a3 3 0 116 0v2h1.2c.44 0 .8.36.8.8v5.4c0 .44-.36.8-.8.8H3.8c-.44 0-.8-.36-.8-.8V7.8c0-.44.36-.8.8-.8H5zm1.5 0h3V5a1.5 1.5 0 10-3 0v2z"/></svg></span>';
  }
  return '<span class="wifi-lock open" title="Open network" aria-label="Open network">' +
    '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="currentColor" d="M10.5 7V4.8A2.8 2.8 0 007.7 2 2.8 2.8 0 005 4.5h1.4A1.4 1.4 0 017.7 3.4c.72 0 1.3.58 1.3 1.4V7H3.8c-.44 0-.8.36-.8.8v5.4c0 .44.36.8.8.8h8.4c.44 0 .8-.36.8-.8V7.8c0-.44-.36-.8-.8-.8h-1.7z"/></svg></span>';
}

export function signalText(rssi) {
  const s = signalInfo(rssi);
  if (!s) return '';
  return s.label + ' (' + s.rssi + ' dBm)';
}

export ''' + dir_meta.replace("const DIR_META=", "const DIR_META = ", 1)

(JS / "util.js").write_text(util if util.endswith("\n") else util + "\n", encoding="utf-8")

# --- wifi.js ---
wifi_state = between("const WIFI_JOIN_HTML=", "const UI_GROUPS=[")
wifi_fns = between("function applyWifiStatus(d){", "function renderGroups(groups,text){")
wifi = """import {
  state, clearDirty, setMsg, esc, cssEsc, grabField,
  signalBarsHtml, signalText, wifiLockHtml, cfgFields
} from './util.js';

"""
wifi += wifi_state
wifi += wifi_fns
wifi = wifi.replace("const WIFI_JOIN_HTML=", "export const WIFI_JOIN_HTML = ")
wifi = wifi.replace("let lastWifiScan=[];", "export let lastWifiScan = [];")
wifi = wifi.replace("let lastWifiProfiles=[];", "export let lastWifiProfiles = [];")
wifi = wifi.replace("let openWifiSsid='';", "export let openWifiSsid = '';")
wifi = wifi.replace("let wifiDraftPass='';", "export let wifiDraftPass = '';")
wifi = wifi.replace("let networkKvCache={};", "export let networkKvCache = {};")
wifi = wifi.replace("let lastConnectedSsid='';", "export let lastConnectedSsid = '';")
wifi = wifi.replace("let focusProfileSsid='';", "export let focusProfileSsid = '';")
wifi = export_fns(wifi, [
    "applyWifiStatus", "syncScanKnownFromProfiles", "refreshScanListIfIdle", "paintWifiScan",
    "setProfilesModalOpen", "updateProfileAutoJoin", "openSavedProfile", "refreshWifiProfiles",
    "refreshWifiStatus", "scanWifi", "connectWifi", "stopWifiPoll", "startWifiPoll",
    "bindWifiPanel",
])
wifi = wifi.replace("dirty=false;", "clearDirty();")
wifi = wifi.replace("if(currentCfg!=='network')return;", "if(state.currentCfg!=='network')return;")
wifi += """
export function resetJoinDraft() {
  openWifiSsid = '';
  wifiDraftPass = '';
}
export function setNetworkKvCache(kv) {
  networkKvCache = Object.assign({}, kv);
}
export function insertLiveWifiBlock() {
  const softTitle = [...cfgFields.querySelectorAll('.section-title')]
    .find(el => el.textContent === 'SoftAP hotspot');
  if (softTitle) softTitle.insertAdjacentHTML('beforebegin', WIFI_JOIN_HTML);
  else cfgFields.insertAdjacentHTML('beforeend', WIFI_JOIN_HTML);
}
"""
(JS / "wifi.js").write_text(wifi, encoding="utf-8")

# serializeNetwork / saveNetworkNow / bindWifiModeAutosave live in config.js (need NET_GROUPS)
wifi_save_fns = between("function serializeNetwork(){", "function folderRow(path){")

# --- sd.js ---
sd_body = between("function folderRow(path){", "document.getElementById('main-tabs')")
sd = """import {
  state, markDirty, clearDirty, setMsg, esc, parseIni, fieldHtml, bindDirty, grabField,
  sdBadge, sdExpose, sdInfo, dirMetaFields, dirFolderList, DIR_META
} from './util.js';

let sdBusy = false;
let sdIconExpose = false;
let sdIconActivity = false;
let sdFlashTimer = 0;

function paintSdBadge() {
  if (!sdIconExpose) { sdBadge.className = ''; return; }
  const flashOn = sdIconActivity && ((Date.now() / 90) & 1);
  sdBadge.className = 'on' + (flashOn ? ' flash' : '');
}
export function setSdIconState(expose, activity) {
  sdIconExpose = !!expose;
  sdIconActivity = !!(expose && activity);
  paintSdBadge();
}
async function pollSdIcon() {
  try {
    const r = await fetch('/api/sd/icon');
    if (!r.ok) return;
    const d = await r.json();
    setSdIconState(d.expose, d.activity);
  } catch (_) {}
}
export function startSdIconWatch() {
  pollSdIcon();
  setInterval(pollSdIcon, 180);
  if (sdFlashTimer) clearInterval(sdFlashTimer);
  sdFlashTimer = setInterval(paintSdBadge, 90);
}

"""
sd += export_fns(sd_body, ["folderRow", "applySdStatus", "refreshSdStatus", "renderSd", "serializeSd"])
sd = sd.replace("dirty=false;", "clearDirty();")
sd = sd.replace("if(dirty)", "if(state.dirty)")
sd = sd.replace("if(dirty&&", "if(state.dirty&&")
# Wrap document listeners (sd panel uses sdExpose ref, not getElementById('sd-expose'))
marker = "document.getElementById('dir-add')"
if marker in sd:
    i = sd.index(marker)
    sd = sd[:i] + "export function bindSdPanel() {\n" + sd[i:].rstrip() + "\n}\n"
(JS / "sd.js").write_text(sd, encoding="utf-8")

# --- config.js ---
net = between("const NET_GROUPS=[", "const BT_GROUPS=[")
bt_mouse = between("const BT_GROUPS=[", "const WIFI_JOIN_HTML=")
ui = between("const UI_GROUPS=[", "const DIR_META=[")
cfg = between("const CFG={", "let currentCfg='network';")
nl = "\\n"
config = f"""import {{
  state, clearDirty, setMsg, esc, parseIni, fieldHtml, bindDirty, grabField,
  showView, cfgMeta, cfgFields
}} from './util.js';
import {{
  stopWifiPoll, startWifiPoll, refreshWifiStatus, bindWifiPanel,
  resetJoinDraft, setNetworkKvCache, insertLiveWifiBlock,
  refreshWifiProfiles, scanWifi, networkKvCache
}} from './wifi.js';
import {{ renderSd, serializeSd, refreshSdStatus }} from './sd.js';
import {{ loadMouseCfg }} from './remote.js';

{net.replace('const NET_GROUPS=', 'export const NET_GROUPS = ', 1)}
{bt_mouse.replace('const BT_GROUPS=', 'export const BT_GROUPS = ', 1).replace('const MOUSE_GROUPS=', 'export const MOUSE_GROUPS = ', 1)}
{ui.replace('const UI_GROUPS=', 'export const UI_GROUPS = ', 1)}
{cfg.replace('const CFG=', 'export const CFG = ', 1)}

export function renderGroups(groups, text) {{
  const {{ kv }} = parseIni(text);
  if (!kv.splash && kv.product_name) kv.splash = kv.product_name;
  cfgFields.innerHTML = groups.map(g => {{
    const body = g.fields.map(f => fieldHtml(f, kv[f.key] || '')).join('');
    return `<p class="section-title">${{g.title}}</p>${{body}}`;
  }}).join('');
  bindDirty(cfgFields);
}}

export function serializeGroups(header, label, groups) {{
  const lines = ['; ' + header, '; ' + label];
  groups.forEach(g => g.fields.forEach(f => {{
    lines.push(f.key + '=' + grabField(f.key));
  }}));
  return lines.join('{nl}') + '{nl}';
}}

{export_fns(wifi_save_fns, ['serializeNetwork', 'saveNetworkNow', 'bindWifiModeAutosave']).replace('dirty=false;', 'clearDirty();')}

export function renderNetwork(text) {{
  const {{ kv }} = parseIni(text);
  setNetworkKvCache(kv);
  resetJoinDraft();
  renderGroups(NET_GROUPS, text);
  insertLiveWifiBlock();
  bindWifiPanel();
  bindWifiModeAutosave();
  refreshWifiStatus();
  refreshWifiProfiles();
  scanWifi({{ quiet: true }});
  startWifiPoll();
}}

export function bodyForSave(id) {{
  const info = CFG[id];
  if (info.mode === 'info') return '';
  if (info.mode === 'sd') return serializeSd();
  if (id === 'network') return serializeNetwork();
  return serializeGroups(info.header, info.label, info.groups);
}}

export async function loadCfg(id) {{
  const info = CFG[id];
  if (info.mode !== 'network') stopWifiPoll();
  cfgMeta.textContent = info.label + (info.note ? ' - ' + info.note : '');
  showView(info.mode);
  if (info.mode === 'info') {{
    cfgFields.innerHTML = '<p class="hint" style="text-align:left">' + esc(info.note || '') + '</p>';
    clearDirty();
    setMsg('');
    return;
  }}
  setMsg('Loading…');
  try {{
    const r = await fetch('/api/config/' + id);
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    if (info.mode === 'sd') renderSd(t);
    else if (info.mode === 'network') renderNetwork(t);
    else renderGroups(info.groups, t);
    clearDirty();
    setMsg('Loaded', 'ok');
  }} catch (err) {{
    if (info.mode === 'fields' || info.mode === 'network') cfgFields.innerHTML = '';
    setMsg(String(err.message || err), 'err');
  }}
}}

export async function saveCfg(id) {{
  const info = CFG[id];
  if (!info || info.mode === 'info') return;
  setMsg('Saving…');
  try {{
    const r = await fetch('/api/config/' + id, {{
      method: 'POST',
      headers: {{ 'Content-Type': 'text/plain; charset=utf-8' }},
      body: bodyForSave(id)
    }});
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    clearDirty();
    setMsg(t || 'Saved', 'ok');
    if (id === 'sd') await refreshSdStatus();
    if (id === 'network') await refreshWifiStatus();
    if (id === 'mouse') loadMouseCfg();
  }} catch (err) {{
    setMsg(String(err.message || err), 'err');
  }}
}}
"""
(JS / "config.js").write_text(config, encoding="utf-8")

# --- remote.js ---
remote_src = SCRIPT.split("/* -- Remote HID -- */", 1)[1]
remote_src = remote_src.replace(
    "ws=new WebSocket(`ws://${location.hostname}:__PO_WS_PORT__/`);",
    "const port = (window.__PO__ && window.__PO__.wsPort) || 80;\n"
    "  ws = new WebSocket(`ws://${location.hostname}:${port}/`);",
)
remote_src = remote_src.replace("loadMouseCfg();\nconnect();\nstartSdIconWatch();", "")
remote_src = remote_src.replace("text.focus()", "textEl.focus()")
remote_src = remote_src.replace("text.addEventListener", "textEl.addEventListener")
remote_src = remote_src.replace("text.value", "textEl.value")
remote_src = remote_src.replace("async function loadMouseCfg", "export async function loadMouseCfg", 1)
remote_src = remote_src.replace("function connect()", "export function connect()", 1)

pad_mark = "const pad=document.getElementById('pad');"
if pad_mark not in remote_src:
    raise SystemExit("pad marker missing")
pre, rest = remote_src.split(pad_mark, 1)
remote = (
    "import { parseIni, statusEl, textEl } from './util.js';\n\n"
    + pre
    + "export function startRemote() {\n"
    + pad_mark
    + rest
)
if not remote.rstrip().endswith("}"):
    remote = remote.rstrip() + "\n}\n"
else:
    # startRemote opens a brace; ensure closed
    remote = remote.rstrip() + "\n}\n"
(JS / "remote.js").write_text(remote, encoding="utf-8")

# --- app.js ---
app = """import { state } from './util.js';
import { loadCfg, saveCfg } from './config.js';
import { startSdIconWatch, bindSdPanel } from './sd.js';
import { startRemote, connect, loadMouseCfg } from './remote.js';

document.getElementById('main-tabs').addEventListener('click', e => {
  const btn = e.target.closest('button[data-tab]');
  if (!btn) return;
  document.querySelectorAll('#main-tabs button').forEach(b => b.classList.toggle('active', b === btn));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + btn.dataset.tab));
  if (btn.dataset.tab === 'config') loadCfg(state.currentCfg);
});

document.getElementById('cfg-subtabs').addEventListener('click', e => {
  const btn = e.target.closest('button[data-cfg]');
  if (!btn) return;
  if (state.dirty && !confirm('Discard unsaved changes?')) return;
  state.currentCfg = btn.dataset.cfg;
  document.querySelectorAll('#cfg-subtabs button').forEach(b => b.classList.toggle('active', b === btn));
  loadCfg(state.currentCfg);
});

document.getElementById('cfg-reload').addEventListener('click', () => {
  if (state.dirty && !confirm('Reload and discard edits?')) return;
  loadCfg(state.currentCfg);
});

document.getElementById('cfg-save').addEventListener('click', () => saveCfg(state.currentCfg));

bindSdPanel();
loadMouseCfg();
startRemote();
connect();
startSdIconWatch();
"""
(JS / "app.js").write_text(app, encoding="utf-8")

index = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"/>
<title>__PO_REMOTE_TITLE__</title>
<link rel="stylesheet" href="/css/app.css"/>
<script>window.__PO__=window.__PO__||{{}};window.__PO__.wsPort=__PO_WS_PORT__;</script>
</head>
<body>
{BODY}
<script type="module" src="/js/app.js"></script>
</body>
</html>
"""
(ROOT / "index.html").write_text(index, encoding="utf-8")

for p in sorted(JS.glob("*.js")):
    print(f"{p.name}: {len(p.read_text().splitlines())} lines")
print("index.html:", len(index.splitlines()), "lines")
