import { state, setWifiModeBadge, updateStaChrome } from './util.js';
import { loadCfg, saveCfg, revertDisplayLive, bindBackupPanel } from './config.js';
import { bindLogsPanel } from './logs.js';
import { startSdIconWatch, bindSdPanel, refreshSdBadge } from './sd.js';
import { startRemote, connect, loadMouseCfg } from './remote.js';
import { bindLanPanel, enterLanPanel, leaveLanPanel, applyLanFromWifiStatus } from './lan.js';
import { bindFilesPanel, enterFilesPanel, leaveFilesPanel } from './files.js';

async function pollWifiModeBadge() {
  try {
    const r = await fetch('/api/wifi/status');
    const d = await r.json();
    if (!r.ok) return;
    setWifiModeBadge(d.mode || 'ap', { apUp: !!d.ap_up, staUp: !!d.sta_connected });
    updateStaChrome(d);
    applyLanFromWifiStatus(d);
  } catch (_) {}
}

async function discardDirtyIfNeeded(nextCfg) {
  if (!state.dirty) return true;
  const leaveDisplay = state.currentCfg === 'display';
  const msg = leaveDisplay
    ? 'Discard unsaved Display changes and restore previous brightness?'
    : 'Discard unsaved changes?';
  if (!confirm(msg)) return false;
  if (leaveDisplay) await revertDisplayLive();
  return true;
}

document.getElementById('main-tabs').addEventListener('click', async e => {
  const btn = e.target.closest('button[data-tab]');
  if (!btn || btn.disabled) return;
  if (btn.dataset.tab !== 'config' && state.currentCfg === 'display' && state.dirty) {
    if (!(await discardDirtyIfNeeded())) return;
  }
  const prev = document.querySelector('#main-tabs button.active');
  const prevTab = prev && prev.dataset.tab;
  if (prevTab === 'lan' && btn.dataset.tab !== 'lan') {
    await leaveLanPanel();
  }
  if (prevTab === 'files' && btn.dataset.tab !== 'files') {
    await leaveFilesPanel();
  }
  document.querySelectorAll('#main-tabs button').forEach(b => b.classList.toggle('active', b === btn));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + btn.dataset.tab));
  if (btn.dataset.tab === 'config') loadCfg(state.currentCfg);
  if (btn.dataset.tab === 'lan') enterLanPanel();
  if (btn.dataset.tab === 'files') enterFilesPanel();
  refreshSdBadge();
});

document.getElementById('cfg-subtabs').addEventListener('click', async e => {
  const btn = e.target.closest('button[data-cfg]');
  if (!btn) return;
  if (btn.dataset.cfg === state.currentCfg) return;
  if (!(await discardDirtyIfNeeded(btn.dataset.cfg))) return;
  state.currentCfg = btn.dataset.cfg;
  document.querySelectorAll('#cfg-subtabs button').forEach(b => b.classList.toggle('active', b === btn));
  loadCfg(state.currentCfg);
});

document.getElementById('cfg-reload').addEventListener('click', async () => {
  const onDisplay = state.currentCfg === 'display';
  if (state.dirty) {
    const msg = onDisplay
      ? 'Cancel and restore the last saved display / LED levels?'
      : 'Reload and discard edits?';
    if (!confirm(msg)) return;
  }
  if (onDisplay) await revertDisplayLive();
  loadCfg(state.currentCfg);
});

document.getElementById('cfg-save').addEventListener('click', () => saveCfg(state.currentCfg));

bindSdPanel();
bindBackupPanel();
bindLogsPanel();
bindLanPanel();
bindFilesPanel();
loadMouseCfg();
startRemote();
connect();
startSdIconWatch();
pollWifiModeBadge();
setInterval(pollWifiModeBadge, 5000);
