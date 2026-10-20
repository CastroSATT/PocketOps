import { setMsg, esc } from './util.js';

let selectedName = '';

function $(id) { return document.getElementById(id); }

function formatSize(n) {
  n = Number(n) || 0;
  if (n < 1024) return n + ' B';
  return (n / 1024).toFixed(1) + ' KB';
}

async function loadLogsList() {
  const list = $('logs-list');
  if (!list) return;
  list.innerHTML = '<p class="hint" style="text-align:left">Loading…</p>';
  try {
    const r = await fetch('/api/logs');
    const d = await r.json();
    if (!r.ok) throw new Error(d.error || ('HTTP ' + r.status));
    const en = $('logs-enable');
    if (en) en.checked = d.enabled !== false;
    const ser = $('logs-serial');
    if (ser) ser.checked = d.serial !== false;
    const files = d.files || [];
    const meta = $('logs-meta');
    if (meta) {
      const bits = [];
      if (d.enabled === false) bits.push('device logging off');
      if (d.serial === false) bits.push('USB Serial muted');
      const state = bits.length ? bits.join(', ') + '. ' : '';
      meta.textContent = state + files.length + ' / ' + (d.max || 10) +
        ' sessions on device (LittleFS).' +
        (d.current ? ' Current: ' + d.current : '');
    }
    if (!files.length) {
      list.innerHTML = '<p class="hint" style="text-align:left">No log files yet.</p>';
      return;
    }
    list.innerHTML = files.map(f => {
      const cur = f.current ? ' <span class="logs-current">live</span>' : '';
      const active = f.name === selectedName ? ' active' : '';
      return `<button type="button" class="logs-item${active}" data-name="${esc(f.name)}">` +
        `<span class="logs-name">${esc(f.name)}</span>${cur}` +
        `<span class="logs-size">${esc(formatSize(f.size))}</span></button>`;
    }).join('');
    list.querySelectorAll('.logs-item').forEach(btn => {
      btn.addEventListener('click', () => openLog(btn.dataset.name));
    });
    if (selectedName && !files.some(f => f.name === selectedName)) {
      selectedName = '';
      const pre = $('logs-body');
      if (pre) pre.textContent = 'Select a log file.';
    }
    if (!selectedName && files[0]) {
      await openLog(files[0].name);
    } else if (selectedName) {
      list.querySelectorAll('.logs-item').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.name === selectedName);
      });
    }
  } catch (e) {
    list.innerHTML = '';
    setMsg(String(e.message || e), 'err');
  }
}

async function openLog(name) {
  selectedName = name || '';
  const pre = $('logs-body');
  const title = $('logs-view-title');
  if (title) title.textContent = selectedName || 'Log';
  document.querySelectorAll('.logs-item').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.name === selectedName);
  });
  if (!selectedName || !pre) return;
  pre.textContent = 'Loading…';
  try {
    const r = await fetch('/api/logs/file?name=' + encodeURIComponent(selectedName));
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    pre.textContent = t || '(empty)';
    pre.scrollTop = 0;
    setMsg('Loaded ' + selectedName, 'ok');
  } catch (e) {
    pre.textContent = String(e.message || e);
    setMsg(String(e.message || e), 'err');
  }
}

async function deleteSelected() {
  if (!selectedName) {
    setMsg('Select a log first', 'warn');
    return;
  }
  if (!confirm('Delete ' + selectedName + '?')) return;
  try {
    const r = await fetch('/api/logs/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'name=' + selectedName
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    selectedName = '';
    const pre = $('logs-body');
    if (pre) pre.textContent = 'Select a log file.';
    setMsg('Deleted', 'ok');
    await loadLogsList();
  } catch (e) {
    setMsg(String(e.message || e), 'err');
  }
}

async function clearOldLogs() {
  if (!confirm('Delete all finished log sessions? (keeps the current live session)')) return;
  try {
    const r = await fetch('/api/logs/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'clear'
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    selectedName = '';
    setMsg('Cleared old sessions', 'ok');
    await loadLogsList();
  } catch (e) {
    setMsg(String(e.message || e), 'err');
  }
}

async function setLogsEnabled(want) {
  const en = $('logs-enable');
  try {
    const r = await fetch('/api/logs/enable', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: want ? 'on' : 'off'
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    const d = JSON.parse(t);
    if (en) en.checked = !!d.enabled;
    const ser = $('logs-serial');
    if (ser && d.serial !== undefined) ser.checked = !!d.serial;
    setMsg(d.enabled ? 'Logging on' : 'Logging off', 'ok');
    await loadLogsList();
  } catch (e) {
    if (en) en.checked = !want;
    setMsg(String(e.message || e), 'err');
  }
}

async function setSerialEnabled(want) {
  const ser = $('logs-serial');
  try {
    const r = await fetch('/api/logs/serial', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: want ? 'on' : 'off'
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    const d = JSON.parse(t);
    if (ser) ser.checked = !!d.serial;
    setMsg(d.serial ? 'USB Serial on' : 'USB Serial muted', 'ok');
    await loadLogsList();
  } catch (e) {
    if (ser) ser.checked = !want;
    setMsg(String(e.message || e), 'err');
  }
}

export function enterLogsPanel() {
  loadLogsList();
}

export function bindLogsPanel() {
  const refresh = $('logs-refresh');
  const del = $('logs-delete');
  const clear = $('logs-clear');
  const en = $('logs-enable');
  const ser = $('logs-serial');
  if (refresh && !refresh.dataset.bound) {
    refresh.dataset.bound = '1';
    refresh.addEventListener('click', () => loadLogsList());
  }
  if (del && !del.dataset.bound) {
    del.dataset.bound = '1';
    del.addEventListener('click', () => deleteSelected());
  }
  if (clear && !clear.dataset.bound) {
    clear.dataset.bound = '1';
    clear.addEventListener('click', () => clearOldLogs());
  }
  if (en && !en.dataset.bound) {
    en.dataset.bound = '1';
    en.addEventListener('change', () => setLogsEnabled(!!en.checked));
  }
  if (ser && !ser.dataset.bound) {
    ser.dataset.bound = '1';
    ser.addEventListener('change', () => setSerialEnabled(!!ser.checked));
  }
}
