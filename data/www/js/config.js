import {
  state, clearDirty, setMsg, esc, parseIni, fieldHtml, bindDirty, grabField,
  showView, cfgMeta, cfgFields, setWifiModeBadge, apiJson
} from './util.js';
import { renderSd, serializeSd, refreshSdStatus } from './sd.js';
import { enterLogsPanel } from './logs.js';
import { enterFirmwarePanel } from './firmware.js';
import { loadMouseCfg } from './remote.js';

/** Lazy wifi module — Config → Network only (keeps other Config subtabs lighter). */
let wifiMod = null;
async function wifi() {
  if (!wifiMod) wifiMod = await import('./wifi.js');
  return wifiMod;
}

export const NET_GROUPS = [
  {title:'Wi-Fi mode',fields:[
    {key:'wifi_mode',label:'Mode',type:'select',options:[
      {v:'ap',t:'SoftAP only (hotspot)'},
      {v:'apsta',t:'SoftAP + join network'},
      {v:'sta',t:'Join network only'}
    ]}
  ]}
];

export const BT_GROUPS = [
  {title:'Bluetooth names',fields:[
    {key:'ble_device_name',label:'BLE Remote name (what phones see for Remote HID)'},
    {key:'ble_serial_device_name',label:'BLE Serial name (what phones see for Serial HID)'}
  ]}
];
export const MOUSE_GROUPS = [
  {title:'Remote touchpad',fields:[
    {key:'touchpad_speed',label:'Pointer speed (%) · 150 = default',type:'range',min:25,max:300,step:5},
    {key:'scroll_speed',label:'Scroll speed (%) · 100 = normal',type:'range',min:25,max:300,step:5},
    {key:'two_finger_scroll',label:'Two-finger drag scroll',type:'checkbox'}
  ]},
  {title:'BT Serial mouse',fields:[
    {key:'ble_serial_mouse_step',label:'WASD mouse step (1–127)',type:'number'}
  ]}
];

export const KEYBOARD_GROUPS = [
  {title:'Typing',fields:[
    {key:'char_interval_ms',label:'Delay between characters (ms) · 0 = fastest',type:'range',min:0,max:100,step:1}
  ]}
];

export const UI_GROUPS = [
  {title:'Splash',fields:[
    {key:'splash',label:'Splash text (blank = PocketOps)'},
    {key:'splash_ms',label:'Splash duration (ms)',type:'number'}
  ]}
];

export const DISPLAY_GROUPS = [
  {title:'Screen',fields:[
    {key:'display_sleep_sec',label:'Display sleep',type:'select',options:[
      {v:'0',t:'Never'},
      {v:'15',t:'15 seconds'},
      {v:'30',t:'30 seconds'},
      {v:'60',t:'1 minute'},
      {v:'120',t:'2 minutes'},
      {v:'300',t:'5 minutes'},
      {v:'600',t:'10 minutes'}
    ]},
    {key:'display_brightness',label:'Display brightness (%)',type:'range',min:5,max:100,step:1}
  ]},
  {title:'Status light',fields:[
    {key:'light_brightness',label:'LED brightness (%)',type:'range',min:5,max:100,step:1}
  ]}
];

export const CFG = {
  network:{mode:'network',groups:NET_GROUPS,header:'; PocketOps network / wireless',label:'Network',note:'Wi-Fi mode saves immediately. Tap SoftAP to open hotspot settings (SSID / password / channel / ports). Join other networks from the live list.'},
  bluetooth:{mode:'fields',groups:BT_GROUPS,header:'; PocketOps Bluetooth',label:'Bluetooth',note:'Advertise names for Remote / Serial tools.'},
  mouse:{mode:'fields',groups:MOUSE_GROUPS,header:'; PocketOps Mouse',label:'Mouse',note:'Pointer / scroll speed, two-finger scroll, and BT Serial WASD step. Use Remote to control the pointer.'},
  keyboard:{mode:'fields',groups:KEYBOARD_GROUPS,header:'; PocketOps Keyboard',label:'Keyboard',note:'How fast characters are typed when you use the Remote text box or paste.'},
  sd:{mode:'sd',label:'SD',note:'Drive name, folders after Format, Expose, and wipe.'},
  ui:{mode:'fields',groups:UI_GROUPS,header:'; PocketOps UI',label:'UI',note:'Splash, tool on boot (applies next reboot), and duration. Version / remote / BT / BOOT button timings are fixed in firmware.'},
  display:{mode:'display',groups:DISPLAY_GROUPS,header:'; PocketOps Display / device light',label:'Display',note:'Sliders preview live. Save to keep. Cancel restores the last saved levels.'},
  logs:{mode:'logs',label:'Logs',note:'Full on-chip session logs (max 10). No SD required.'},
  firmware:{mode:'firmware',label:'Firmware',note:'USB DFU preference + future install path (placeholders).'},
  backup:{mode:'backup',label:'Backup',note:'Download / restore .pobak, or factory-reset on-chip settings, profiles, and logs.'}
};

let deviceStatusTimer = null;
let displayPreviewTimer = null;
/** Last saved / loaded Display values — Cancel / discard reverts to these. */
let displayCommitted = {
  display_brightness: '80',
  light_brightness: '44',
  display_sleep_sec: '0'
};

function setCfgActionLabels(id) {
  const reload = document.getElementById('cfg-reload');
  const save = document.getElementById('cfg-save');
  if (!reload) return;
  if (id === 'network' || id === 'backup' || id === 'logs' || id === 'firmware') {
    reload.style.display = 'none';
    if (save && (id === 'backup' || id === 'logs' || id === 'firmware')) save.style.display = 'none';
    return;
  }
  reload.style.display = '';
  if (save) save.style.display = '';
  reload.textContent = id === 'display' ? 'Cancel' : 'Reload';
}

function backupDownloadName() {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return 'pocketops-config-' + y + '-' + m + '-' + day + '.pobak';
}

export async function downloadBackupZip() {
  setMsg('Building backup…');
  try {
    const r = await fetch('/api/config/backup');
    if (!r.ok) throw new Error(await r.text() || ('HTTP ' + r.status));
    const blob = await r.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = backupDownloadName();
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
    setMsg('Download started', 'ok');
  } catch (err) {
    setMsg(String(err.message || err), 'err');
  }
}

export async function uploadBackupZip(file) {
  if (!file) return;
  if (!confirm('Replace on-chip config with this backup file?')) return;
  setMsg('Restoring…');
  try {
    const fd = new FormData();
    fd.append('backup', file, file.name || 'pocketops-config.pobak');
    const r = await fetch('/api/config/backup', { method: 'POST', body: fd });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    await loadMouseCfg();
    setMsg(t || 'Restored', 'ok');
  } catch (err) {
    setMsg(String(err.message || err), 'err');
  }
}

const FACTORY_CONFIRM = 'FACTORY RESET';

function factoryResetOptsFromUi() {
  return {
    settings: !!(document.getElementById('fr-settings') || {}).checked,
    nvs: !!(document.getElementById('fr-nvs') || {}).checked,
    wifi: !!(document.getElementById('fr-wifi') || {}).checked,
    lan: !!(document.getElementById('fr-lan') || {}).checked,
    logs: !!(document.getElementById('fr-logs') || {}).checked
  };
}

function syncFactoryResetUnlock() {
  const btn = document.getElementById('factory-reset');
  const input = document.getElementById('fr-confirm');
  if (!btn || !input) return;
  const opts = factoryResetOptsFromUi();
  const any = opts.settings || opts.nvs || opts.wifi || opts.lan || opts.logs;
  btn.disabled = !(any && input.value.trim() === FACTORY_CONFIRM);
}

export async function runFactoryReset() {
  const opts = factoryResetOptsFromUi();
  if (!(opts.settings || opts.nvs || opts.wifi || opts.lan || opts.logs)) {
    setMsg('Select at least one reset option', 'err');
    return;
  }
  const typed = ((document.getElementById('fr-confirm') || {}).value || '').trim();
  if (typed !== FACTORY_CONFIRM) {
    setMsg('Type FACTORY RESET to unlock', 'err');
    return;
  }
  if (!confirm('Factory reset the selected items on this device? This cannot be undone.')) return;
  if (!confirm('Final confirm: wipe selected settings / profiles / logs?')) return;

  setMsg('Factory reset…');
  try {
    const body = new URLSearchParams();
    body.set('confirm', FACTORY_CONFIRM);
    body.set('settings', opts.settings ? '1' : '0');
    body.set('nvs', opts.nvs ? '1' : '0');
    body.set('wifi', opts.wifi ? '1' : '0');
    body.set('lan', opts.lan ? '1' : '0');
    body.set('logs', opts.logs ? '1' : '0');
    const r = await fetch('/api/config/factory-reset', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString()
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    const conf = document.getElementById('fr-confirm');
    if (conf) conf.value = '';
    syncFactoryResetUnlock();
    setMsg(t || 'Factory reset complete', 'ok');
  } catch (err) {
    setMsg(String(err.message || err), 'err');
  }
}

export function bindBackupPanel() {
  const dl = document.getElementById('backup-download');
  const up = document.getElementById('backup-upload');
  const file = document.getElementById('backup-file');
  if (dl && !dl.dataset.bound) {
    dl.dataset.bound = '1';
    dl.addEventListener('click', () => downloadBackupZip());
  }
  if (up && file && !up.dataset.bound) {
    up.dataset.bound = '1';
    up.addEventListener('click', () => file.click());
    file.addEventListener('change', () => {
      const f = file.files && file.files[0];
      file.value = '';
      uploadBackupZip(f);
    });
  }

  const frBtn = document.getElementById('factory-reset');
  const frConfirm = document.getElementById('fr-confirm');
  if (frBtn && !frBtn.dataset.bound) {
    frBtn.dataset.bound = '1';
    frBtn.addEventListener('click', () => runFactoryReset());
  }
  if (frConfirm && !frConfirm.dataset.bound) {
    frConfirm.dataset.bound = '1';
    frConfirm.addEventListener('input', syncFactoryResetUnlock);
  }
  ['fr-settings', 'fr-nvs', 'fr-wifi', 'fr-lan', 'fr-logs'].forEach((id) => {
    const el = document.getElementById(id);
    if (el && !el.dataset.bound) {
      el.dataset.bound = '1';
      el.addEventListener('change', syncFactoryResetUnlock);
    }
  });
  syncFactoryResetUnlock();
}

export async function previewDisplayLive(vals) {
  const body = new URLSearchParams();
  const src = vals || {
    display_brightness: grabField('display_brightness') || displayCommitted.display_brightness,
    light_brightness: grabField('light_brightness') || displayCommitted.light_brightness,
    display_sleep_sec: grabField('display_sleep_sec') || displayCommitted.display_sleep_sec
  };
  body.set('display_brightness', String(src.display_brightness));
  body.set('light_brightness', String(src.light_brightness));
  body.set('display_sleep_sec', String(src.display_sleep_sec));
  try {
    await fetch('/api/device/preview', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body.toString()
    });
  } catch (_) {}
}

function scheduleDisplayPreview() {
  if (displayPreviewTimer) clearTimeout(displayPreviewTimer);
  displayPreviewTimer = setTimeout(() => previewDisplayLive(), 60);
}

/** Re-apply last committed (saved) brightness/sleep — used by Cancel / discard. */
export async function revertDisplayLive() {
  await previewDisplayLive(displayCommitted);
}

function bindDisplayLiveControls() {
  ['display_brightness', 'light_brightness'].forEach(key => {
    const el = document.getElementById('f-' + key);
    if (!el) return;
    el.addEventListener('input', scheduleDisplayPreview);
  });
  const sleepEl = document.getElementById('f-display_sleep_sec');
  if (sleepEl) sleepEl.addEventListener('change', scheduleDisplayPreview);
}

function formatUptime(sec) {
  const s = Math.max(0, Math.floor(Number(sec) || 0));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  if (h > 0) return h + 'h ' + m + 'm ' + r + 's';
  if (m > 0) return m + 'm ' + r + 's';
  return r + 's';
}

export function stopDeviceStatusPoll() {
  if (deviceStatusTimer) {
    clearInterval(deviceStatusTimer);
    deviceStatusTimer = null;
  }
}

/** Leave Config tab slab — stop Display/Network polls so SoftAP stays free for Remote/LAN. */
export function leaveConfigPanel() {
  stopDeviceStatusPoll();
  if (wifiMod) wifiMod.stopWifiPoll();
}

export async function refreshDeviceStatus() {
  const tempEl = document.getElementById('device-temp');
  const upEl = document.getElementById('device-uptime');
  if (!tempEl && !upEl) return;
  try {
    const d = await apiJson('/api/device/status', { timeoutMs: 8000 });
    if (tempEl) {
      tempEl.textContent = d.temp_ok
        ? ('Chip temperature: ' + Number(d.temp_c).toFixed(1) + ' °C')
        : 'Chip temperature: n/a';
    }
    if (upEl) {
      upEl.textContent = 'Uptime (this boot): ' + formatUptime(d.uptime_s) +
        (d.asleep ? ' · screen asleep' : '');
    }
  } catch (_) {
    if (tempEl) tempEl.textContent = 'Chip temperature: —';
    if (upEl) upEl.textContent = 'Uptime (this boot): —';
  }
}

export function startDeviceStatusPoll() {
  stopDeviceStatusPoll();
  refreshDeviceStatus();
  deviceStatusTimer = setInterval(refreshDeviceStatus, 2000);
}

export function renderDisplay(text) {
  const { kv } = parseIni(text);
  displayCommitted = {
    display_brightness: kv.display_brightness || '80',
    light_brightness: kv.light_brightness || '44',
    display_sleep_sec: kv.display_sleep_sec != null && kv.display_sleep_sec !== ''
      ? kv.display_sleep_sec
      : '0'
  };
  renderGroups(DISPLAY_GROUPS, text);
  cfgFields.insertAdjacentHTML('beforeend',
    '<p class="section-title">Device status</p>' +
    '<div class="device-status">' +
    '<p id="device-temp">Chip temperature: …</p>' +
    '<p id="device-uptime">Uptime (this boot): …</p>' +
    '</div>');
  bindDisplayLiveControls();
  previewDisplayLive(displayCommitted);
  startDeviceStatusPoll();
}



export function renderGroups(groups, text) {
  const { kv } = parseIni(text);
  if (!kv.splash && kv.product_name) kv.splash = kv.product_name;
  if (!kv.scroll_speed) kv.scroll_speed = '100';
  cfgFields.innerHTML = groups.map(g => {
    const body = g.fields.map(f => fieldHtml(f, kv[f.key] || '')).join('');
    return `<p class="section-title">${g.title}</p>${body}`;
  }).join('');
  bindDirty(cfgFields);
}

export function serializeGroups(header, label, groups) {
  const lines = ['; ' + header, '; ' + label];
  groups.forEach(g => g.fields.forEach(f => {
    lines.push(f.key + '=' + grabField(f.key));
  }));
  return lines.join('\n') + '\n';
}

function netVal(k){
  const el=document.getElementById('f-'+k);
  if(el){
    if(el.type==='checkbox')return el.checked?'1':'0';
    return el.value.trim();
  }
  const cache = wifiMod ? wifiMod.networkKvCache : {};
  return cache[k]!=null?String(cache[k]):'';
}

export function serializeNetwork(){
  const w = wifiMod;
  const cache = w ? w.networkKvCache : {};
  const lines=[CFG.network.header,'; '+CFG.network.label];
  NET_GROUPS.forEach(g=>g.fields.forEach(f=>{
    lines.push(f.key+'='+netVal(f.key));
  }));
  lines.push('; SoftAP hotspot (Live Wi-Fi row)');
  (w ? w.HOTSPOT_FIELDS : []).forEach(f=>{
    lines.push(f.key+'='+netVal(f.key));
  });
  // Last-join credentials are managed by Live Wi-Fi Connect, not the form.
  lines.push('; Last joined network (updated by Live Wi-Fi Connect)');
  lines.push('wifi_sta_ssid='+(cache.wifi_sta_ssid||''));
  lines.push('wifi_sta_pass='+(cache.wifi_sta_pass||''));
  return lines.join('\n')+'\n';
}

export async function saveNetworkNow(okMsg){
  const w = await wifi();
  const r=await fetch('/api/config/network',{
    method:'POST',
    headers:{'Content-Type':'text/plain; charset=utf-8'},
    body:serializeNetwork()
  });
  const t=await r.text();
  if(!r.ok)throw new Error(t||('HTTP '+r.status));
  w.networkKvCache.wifi_mode=grabField('wifi_mode')||w.networkKvCache.wifi_mode||'ap';
  // Live ap/sta flags refreshed by refreshWifiStatus right after.
  clearDirty();
  setMsg(okMsg||(t||'Saved'),'ok');
  await w.refreshWifiStatus();
}

export async function bindWifiModeAutosave(){
  const w = await wifi();
  const modeEl=document.getElementById('f-wifi_mode');
  if(!modeEl||modeEl.dataset.autosave==='1')return;
  modeEl.dataset.autosave='1';
  modeEl.addEventListener('change',async()=>{
    const next=modeEl.value;
    const prev=w.networkKvCache.wifi_mode||'ap';
    if(next==='sta'){
      const ok=confirm(
        'Join network only turns off the PocketOps hotspot.\n\n'+
        'Anyone connected to SoftAP (including this page) may be disconnected and lose the config UI until you rejoin via LAN or reboot into SoftAP.\n\n'+
        'Continue?'
      );
      if(!ok){
        modeEl.value=prev;
        return;
      }
    }
    setMsg('Saving Wi-Fi mode…','warn');
    try{
      await saveNetworkNow('Wi-Fi mode saved — used when Wi-Fi tool starts / on boot');
      // SoftAP-only darkens join rows; other modes re-enable Connect.
      w.paintWifiScan(w.lastWifiScan,w.lastConnectedSsid);
      // Radio just switched modes — give it a beat, then refresh the live list.
      setTimeout(()=>{ w.scanWifi({ quiet: false }); }, 400);
    }catch(err){
      modeEl.value=prev;
      setMsg(String(err.message||err),'err');
    }
  });
}



export async function renderNetwork(text) {
  const w = await wifi();
  const { kv } = parseIni(text);
  w.setNetworkKvCache(kv);
  w.resetJoinDraft();
  renderGroups(NET_GROUPS, text);
  w.insertLiveWifiBlock();
  w.bindWifiPanel();
  await bindWifiModeAutosave();
  w.refreshWifiStatus();
  w.refreshWifiProfiles();
  w.scanWifi({ quiet: true });
  w.startWifiPoll();
}

export function bodyForSave(id) {
  const info = CFG[id];
  if (info.mode === 'info') return '';
  if (info.mode === 'sd') return serializeSd();
  if (id === 'network') return serializeNetwork();
  return serializeGroups(info.header, info.label, info.groups);
}

async function mountBootToolPicker() {
  // Appended under Config → UI fields; saves immediately (not via Config Save).
  const host = cfgFields;
  if (!host) return;
  let box = document.getElementById('boot-tool-box');
  if (!box) {
    box = document.createElement('div');
    box.id = 'boot-tool-box';
    box.className = 'cfg-group';
    box.innerHTML =
      '<p class="section-title">Boot</p>' +
      '<div class="field"><label for="boot-tool-select">Tool on boot</label>' +
      '<select id="boot-tool-select">' +
      '<option value="menu">Menu (no auto-start)</option>' +
      '</select></div>' +
      '<p class="hint" style="text-align:left">Applies on the next reboot. Triple-back still returns to the menu.</p>';
    host.appendChild(box);
  }
  const sel = document.getElementById('boot-tool-select');
  if (!sel) return;
  try {
    const [tr, br] = await Promise.all([fetch('/api/tools'), fetch('/api/boot/tool')]);
    const td = await tr.json();
    const bd = await br.json();
    if (!tr.ok) throw new Error(td.error || ('tools HTTP ' + tr.status));
    if (!br.ok) throw new Error(bd.error || ('boot HTTP ' + br.status));
    const tools = td.tools || [];
    const cur = bd.id || 'menu';
    sel.innerHTML = '<option value="menu">Menu (no auto-start)</option>' +
      tools.map(t => '<option value="' + esc(t.id) + '">' + esc(t.name) + '</option>').join('');
    sel.value = tools.some(t => t.id === cur) ? cur : 'menu';
    if (!sel.dataset.bound) {
      sel.dataset.bound = '1';
      sel.addEventListener('change', async () => {
        const want = sel.value || 'menu';
        setMsg('Saving boot tool…', 'warn');
        try {
          const r = await fetch('/api/boot/tool', {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: want
          });
          const t = await r.text();
          if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
          const d = JSON.parse(t);
          sel.value = d.id || 'menu';
          setMsg(sel.value === 'menu' ? 'Boot: menu' : ('Boot tool: ' + sel.value), 'ok');
        } catch (e) {
          setMsg(String(e.message || e), 'err');
        }
      });
    }
  } catch (e) {
    setMsg(String(e.message || e), 'err');
  }
}

export async function loadCfg(id) {
  const info = CFG[id];
  if (!info) return;
  if (info.mode !== 'network' && wifiMod) wifiMod.stopWifiPoll();
  if (info.mode !== 'display') stopDeviceStatusPoll();
  cfgMeta.textContent = info.label + (info.note ? ' - ' + info.note : '');
  showView(info.mode === 'display' ? 'fields' : info.mode);
  setCfgActionLabels(id);
  if (info.mode === 'info') {
    cfgFields.innerHTML = '<p class="hint" style="text-align:left">' + esc(info.note || '') + '</p>';
    clearDirty();
    setMsg('');
    return;
  }
  if (info.mode === 'backup') {
    clearDirty();
    setMsg('');
    return;
  }
  if (info.mode === 'firmware') {
    clearDirty();
    setMsg('');
    enterFirmwarePanel();
    return;
  }
  if (info.mode === 'logs') {
    clearDirty();
    setMsg('');
    enterLogsPanel();
    return;
  }
  setMsg('Loading…');
  try {
    const r = await fetch('/api/config/' + id);
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    if (info.mode === 'sd') renderSd(t);
    else if (info.mode === 'network') await renderNetwork(t);
    else if (info.mode === 'display') renderDisplay(t);
    else renderGroups(info.groups, t);
    if (id === 'ui') await mountBootToolPicker();
    clearDirty();
    setMsg('Loaded', 'ok');
  } catch (err) {
    if (info.mode === 'fields' || info.mode === 'network' || info.mode === 'display') {
      cfgFields.innerHTML = '';
    }
    setMsg(String(err.message || err), 'err');
  }
}

export async function saveCfg(id) {
  const info = CFG[id];
  if (!info || info.mode === 'info' || info.mode === 'backup' || info.mode === 'logs' || info.mode === 'firmware') return;
  setMsg('Saving…');
  try {
    const r = await fetch('/api/config/' + id, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: bodyForSave(id)
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    clearDirty();
    setMsg(t || 'Saved', 'ok');
    if (id === 'sd') await refreshSdStatus();
    if (id === 'network') await (await wifi()).refreshWifiStatus();
    if (id === 'mouse') loadMouseCfg();
    if (id === 'display') {
      displayCommitted = {
        display_brightness: grabField('display_brightness') || displayCommitted.display_brightness,
        light_brightness: grabField('light_brightness') || displayCommitted.light_brightness,
        display_sleep_sec: grabField('display_sleep_sec') || displayCommitted.display_sleep_sec
      };
      await previewDisplayLive(displayCommitted);
      await refreshDeviceStatus();
    }
  } catch (err) {
    setMsg(String(err.message || err), 'err');
  }
}
