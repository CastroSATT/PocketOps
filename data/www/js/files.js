import { esc } from './util.js';

let cwd = '/';
let sdPresent = false;
let filesPanelOpen = false;
let filesUiPulse = 0;

async function pulseFilesUi(on) {
  try {
    await fetch('/api/sd/files-ui', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: on ? 'on' : 'off'
    });
  } catch (_) {}
}

function startFilesUiHeartbeat() {
  stopFilesUiHeartbeat();
  pulseFilesUi(true);
  filesUiPulse = setInterval(() => pulseFilesUi(true), 1500);
}

function stopFilesUiHeartbeat() {
  if (filesUiPulse) {
    clearInterval(filesUiPulse);
    filesUiPulse = 0;
  }
}

function $(id) { return document.getElementById(id); }

function setMsg(t, kind) {
  const el = $('files-msg');
  if (!el) return;
  el.textContent = t || '';
  el.className = 'hint' + (kind ? ' ' + kind : '');
  el.style.textAlign = 'left';
  if (kind === 'err') el.style.color = '#d86';
  else if (kind === 'ok') el.style.color = '#6d6';
  else el.style.color = '';
}

function parentPath(p) {
  if (!p || p === '/') return '/';
  const s = p.replace(/\/+$/, '');
  const i = s.lastIndexOf('/');
  if (i <= 0) return '/';
  return s.slice(0, i);
}

function formatSize(n) {
  n = Number(n) || 0;
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
  return (n / (1024 * 1024)).toFixed(1) + ' MB';
}

/** Show Files tab when a microSD card is installed (Expose not required). */
export function setFilesTabVisible(show) {
  sdPresent = !!show;
  const tab = $('tab-files');
  if (!tab) return;
  const wasHidden = tab.hidden;
  tab.hidden = !sdPresent;
  if (!sdPresent && !wasHidden) {
    const panel = $('panel-files');
    if (panel && panel.classList.contains('active')) {
      document.querySelectorAll('#main-tabs button').forEach(b => {
        b.classList.toggle('active', b.dataset.tab === 'remote');
      });
      document.querySelectorAll('.panel').forEach(p => {
        p.classList.toggle('active', p.id === 'panel-remote');
      });
      if (filesPanelOpen) leaveFilesPanel();
    }
  }
}

function friendlyErr(d, fallback) {
  if (d && d.error === 'expose_on') {
    return d.message || 'Turn off USB Expose to browse files.';
  }
  if (d && d.error === 'sd_busy') {
    return d.message || 'SD is busy — eject on the Mac or wait, then try again.';
  }
  if (d && d.error === 'sd_settle') {
    return d.message || 'SD settling after Expose off — eject on the Mac if the drive still shows, wait, then Refresh.';
  }
  if (d && d.message) return d.message;
  if (d && d.error) return d.error;
  return fallback || 'request failed';
}

function sleep(ms) {
  return new Promise(r => setTimeout(r, ms));
}

async function apiJson(url, opts) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 20000);
  try {
    const r = await fetch(url, { ...(opts || {}), signal: ctrl.signal });
    const d = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(friendlyErr(d, r.statusText));
    return d;
  } catch (e) {
    if (e && e.name === 'AbortError') throw new Error('Timed out — is USB Expose on? Turn it off in Config → SD.');
    throw e;
  } finally {
    clearTimeout(t);
  }
}

async function fetchSdStatus() {
  const r = await fetch('/api/sd/status');
  return r.json();
}

function openExposeModal() {
  const m = $('files-expose-modal');
  if (!m) return;
  m.classList.add('open');
  m.setAttribute('aria-hidden', 'false');
}

function closeExposeModal() {
  const m = $('files-expose-modal');
  if (!m) return;
  m.classList.remove('open');
  m.setAttribute('aria-hidden', 'true');
}

async function refreshExposeDialog() {
  const statusEl = $('files-expose-status');
  const offBtn = $('files-expose-off');
  if (statusEl) statusEl.textContent = 'Checking SD activity…';
  if (offBtn) offBtn.disabled = true;
  try {
    const sd = await fetchSdStatus();
    if (!sd.expose) {
      closeExposeModal();
      setMsg('Expose is off — loading files…', 'ok');
      await loadFilesDir(cwd);
      return;
    }
    if (sd.activity) {
      if (statusEl) {
        statusEl.textContent = 'SD is busy (Mac is reading/writing). Eject the disk on the Mac or wait, then tap Check again.';
      }
      if (offBtn) offBtn.disabled = true;
    } else {
      if (statusEl) {
        statusEl.textContent =
          'No SD activity — safe to turn Expose off. Afterward, eject the volume on the Mac if it still appears before browsing Files.';
      }
      if (offBtn) offBtn.disabled = false;
    }
  } catch (e) {
    if (statusEl) statusEl.textContent = String(e.message || e);
  }
}

async function safeTurnExposeOff() {
  const statusEl = $('files-expose-status');
  const offBtn = $('files-expose-off');
  if (offBtn) offBtn.disabled = true;
  if (statusEl) statusEl.textContent = 'Turning Expose off safely…';
  try {
    const r = await fetch('/api/sd/expose', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: 'safe-off'
    });
    const text = await r.text();
    let d = {};
    try { d = JSON.parse(text); } catch (_) {}
    if (!r.ok) {
      throw new Error(friendlyErr(d, text || r.statusText));
    }
    const exposeEl = document.getElementById('sd-expose');
    if (exposeEl) exposeEl.checked = false;
    closeExposeModal();
    setMsg('Expose off — waiting for Mac to drop the volume, then loading…', 'warn');
    // Let USB re-enum + cool-off finish before remounting the card for Files.
    await sleep(2200);
    setMsg('Loading files…');
    await loadFilesDir(cwd);
    setMsg('If Finder still shows the drive, eject it, then tap Refresh.', 'ok');
  } catch (e) {
    if (statusEl) statusEl.textContent = String(e.message || e);
    await refreshExposeDialog();
  }
}

export async function loadFilesDir(path) {
  if (path != null) cwd = path || '/';
  if (!cwd.startsWith('/')) cwd = '/' + cwd;
  const pathEl = $('files-path');
  if (pathEl) pathEl.value = cwd;
  setMsg('Loading…');
  try {
    try {
      const sd = await fetchSdStatus();
      if (sd && sd.expose) {
        renderList([]);
        setMsg('USB Expose is on — confirm in the dialog to turn it off safely.', 'err');
        openExposeModal();
        await refreshExposeDialog();
        return;
      }
    } catch (_) {}
    const d = await apiJson('/api/sd/fs/list?path=' + encodeURIComponent(cwd));
    cwd = d.path || cwd;
    if (pathEl) pathEl.value = cwd;
    renderList(d.entries || []);
    setMsg((d.entries || []).length + ' item(s)', 'ok');
  } catch (e) {
    renderList([]);
    setMsg(String(e.message || e), 'err');
    if (String(e.message || '').includes('expose') || String(e.message || '') === 'expose_on') {
      openExposeModal();
      await refreshExposeDialog();
    }
  }
}

function renderList(entries) {
  const box = $('files-list');
  if (!box) return;
  const sorted = entries.slice().sort((a, b) => {
    if (!!a.dir !== !!b.dir) return a.dir ? -1 : 1;
    return String(a.name || '').localeCompare(String(b.name || ''), undefined, { sensitivity: 'base' });
  });
  if (!sorted.length) {
    box.innerHTML = '<p class="hint" style="text-align:left">Empty folder</p>';
    return;
  }
  box.innerHTML = sorted.map(e => {
    const kind = e.dir ? 'DIR' : 'FILE';
    const meta = e.dir ? 'folder' : formatSize(e.size);
    return `<div class="files-row" data-path="${esc(e.path)}" data-dir="${e.dir ? '1' : '0'}">
      <button type="button" class="files-open"><span class="files-kind">${kind}</span> <span class="files-name">${esc(e.name)}</span></button>
      <span class="files-meta">${esc(meta)}</span>
      <div class="files-row-actions">
        ${e.dir ? '' : `<button type="button" class="icon-btn files-dl" title="Download">↓</button>`}
        <button type="button" class="icon-btn danger files-del" title="Delete">✕</button>
      </div>
    </div>`;
  }).join('');

  box.querySelectorAll('.files-row').forEach(row => {
    const path = row.dataset.path;
    const isDir = row.dataset.dir === '1';
    row.querySelector('.files-open').addEventListener('click', () => {
      if (isDir) loadFilesDir(path);
    });
    const dl = row.querySelector('.files-dl');
    if (dl) {
      dl.addEventListener('click', e => {
        e.stopPropagation();
        window.location.href = '/api/sd/fs/download?path=' + encodeURIComponent(path);
      });
    }
    row.querySelector('.files-del').addEventListener('click', async e => {
      e.stopPropagation();
      if (!confirm('Delete ' + path + (isDir ? ' and everything inside?' : '?'))) return;
      try {
        await apiJson('/api/sd/fs/delete', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ path })
        });
        setMsg('Deleted ' + path, 'ok');
        await loadFilesDir(cwd);
      } catch (err) {
        setMsg(String(err.message || err), 'err');
      }
    });
  });
}

function setUploadProgress(show, pct, status) {
  const wrap = $('files-upload-progress');
  const bar = $('files-upload-bar');
  const pctEl = $('files-upload-pct');
  const stEl = $('files-upload-status');
  if (wrap) wrap.hidden = !show;
  const p = Math.max(0, Math.min(100, Math.round(pct || 0)));
  if (bar) bar.style.width = p + '%';
  if (pctEl) pctEl.textContent = p + '%';
  if (stEl) stEl.textContent = status || '';
}

function uploadOne(file, onProgress) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const fd = new FormData();
    fd.append('file', file, file.name);
    xhr.open('POST', '/api/sd/fs/upload?path=' + encodeURIComponent(cwd));
    xhr.upload.onprogress = e => {
      if (!e.lengthComputable) return;
      onProgress(e.loaded / e.total);
    };
    xhr.onload = () => {
      let d = {};
      try { d = JSON.parse(xhr.responseText || '{}'); } catch (_) {}
      if (xhr.status >= 200 && xhr.status < 300) resolve(d);
      else reject(new Error(friendlyErr(d, xhr.statusText || 'upload fail')));
    };
    xhr.onerror = () => reject(new Error('network error'));
    xhr.send(fd);
  });
}

async function uploadFiles(fileList) {
  const files = Array.from(fileList || []);
  if (!files.length) return;
  const btn = $('files-upload-btn');
  if (btn) btn.disabled = true;
  const totalBytes = files.reduce((s, f) => s + (f.size || 0), 0) || 1;
  let doneBytes = 0;
  setUploadProgress(true, 0, 'Starting…');
  try {
    for (let i = 0; i < files.length; ++i) {
      const file = files[i];
      const base = doneBytes;
      setMsg('Uploading ' + file.name + ' (' + (i + 1) + '/' + files.length + ')…');
      setUploadProgress(true, (base / totalBytes) * 100, file.name);
      try {
        const d = await uploadOne(file, frac => {
          const loaded = base + frac * (file.size || 0);
          const pct = (loaded / totalBytes) * 100;
          setUploadProgress(true, pct, file.name);
        });
        doneBytes += file.size || 0;
        setUploadProgress(true, (doneBytes / totalBytes) * 100, file.name);
        setMsg('Uploaded ' + (d.path || file.name), 'ok');
      } catch (e) {
        setMsg(String(e.message || e), 'err');
        setUploadProgress(true, (doneBytes / totalBytes) * 100, 'Failed');
        return;
      }
    }
    setUploadProgress(true, 100, 'Done');
    await loadFilesDir(cwd);
  } finally {
    if (btn) btn.disabled = false;
    setTimeout(() => setUploadProgress(false, 0, ''), 1200);
  }
}

export function bindFilesPanel() {
  $('files-up') && $('files-up').addEventListener('click', () => loadFilesDir(parentPath(cwd)));
  $('files-refresh') && $('files-refresh').addEventListener('click', () => loadFilesDir(cwd));
  $('files-expose-close') && $('files-expose-close').addEventListener('click', closeExposeModal);
  $('files-expose-retry') && $('files-expose-retry').addEventListener('click', () => refreshExposeDialog());
  $('files-expose-off') && $('files-expose-off').addEventListener('click', () => safeTurnExposeOff());
  $('files-mkdir') && $('files-mkdir').addEventListener('click', async () => {
    const name = prompt('New folder name');
    if (!name) return;
    const path = cwd === '/' ? '/' + name.replace(/^\/+/, '') : cwd + '/' + name.replace(/^\/+/, '');
    try {
      await apiJson('/api/sd/fs/mkdir', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path })
      });
      await loadFilesDir(cwd);
      setMsg('Created ' + path, 'ok');
    } catch (e) {
      setMsg(String(e.message || e), 'err');
    }
  });
  $('files-upload-btn') && $('files-upload-btn').addEventListener('click', () => {
    const inp = $('files-upload');
    if (inp) inp.click();
  });
  $('files-upload') && $('files-upload').addEventListener('change', e => {
    uploadFiles(e.target.files).finally(() => { e.target.value = ''; });
  });
}

export function enterFilesPanel() {
  filesPanelOpen = true;
  startFilesUiHeartbeat();
  loadFilesDir(cwd);
}

export async function leaveFilesPanel() {
  filesPanelOpen = false;
  stopFilesUiHeartbeat();
  await pulseFilesUi(false);
}
