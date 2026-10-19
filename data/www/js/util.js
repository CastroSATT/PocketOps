export const statusEl = document.getElementById('status');
export const wifiModeBadge = document.getElementById('wifi-mode-badge');
export const sdBadge = document.getElementById('sd-badge');
export const textEl = document.getElementById('text');

/**
 * Header badge next to SD: AP / AP+ / STA — only when that path is live.
 * opts.apUp — SoftAP up; opts.staUp — joined to a LAN AP.
 */
export function setWifiModeBadge(mode, opts) {
  if (!wifiModeBadge) return;
  const m = (mode === 'apsta' || mode === 'sta') ? mode : 'ap';
  const apUp = !!(opts && opts.apUp);
  const staUp = !!(opts && opts.staUp);
  let tag = '';
  let colorMode = m;
  // Live paths only: both up → AP+ (alt colours); SoftAP alone → AP blue; join alone → STA amber.
  if (m === 'apsta') {
    if (apUp && staUp) { tag = 'AP+'; colorMode = 'apsta'; }
    else if (apUp) { tag = 'AP'; colorMode = 'ap'; }
    else if (staUp) { tag = 'STA'; colorMode = 'sta'; }
  } else if (m === 'ap' && apUp) {
    tag = 'AP';
    colorMode = 'ap';
  } else if (m === 'sta' && staUp) {
    tag = 'STA';
    colorMode = 'sta';
  }
  wifiModeBadge.dataset.mode = colorMode;
  wifiModeBadge.textContent = tag || '—';
  wifiModeBadge.title =
    colorMode === 'apsta' ? 'Wi-Fi mode: SoftAP + join' :
    colorMode === 'sta' ? 'Wi-Fi mode: Join network only' :
    'Wi-Fi mode: SoftAP only (hotspot)';
  wifiModeBadge.classList.toggle('on', !!tag);
}
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
  String(text || '').split(/\r?\n/).forEach(line => {
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
  return String(s || '').replace(/[^a-zA-Z0-9_-]/g, ch => '\\' + ch);
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
  if (type === 'range') {
    const min = f.min != null ? f.min : 0;
    const max = f.max != null ? f.max : 100;
    const step = f.step != null ? f.step : 1;
    const cur = v === '' ? String(min) : String(v);
    return `<div class="field"><label for="f-${f.key}">${f.label} ` +
      `<span class="range-val" id="v-${f.key}">${esc(cur)}</span></label>` +
      `<input id="f-${f.key}" data-key="${f.key}" class="range" type="range" min="${min}" max="${max}" step="${step}" value="${esc(cur)}"/></div>`;
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
  root.querySelectorAll('input.range').forEach(el => {
    const key = el.dataset.key;
    const val = key ? document.getElementById('v-' + key) : null;
    const sync = () => { if (val) val.textContent = el.value; };
    el.addEventListener('input', sync);
    sync();
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
  const backupView = document.getElementById('view-backup');
  if (backupView) backupView.classList.toggle('active', mode === 'backup');
  const save = document.getElementById('cfg-save');
  const reload = document.getElementById('cfg-reload');
  const hideActions = mode === 'info' || mode === 'backup';
  if (save) save.style.display = hideActions ? 'none' : '';
  // Network has its own Wi-Fi scan refresh — hide generic Reload there.
  if (reload) reload.style.display = (hideActions || mode === 'network') ? 'none' : '';
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

/** Floppy = saved Wi-Fi profile on this SSID. */
export function wifiSavedHtml() {
  return '<span class="wifi-tag saved" title="Saved profile" aria-label="Saved profile">' +
    '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="currentColor" d="M2.2 1.5h9.1L14 4.2v9.3c0 .55-.45 1-1 1H3c-.55 0-1-.45-1-1V2.5c0-.55.45-1 1-1h.2zm1.3 1v3.8h7.3V2.5H3.5zM4 8.2v5.3h8V8.2H4zm1.3 1.1h2.4v2.6H5.3V9.3z"/></svg></span>';
}

/** Circular arrows = auto-join enabled. */
export function wifiAutoHtml() {
  return '<span class="wifi-tag auto" title="Auto-join" aria-label="Auto-join">' +
    '<svg viewBox="0 0 16 16" aria-hidden="true"><path fill="currentColor" d="M8 2.2a5.8 5.8 0 00-5.5 4H1.1L3.4 9l2.3-2.8H4.2A4.3 4.3 0 118 12.3v1.5A5.8 5.8 0 008 2.2zm3.2 1.6l-2.3 2.8h1.5A4.3 4.3 0 118 3.7V2.2a5.8 5.8 0 013.2 1.6z"/></svg></span>';
}

export function signalText(rssi) {
  const s = signalInfo(rssi);
  if (!s) return '';
  return s.label + ' (' + s.rssi + ' dBm)';
}

export const DIR_META = [
  {key:'volume_label',label:'Drive name after Format (max 11 chars)'},
  {key:'nvs_namespace',label:'NVS namespace'}
];
