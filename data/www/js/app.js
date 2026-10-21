import { state, setWifiModeBadge, updateStaChrome, fetchChrome, chromeWifi, chromeSd } from './util.js';
import {
  startSdIconWatch, bindSdPanel, refreshSdBadge, applyChromeSd, sdExposeActive
} from './sd.js';
import { startRemote, connect, loadMouseCfg, enterRemotePanel, leaveRemotePanel } from './remote.js';

/** Tab slabs: load JS (+ device session) only when that tab is used. */
let lanMod = null;
let filesMod = null;
let configMod = null;

async function ensureLan() {
  if (!lanMod) {
    lanMod = await import('./lan.js');
    lanMod.bindLanPanel();
  }
  return lanMod;
}

async function ensureFiles() {
  if (!filesMod) {
    filesMod = await import('./files.js');
    filesMod.bindFilesPanel();
  }
  return filesMod;
}

async function ensureConfig() {
  if (!configMod) {
    configMod = await import('./config.js');
    const logs = await import('./logs.js');
    const firmware = await import('./firmware.js');
    configMod.bindBackupPanel();
    logs.bindLogsPanel();
    firmware.bindFirmwarePanel();
    configMod._logs = logs;
  }
  return configMod;
}

/** One SoftAP hit for Wi‑Fi mode badge + SD badge. */
async function pollChrome() {
  try {
    const d = await fetchChrome();
    const w = chromeWifi(d);
    setWifiModeBadge(w.mode || 'ap', { apUp: !!w.ap_up, staUp: !!w.sta_connected });
    updateStaChrome(w);
    if (lanMod) lanMod.applyLanFromWifiStatus(w);
    applyChromeSd(chromeSd(d));
  } catch (_) {}
}

function scheduleChromePoll() {
  pollChrome().finally(() => {
    // Faster while Expose is on so the SD badge can flash activity.
    const ms = sdExposeActive() ? 600 : 2000;
    setTimeout(scheduleChromePoll, ms);
  });
}

async function discardDirtyIfNeeded() {
  if (!state.dirty || !configMod) return true;
  const leaveDisplay = state.currentCfg === 'display';
  const msg = leaveDisplay
    ? 'Discard unsaved Display changes and restore previous brightness?'
    : 'Discard unsaved changes?';
  if (!confirm(msg)) return false;
  if (leaveDisplay) await configMod.revertDisplayLive();
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
  const nextTab = btn.dataset.tab;
  if (prevTab === nextTab) return;

  if (prevTab === 'lan' && nextTab !== 'lan' && lanMod) {
    await lanMod.leaveLanPanel();
  }
  if (prevTab === 'files' && nextTab !== 'files' && filesMod) {
    await filesMod.leaveFilesPanel();
  }
  if (prevTab === 'config' && nextTab !== 'config' && configMod) {
    configMod.leaveConfigPanel();
  }
  if (prevTab === 'remote' && nextTab !== 'remote') {
    leaveRemotePanel();
  }

  document.querySelectorAll('#main-tabs button').forEach(b => b.classList.toggle('active', b === btn));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'panel-' + nextTab));

  if (nextTab === 'config') {
    const cfg = await ensureConfig();
    cfg.loadCfg(state.currentCfg);
  }
  if (nextTab === 'lan') {
    const lan = await ensureLan();
    await lan.enterLanPanel();
  }
  if (nextTab === 'files') {
    const files = await ensureFiles();
    files.enterFilesPanel();
  }
  if (nextTab === 'remote') {
    enterRemotePanel();
  }
  refreshSdBadge();
});

document.getElementById('cfg-subtabs').addEventListener('click', async e => {
  const btn = e.target.closest('button[data-cfg]');
  if (!btn) return;
  if (btn.dataset.cfg === state.currentCfg) return;
  if (!(await discardDirtyIfNeeded())) return;
  const cfg = await ensureConfig();
  state.currentCfg = btn.dataset.cfg;
  document.querySelectorAll('#cfg-subtabs button').forEach(b => b.classList.toggle('active', b === btn));
  cfg.loadCfg(state.currentCfg);
});

document.getElementById('cfg-reload').addEventListener('click', async () => {
  const cfg = await ensureConfig();
  const onDisplay = state.currentCfg === 'display';
  if (state.dirty) {
    const msg = onDisplay
      ? 'Cancel and restore the last saved display / LED levels?'
      : 'Reload and discard edits?';
    if (!confirm(msg)) return;
  }
  if (onDisplay) await cfg.revertDisplayLive();
  cfg.loadCfg(state.currentCfg);
});

document.getElementById('cfg-save').addEventListener('click', async () => {
  const cfg = await ensureConfig();
  cfg.saveCfg(state.currentCfg);
});

// Remote is the default tab slab — keep its handlers + WS warm.
bindSdPanel();
loadMouseCfg();
startRemote();
connect();
startSdIconWatch();
scheduleChromePoll();
