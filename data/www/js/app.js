import { state, setWifiModeBadge } from './util.js';
import { loadCfg, saveCfg, revertDisplayLive, bindBackupPanel } from './config.js';
import { startSdIconWatch, bindSdPanel } from './sd.js';
import { startRemote, connect, loadMouseCfg } from './remote.js';

async function pollWifiModeBadge() {
  try {
    const r = await fetch('/api/wifi/status');
    const d = await r.json();
    if (r.ok) setWifiModeBadge(d.mode || 'ap', { apUp: !!d.ap_up, staUp: !!d.sta_connected });
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
  if (!btn) return;
  if (btn.dataset.tab !== 'config' && state.currentCfg === 'display' && state.dirty) {
    if (!(await discardDirtyIfNeeded())) return;
  }
  document.querySelectorAll('#main-tabs button').forEach(b => b.classList.toggle('active', b === btn));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + btn.dataset.tab));
  if (btn.dataset.tab === 'config') loadCfg(state.currentCfg);
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
loadMouseCfg();
startRemote();
connect();
startSdIconWatch();
pollWifiModeBadge();
setInterval(pollWifiModeBadge, 5000);
