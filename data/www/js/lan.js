import { esc, signalBarsHtml, signalText } from './util.js';

let hosts = [];
let selectedIp = '';
let selectedMac = '';
let pollTimer = null;
let watchTimer = null;
let lanActive = false;
let info = null;
let lastJobKind = '';

const PORT_SCHEME = {
  80: 'http', 8080: 'http', 8000: 'http', 8008: 'http', 8888: 'http',
  443: 'https', 8443: 'https',
  21: 'ftp', 554: 'rtsp'
};

function $(id) { return document.getElementById(id); }

function setBusyUi(busy, text) {
  const b = $('lan-busy');
  const wrap = $('lan-progress-wrap');
  if (b) {
    b.hidden = !busy;
    if (text) b.textContent = text;
  }
  if (wrap) wrap.hidden = !busy;
  const disable = !!busy;
  ['lan-scan', 'lan-ports-quick', 'lan-ports-common', 'lan-ports-full', 'lan-ports-custom-btn',
    'lan-ping', 'lan-traceroute', 'lan-wol', 'lan-mdns'].forEach(id => {
    const el = $(id);
    if (el) el.disabled = disable;
  });
}

function setProgress(pct) {
  const fill = $('lan-progress');
  if (fill) fill.style.width = Math.max(0, Math.min(100, pct || 0)) + '%';
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const ct = r.headers.get('content-type') || '';
  if (ct.includes('application/json')) {
    const d = await r.json();
    if (!r.ok) throw new Error(d.error || r.statusText);
    return d;
  }
  if (!r.ok) throw new Error(await r.text());
  return r;
}

function toolOut(t) {
  const el = $('lan-tool-out');
  if (el) el.textContent = t || '';
}

function deepLink(ip, port) {
  const scheme = PORT_SCHEME[port];
  if (!scheme) return null;
  if ((scheme === 'http' && port === 80) || (scheme === 'https' && port === 443)) {
    return scheme + '://' + ip + '/';
  }
  return scheme + '://' + ip + ':' + port + '/';
}

async function copyText(t) {
  try {
    await navigator.clipboard.writeText(t);
    toolOut('Copied: ' + t);
  } catch (_) {
    toolOut('Copy failed');
  }
}

function hideCtx() {
  const m = $('lan-ctx');
  if (m) m.hidden = true;
}

function showCtx(x, y, items) {
  const m = $('lan-ctx');
  if (!m) return;
  m.innerHTML = items.map((it, i) =>
    `<button type="button" data-i="${i}">${esc(it.label)}</button>`).join('');
  m.hidden = false;
  const pad = 8;
  const rect = m.getBoundingClientRect();
  let left = x;
  let top = y;
  if (left + rect.width > window.innerWidth - pad) left = window.innerWidth - rect.width - pad;
  if (top + rect.height > window.innerHeight - pad) top = window.innerHeight - rect.height - pad;
  m.style.left = Math.max(pad, left) + 'px';
  m.style.top = Math.max(pad, top) + 'px';
  m.onclick = e => {
    const btn = e.target.closest('button[data-i]');
    if (!btn) return;
    const it = items[+btn.dataset.i];
    hideCtx();
    if (it && it.fn) it.fn();
  };
}

function hostMenu(h) {
  return [
    { label: 'Port scan Quick', fn: () => startPorts(h.ip, 'quick') },
    { label: 'Port scan Common', fn: () => startPorts(h.ip, 'common') },
    { label: 'Port scan Full', fn: () => startPorts(h.ip, 'full') },
    { label: 'Ping', fn: () => startPing(h.ip) },
    { label: 'Traceroute', fn: () => startTraceroute(h.ip) },
    { label: 'Wake-on-LAN', fn: () => wol(h.mac) },
    { label: 'Copy IP', fn: () => copyText(h.ip) },
    { label: 'Copy MAC', fn: () => copyText(h.mac || '') },
    { label: 'Edit note', fn: () => editNote(h) },
    { label: 'Use as target', fn: () => { selectedIp = h.ip; selectedMac = h.mac || ''; const tip = $('lan-tool-ip'); if (tip) tip.value = h.ip; } }
  ];
}

function bindLongPress(el, fn) {
  let t = null;
  el.addEventListener('touchstart', e => {
    if (e.touches.length !== 1) return;
    const touch = e.touches[0];
    t = setTimeout(() => { t = null; fn(touch.clientX, touch.clientY); }, 500);
  }, { passive: true });
  el.addEventListener('touchend', () => { if (t) clearTimeout(t); t = null; });
  el.addEventListener('touchmove', () => { if (t) clearTimeout(t); t = null; });
}

function renderHosts() {
  const box = $('lan-host-table');
  if (!box) return;
  const q = (($('lan-host-filter') && $('lan-host-filter').value) || '').toLowerCase();
  const list = hosts.filter(h => {
    if (!q) return true;
    return (h.ip + ' ' + (h.mac || '') + ' ' + (h.hostname || '') + ' ' + (h.vendor || '') + ' ' + (h.notes || '')).toLowerCase().includes(q);
  });
  if (!list.length) {
    box.innerHTML = '<p class="hint" style="text-align:left">No hosts yet — Scan or wait for profile auto-load.</p>';
    return;
  }
  box.innerHTML = list.map(h => {
    const cls = ['lan-host-row'];
    if (h.stale) cls.push('stale');
    if (h.new) cls.push('is-new');
    if (h.missing) cls.push('missing');
    if (h.ip === selectedIp) cls.push('selected');
    return `<div class="${cls.join(' ')}" data-ip="${esc(h.ip)}" data-mac="${esc(h.mac || '')}">
      <div class="lan-host-main">
        <strong>${esc(h.ip)}</strong>
        <span class="lan-host-meta">${esc(h.mac || '—')}${h.vendor ? ' · ' + esc(h.vendor) : ''}</span>
        <span class="lan-host-meta">${h.rtt_ms >= 0 ? h.rtt_ms + ' ms' : ''}${h.hostname ? ' · ' + esc(h.hostname) : ''}</span>
        ${h.notes ? `<span class="lan-note">${esc(h.notes)}</span>` : ''}
        ${h.open_ports ? `<span class="lan-ports-tag">${esc(h.open_ports)}</span>` : ''}
      </div>
    </div>`;
  }).join('');

  box.querySelectorAll('.lan-host-row').forEach(row => {
    const ip = row.dataset.ip;
    const mac = row.dataset.mac;
    const h = hosts.find(x => x.ip === ip) || { ip, mac };
    row.addEventListener('click', () => {
      selectedIp = ip;
      selectedMac = mac;
      const tip = $('lan-tool-ip');
      if (tip) tip.value = ip;
      renderHosts();
    });
    row.addEventListener('contextmenu', e => {
      e.preventDefault();
      selectedIp = ip;
      selectedMac = mac;
      showCtx(e.clientX, e.clientY, hostMenu(h));
    });
    bindLongPress(row, (x, y) => {
      selectedIp = ip;
      selectedMac = mac;
      showCtx(x, y, hostMenu(h));
    });
  });
}

function renderPorts(d) {
  const box = $('lan-port-results');
  if (!box) return;
  const open = (d && d.open) || [];
  const ip = (d && d.ip) || selectedIp || (($('lan-tool-ip') && $('lan-tool-ip').value) || '');
  if (!open.length) {
    box.innerHTML = d && d.busy ? '<p class="hint">Scanning ports…</p>' : '';
    return;
  }
  box.innerHTML = open.map(o => {
    const url = deepLink(ip, o.port);
    const label = o.port + (o.service ? ' ' + o.service : '') +
      (o.http_title ? ' — ' + o.http_title : '') +
      (o.banner ? ' — ' + o.banner : '');
    if (url) {
      return `<a class="lan-port-link" href="${esc(url)}" target="_blank" rel="noopener noreferrer" data-port="${o.port}">${esc(label)}</a>`;
    }
    return `<button type="button" class="lan-port-link" data-port="${o.port}" data-copy="${esc(ip + ':' + o.port)}">${esc(label)} · copy</button>`;
  }).join('');
  box.querySelectorAll('[data-copy]').forEach(el => {
    el.addEventListener('click', () => copyText(el.dataset.copy));
  });
  box.querySelectorAll('.lan-port-link').forEach(el => {
    el.addEventListener('contextmenu', e => {
      e.preventDefault();
      const port = +el.dataset.port;
      const url = deepLink(ip, port);
      const items = [];
      if (url) items.push({ label: 'Open in browser', fn: () => window.open(url, '_blank', 'noopener,noreferrer') });
      items.push({ label: 'Open as http…', fn: () => window.open('http://' + ip + ':' + port + '/', '_blank', 'noopener,noreferrer') });
      items.push({ label: 'Copy ip:port', fn: () => copyText(ip + ':' + port) });
      showCtx(e.clientX, e.clientY, items);
    });
  });
}

async function refreshInfo() {
  try {
    info = await api('/api/lan/info');
    if ($('lan-ssid')) $('lan-ssid').textContent = info.ssid || '—';
    if ($('lan-ip')) $('lan-ip').textContent = info.ip || '—';
    if ($('lan-mask')) $('lan-mask').textContent = info.mask || '—';
    if ($('lan-gw')) $('lan-gw').textContent = info.gateway || '—';
    if ($('lan-dns')) $('lan-dns').textContent = [info.dns1, info.dns2].filter(Boolean).join(' · ') || '—';
    if ($('lan-bssid')) $('lan-bssid').textContent = info.bssid || '—';
    if ($('lan-hostname')) $('lan-hostname').textContent = info.hostname || '—';
    if ($('lan-from') && !$('lan-from').dataset.touched) $('lan-from').value = info.from || '';
    if ($('lan-to') && !$('lan-to').dataset.touched) $('lan-to').value = info.to || '';
    if ($('lan-profile-status')) {
      $('lan-profile-status').textContent = info.profile
        ? ('Loaded · ' + (info.profiles || 0) + '/100')
        : ('None · ' + (info.profiles || 0) + '/100');
    }
  } catch (_) {}
}

async function autoload() {
  try {
    const d = await api('/api/lan/profiles/autoload');
    hosts = d.hosts || [];
    renderHosts();
  } catch (_) {}
}

async function pollJob() {
  if (!lanActive) return;
  try {
    const st = await api('/api/lan/status');
    setBusyUi(!!st.busy, (st.kind || 'job') + '… ' + (st.progress || 0) + '%');
    setProgress(st.progress || 0);
    if (st.kind === 'discover' || lastJobKind === 'discover') {
      const d = await api('/api/lan/discover');
      hosts = d.hosts || hosts;
      renderHosts();
    }
    if (st.kind === 'ports' || lastJobKind === 'ports') {
      const d = await api('/api/lan/ports');
      renderPorts(d);
      if (d.done) toolOut('Port scan done — ' + ((d.open && d.open.length) || 0) + ' open');
    }
    if (st.kind === 'ping') {
      const d = await api('/api/lan/ping');
      if (!d.busy) {
        toolOut('Ping: ' + d.recv + '/' + d.sent + '  min/avg/max ' + d.min + '/' + d.avg + '/' + d.max + ' ms  loss ' + d.loss_pct + '%');
      }
    }
    if (st.kind === 'traceroute') {
      const d = await api('/api/lan/traceroute');
      if (d.hops) toolOut(d.hops.map((h, i) => (i + 1) + '  ' + h.ip + (h.rtt_ms >= 0 ? '  ' + h.rtt_ms + 'ms' : '')).join('\n'));
    }
    if (st.kind === 'mdns') {
      const d = await api('/api/lan/mdns');
      if (d.services) toolOut(d.services.map(s => s.name + '  ' + s.ip + ':' + s.port + '  ' + s.type).join('\n') || 'No mDNS names from host list');
    }
    if (!st.busy) {
      lastJobKind = '';
      if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
      setBusyUi(false);
      const d = await api('/api/lan/hosts');
      hosts = d.hosts || hosts;
      renderHosts();
      refreshInfo();
    }
  } catch (_) {}
}

function startPolling(kind) {
  lastJobKind = kind || '';
  setBusyUi(true, 'Starting…');
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(pollJob, 800);
  pollJob();
}

async function cancelJob() {
  try { await api('/api/lan/cancel', { method: 'POST', body: '{}' }); } catch (_) {}
  setBusyUi(false);
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}

async function startDiscover() {
  const from = $('lan-from').value.trim();
  const to = $('lan-to').value.trim();
  await api('/api/lan/discover', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ from, to })
  });
  startPolling('discover');
}

async function startPorts(ip, mode, ports) {
  ip = ip || ($('lan-tool-ip') && $('lan-tool-ip').value.trim());
  if (!ip) { toolOut('Set a target IP'); return; }
  $('lan-tool-ip').value = ip;
  selectedIp = ip;
  await api('/api/lan/ports', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ip, mode: mode || 'common', ports: ports || '' })
  });
  startPolling('ports');
}

async function startPing(ip) {
  ip = ip || ($('lan-tool-ip') && $('lan-tool-ip').value.trim());
  if (!ip) { toolOut('Set a target IP'); return; }
  await api('/api/lan/ping', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ip, count: 4 })
  });
  startPolling('ping');
}

async function startTraceroute(ip) {
  ip = ip || ($('lan-tool-ip') && $('lan-tool-ip').value.trim());
  if (!ip) { toolOut('Set a target IP'); return; }
  await api('/api/lan/traceroute', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ip, max_hops: 8 })
  });
  startPolling('traceroute');
}

async function wol(mac) {
  mac = mac || selectedMac;
  if (!mac) { toolOut('No MAC for WOL'); return; }
  await api('/api/lan/wol', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac })
  });
  toolOut('Wake packet sent to ' + mac);
}

async function editNote(h) {
  const note = prompt('Note for ' + (h.ip || '') + ' (' + (h.mac || '') + ')', h.notes || '');
  if (note === null) return;
  await api('/api/lan/profiles/note', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac: h.mac, note })
  });
  h.notes = note;
  renderHosts();
}

export async function enterLanPanel() {
  lanActive = true;
  await refreshInfo();
  await autoload();
}

export async function leaveLanPanel() {
  lanActive = false;
  await cancelJob();
  const w = $('lan-watch');
  if (w && w.checked) {
    w.checked = false;
    try {
      await api('/api/lan/watch', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: false, interval_min: 5 })
      });
    } catch (_) {}
  }
  if (watchTimer) { clearInterval(watchTimer); watchTimer = null; }
}

export function bindLanPanel() {
  document.addEventListener('click', e => {
    if (!$('lan-ctx') || $('lan-ctx').hidden) return;
    if (!e.target.closest('#lan-ctx')) hideCtx();
  });
  document.addEventListener('keydown', e => { if (e.key === 'Escape') hideCtx(); });

  const from = $('lan-from'), to = $('lan-to');
  if (from) from.addEventListener('input', () => { from.dataset.touched = '1'; });
  if (to) to.addEventListener('input', () => { to.dataset.touched = '1'; });

  $('lan-scan') && $('lan-scan').addEventListener('click', () => startDiscover().catch(e => toolOut(String(e.message || e))));
  $('lan-cancel') && $('lan-cancel').addEventListener('click', () => cancelJob());
  $('lan-copy-all') && $('lan-copy-all').addEventListener('click', () => {
    const lines = hosts.map(h => [h.ip, h.mac, h.hostname, h.vendor, h.rtt_ms, h.open_ports, h.notes].join('\t')).join('\n');
    copyText(lines || '');
  });
  $('lan-host-filter') && $('lan-host-filter').addEventListener('input', renderHosts);

  $('lan-ping') && $('lan-ping').addEventListener('click', () => startPing().catch(e => toolOut(String(e.message || e))));
  $('lan-ports-quick') && $('lan-ports-quick').addEventListener('click', () => startPorts(null, 'quick').catch(e => toolOut(String(e.message || e))));
  $('lan-ports-common') && $('lan-ports-common').addEventListener('click', () => startPorts(null, 'common').catch(e => toolOut(String(e.message || e))));
  $('lan-ports-full') && $('lan-ports-full').addEventListener('click', () => {
    if (!confirm('Full 1–65535 scan can take a long time on the dongle. Continue?')) return;
    startPorts(null, 'full').catch(e => toolOut(String(e.message || e)));
  });
  $('lan-ports-custom-btn') && $('lan-ports-custom-btn').addEventListener('click', () => {
    const p = $('lan-ports-custom').value.trim();
    startPorts(null, 'custom', p).catch(e => toolOut(String(e.message || e)));
  });
  $('lan-traceroute') && $('lan-traceroute').addEventListener('click', () => startTraceroute().catch(e => toolOut(String(e.message || e))));
  $('lan-wol') && $('lan-wol').addEventListener('click', () => wol().catch(e => toolOut(String(e.message || e))));
  $('lan-mdns') && $('lan-mdns').addEventListener('click', async () => {
    await api('/api/lan/mdns', { method: 'POST', body: '{}' });
    startPolling('mdns');
  });
  $('lan-dns-fwd') && $('lan-dns-fwd').addEventListener('click', async () => {
    const q = $('lan-dns-q').value.trim();
    const d = await api('/api/lan/dns', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ q, type: 'forward' }) });
    toolOut((d.result || '(none)') + ' ← ' + q);
  });
  $('lan-dns-rev') && $('lan-dns-rev').addEventListener('click', async () => {
    const q = $('lan-dns-q').value.trim();
    const d = await api('/api/lan/dns', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ q, type: 'reverse' }) });
    toolOut((d.result || '(none)') + ' ← ' + q);
  });

  $('lan-profile-save') && $('lan-profile-save').addEventListener('click', async () => {
    await api('/api/lan/profiles/save', { method: 'POST', body: '{}' });
    toolOut('Profile saved');
    refreshInfo();
  });
  $('lan-profile-clear') && $('lan-profile-clear').addEventListener('click', async () => {
    if (!confirm('Wipe the saved profile for this Wi‑Fi (AP MAC)?')) return;
    await api('/api/lan/profiles/clear', { method: 'POST', body: '{}' });
    hosts = [];
    renderHosts();
    toolOut('Profile cleared');
    refreshInfo();
  });

  $('lan-watch') && $('lan-watch').addEventListener('change', async () => {
    const on = $('lan-watch').checked;
    const mins = parseInt($('lan-watch-min').value, 10) || 5;
    await api('/api/lan/watch', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled: on, interval_min: mins })
    });
    toolOut(on ? 'Watch on' : 'Watch off');
  });

  const modal = $('lan-export-modal');
  const openModal = async () => {
    if (!modal) return;
    modal.classList.add('open');
    modal.setAttribute('aria-hidden', 'false');
    const msg = $('lan-export-msg');
    const sdBtn = $('lan-export-sd');
    try {
      const r = await fetch('/api/sd/status');
      const d = await r.json();
      const ok = !!(d && d.present);
      if (sdBtn) {
        sdBtn.disabled = !ok;
        sdBtn.title = ok ? 'Save under /Logs/Lan/' : 'No SD card';
      }
      if (msg) msg.textContent = ok ? 'SD ready' : 'No SD card — download still works';
    } catch (_) {
      if (sdBtn) sdBtn.disabled = true;
    }
  };
  const closeModal = () => {
    if (!modal) return;
    modal.classList.remove('open');
    modal.setAttribute('aria-hidden', 'true');
  };
  $('lan-export') && $('lan-export').addEventListener('click', openModal);
  $('lan-export-close') && $('lan-export-close').addEventListener('click', closeModal);
  $('lan-export-dl') && $('lan-export-dl').addEventListener('click', () => {
    window.location.href = '/api/lan/export.csv';
    closeModal();
  });
  $('lan-export-sd') && $('lan-export-sd').addEventListener('click', async () => {
    try {
      const d = await api('/api/lan/export/sd', { method: 'POST', body: '{}' });
      const msg = $('lan-export-msg');
      if (msg) msg.textContent = 'Saved: ' + (d.path || '');
      toolOut('Saved ' + (d.path || ''));
    } catch (e) {
      const msg = $('lan-export-msg');
      if (msg) msg.textContent = String(e.message || e);
    }
  });
}

/** Keep LAN info card in sync when wifi status poll runs. */
export function applyLanFromWifiStatus(d) {
  if (!d || !d.sta_connected) return;
  if ($('lan-ssid')) $('lan-ssid').textContent = d.connected_ssid || d.sta_ssid || '—';
  if ($('lan-ip')) $('lan-ip').textContent = d.sta_ip || '—';
  if ($('lan-bssid')) $('lan-bssid').textContent = d.sta_bssid || '—';
  if ($('lan-signal')) {
    $('lan-signal').innerHTML = signalBarsHtml(d.rssi) + ' <span>' + esc(signalText(d.rssi) || '') + '</span>';
  }
  if ($('lan-status')) $('lan-status').textContent = d.status || 'connected';
}
