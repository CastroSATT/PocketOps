import {
  state, markDirty, clearDirty, setMsg, esc, parseIni, fieldHtml, bindDirty, grabField,
  sdBadge, sdExpose, sdExposeBoot, sdInfo, dirMetaFields, dirFolderList, DIR_META
} from './util.js';
import { setFilesTabVisible } from './files.js';

let sdBusy = false;
let sdIconExpose = false;
let sdIconPresent = false;
let sdIconActivity = false;
let sdIconFilesUi = false;
let sdFlashTimer = 0;
let sdBadgeClickBound = false;
/** Last known SD space from status/icon (MB). */
let lastSdSpace = { used: 0, total: 0, ok: false };

function fmtBytes(n) {
  const v = Number(n) || 0;
  if (v >= 1024 * 1024 * 1024) return (v / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
  if (v >= 1024 * 1024) return (v / (1024 * 1024)).toFixed(1) + ' MB';
  if (v >= 1024) return (v / 1024).toFixed(1) + ' KB';
  return Math.round(v) + ' B';
}

function fmtMb(n) {
  const v = Number(n) || 0;
  if (v >= 1024) return (v / 1024).toFixed(1) + ' GB';
  return Math.round(v) + ' MB';
}

function usageLevel(pct) {
  if (pct >= 90) return 'crit';
  if (pct >= 75) return 'warn';
  return '';
}

function setBarFill(el, pct, kind, off) {
  if (!el) return;
  const p = Math.max(0, Math.min(100, Number(pct) || 0));
  el.style.width = (off ? 0 : p) + '%';
  el.className = 'fmt-bar-fill storage-fill' + (kind ? ' ' + kind : '') +
    (off ? ' off' : '') + (off ? '' : (usageLevel(p) ? ' ' + usageLevel(p) : ''));
}

function setHdrMeter(wrapId, fillId, pct, kind, off, title) {
  const wrap = document.getElementById(wrapId);
  const fill = document.getElementById(fillId);
  if (fill) fill.style.width = (off ? 0 : Math.max(0, Math.min(100, Number(pct) || 0))) + '%';
  if (wrap) {
    const p = Math.max(0, Math.min(100, Number(pct) || 0));
    wrap.className = 'hdr-meter' + (kind ? ' ' + kind : '') +
      (off ? ' off' : '') + (!off && usageLevel(p) ? ' ' + usageLevel(p) : '');
    if (title) wrap.title = title;
  }
}

/** Update SD-tab meters + header mini bars from API payload. */
export function applyStorageMeters(d) {
  if (!d) return;

  const lfsUsed = Number(d.lfs_used) || 0;
  const lfsTotal = Number(d.lfs_total) || 0;
  const lfsPct = lfsTotal > 0 ? (lfsUsed * 100) / lfsTotal : 0;
  const lfsLabel = lfsTotal > 0
    ? (fmtBytes(lfsUsed) + ' / ' + fmtBytes(lfsTotal) + ' · ' + Math.round(lfsPct) + '%')
    : '—';

  const lfsLab = document.getElementById('lfs-meter-label');
  if (lfsLab) lfsLab.textContent = lfsLabel;
  setBarFill(document.getElementById('lfs-meter-fill'), lfsPct, 'lfs', lfsTotal <= 0);
  setHdrMeter('hdr-lfs', 'hdr-lfs-fill', lfsPct, 'lfs', lfsTotal <= 0,
    lfsTotal > 0 ? ('Internal: ' + lfsLabel) : 'Internal (chip)');

  let sdOk = !!d.sd_space;
  let sdUsed = Number(d.sd_used_mb);
  let sdTotal = Number(d.sd_total_mb);
  if (!sdOk && d.present && Number(d.total_mb) > 0) {
    sdOk = true;
    sdUsed = Number(d.used_mb) || 0;
    sdTotal = Number(d.total_mb) || 0;
  }
  if (sdOk && sdTotal > 0) {
    lastSdSpace = { used: sdUsed, total: sdTotal, ok: true };
  } else if (!d.present && !d.expose) {
    lastSdSpace = { used: 0, total: 0, ok: false };
  } else if (lastSdSpace.ok) {
    sdOk = true;
    sdUsed = lastSdSpace.used;
    sdTotal = lastSdSpace.total;
  }

  const sdPct = sdOk && sdTotal > 0 ? (sdUsed * 100) / sdTotal : 0;
  const sdLabel = sdOk && sdTotal > 0
    ? (fmtMb(sdUsed) + ' / ' + fmtMb(sdTotal) + ' · ' + Math.round(sdPct) + '% used')
    : (d.present || d.expose ? '…' : 'No card');
  const sdLab = document.getElementById('sd-meter-label');
  if (sdLab) sdLab.textContent = sdLabel;
  setBarFill(document.getElementById('sd-meter-fill'), sdPct, 'sd', !sdOk || sdTotal <= 0);
  setHdrMeter('hdr-sd', 'hdr-sd-fill', sdPct, 'sd', !sdOk || sdTotal <= 0,
    sdOk && sdTotal > 0 ? ('microSD: ' + sdLabel) : 'microSD');
}

function filesPanelActive() {
  const p = document.getElementById('panel-files');
  return !!(p && p.classList.contains('active'));
}

function paintSdBadge() {
  if (!sdBadge) return;
  const filesOpen = sdIconFilesUi || filesPanelActive();
  if (!sdIconPresent && !sdIconExpose && !filesOpen) {
    sdBadge.className = '';
    sdBadge.title = 'No SD card';
    return;
  }
  if (filesOpen) {
    const flashOn = sdIconActivity && ((Date.now() / 90) & 1);
    sdBadge.className = 'files' + (flashOn ? ' flash' : '');
    sdBadge.title = flashOn ? 'Files — SD activity' : 'Files panel open';
    return;
  }
  if (sdIconPresent && !sdIconExpose) {
    sdBadge.className = 'standby';
    sdBadge.title = 'SD inserted — tap to turn USB Expose on';
    return;
  }
  const flashOn = sdIconActivity && ((Date.now() / 90) & 1);
  sdBadge.className = 'on' + (flashOn ? ' flash' : '');
  sdBadge.title = flashOn
    ? 'USB Expose — SD activity (tap to turn off)'
    : 'USB Expose on — tap to turn off';
}
export function setSdIconState(expose, activity, present, filesTab, filesUi) {
  sdIconExpose = !!expose;
  sdIconPresent = !!(present || expose);
  sdIconFilesUi = !!filesUi;
  // Activity for Expose MSC or Files browser access.
  sdIconActivity = !!activity && (!!expose || sdIconFilesUi || filesPanelActive());
  paintSdBadge();
  // Prefer server files_tab (card installed; Expose not required).
  if (filesTab !== undefined) setFilesTabVisible(!!filesTab);
  else if (present !== undefined) setFilesTabVisible(!!present);
}
/** Apply SD half of `/api/chrome` to the header badge. */
export function applyChromeSd(d) {
  if (!d) return;
  setSdIconState(d.expose, d.activity, d.present, d.files_tab, d.files_ui);
  applyStorageMeters(d);
}

/** True when USB Expose is on — chrome poll runs faster for activity flash. */
export function sdExposeActive() {
  return !!sdIconExpose;
}

/** Refresh badge after Files tab enter/leave (DOM class already updated). */
export function refreshSdBadge() {
  paintSdBadge();
}

async function toggleExposeFromBadge() {
  if (sdBusy || filesPanelActive() || sdIconFilesUi) return;
  if (!sdIconPresent && !sdIconExpose) return;
  const wantOn = !sdIconExpose;
  sdBusy = true;
  try {
    const r = await fetch('/api/sd/expose', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: wantOn ? 'on' : 'off'
    });
    const t = await r.text();
    if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
    applySdStatus(JSON.parse(t));
  } catch (err) {
    console.warn('Expose toggle failed', err);
    if (sdBadge) sdBadge.title = String(err.message || err);
  } finally {
    sdBusy = false;
  }
}

/** Badge click + local flash animation. Polling is `/api/chrome` in app.js. */
export function startSdIconWatch() {
  if (sdFlashTimer) clearInterval(sdFlashTimer);
  sdFlashTimer = setInterval(paintSdBadge, 120);
  if (sdBadge && !sdBadgeClickBound) {
    sdBadgeClickBound = true;
    sdBadge.addEventListener('click', () => toggleExposeFromBadge());
    sdBadge.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        toggleExposeFromBadge();
      }
    });
  }
}

/** Required Format folder — device logs when SD is present. Not deletable. */
export const LOCKED_LOGS_PATH = '/Logs';

function isLogsPath(path){
  return String(path||'').replace(/\/+$/,'').toLowerCase()==='/logs';
}

export function folderRow(path, opts){
  const locked=!!(opts&&opts.locked)||isLogsPath(path);
  const row=document.createElement('div');
  row.className='folder-row'+(locked?' locked':'');
  if(locked){
    row.innerHTML=`<input type="text" class="folder-path" value="${esc(LOCKED_LOGS_PATH)}" readonly disabled title="Required — device logs"/>`+
      `<span class="folder-lock" title="Required for device logs">fixed</span>`;
    return row;
  }
  row.innerHTML=`<input type="text" class="folder-path" placeholder="/Folder" value="${esc(path||'')}"/>`+
    `<button type="button" class="icon-btn danger" title="Remove">-</button>`;
  row.querySelector('input').addEventListener('input',markDirty);
  row.querySelector('button').addEventListener('click',()=>{
    row.remove();
    markDirty();
  });
  return row;
}

export function applySdStatus(d){
  if (sdExpose) sdExpose.checked = !!d.expose;
  if (sdExposeBoot) sdExposeBoot.checked = !!d.expose_boot;
  setSdIconState(d.expose,d.activity,d.present, !!d.present, d.files_ui);
  applyStorageMeters(d);
  if(!d.present){
    sdInfo.className='missing';
    sdInfo.textContent=(d.error||'No card')+(d.expose?'\nExpose is on (waiting for card)':'');
    return;
  }
  sdInfo.className='ok';
  sdInfo.textContent=[
    'Type: '+(d.type||'?'),
    'Size: '+d.size_mb+' MB',
    'Total: '+d.total_mb+' MB',
    'Used: '+d.used_mb+' MB',
    'Free: '+d.free_mb+' MB',
    'Folders: '+(d.dirs_ok?'OK':'missing'),
    'Drive name: '+(d.volume_label||'-'),
    'Mounted: '+(d.mounted?'yes':'no'),
    'Expose: '+(d.expose?'on':'off'),
    'Expose on boot: '+(d.expose_boot?'yes':'no')
  ].join('\n');
}

export async function refreshSdStatus(){
  try{
    const r=await fetch('/api/sd/status');
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    applySdStatus(d);
  }catch(err){
    sdInfo.className='missing';
    sdInfo.textContent=String(err.message||err);
  }
}

export function renderSd(text){
  const {kv,paths}=parseIni(text);
  dirMetaFields.innerHTML=DIR_META.map(f=>fieldHtml(f,kv[f.key]||'')).join('');
  bindDirty(dirMetaFields);
  dirFolderList.innerHTML='';
  dirFolderList.appendChild(folderRow(LOCKED_LOGS_PATH,{locked:true}));
  const rest=(paths.length?paths:[]).filter(p=>!isLogsPath(p));
  rest.forEach(p=>dirFolderList.appendChild(folderRow(p)));
  refreshSdStatus();
}

export function serializeSd(){
  const lines=[
    '; PocketOps SD card layout',
    '; Edited via Config -> SD',
    '',
    '; FAT drive name after Format'
  ];
  DIR_META.forEach(f=>{
    if(f.key==='nvs_namespace'){
      lines.push("","'; NVS namespace (Expose-on-boot flag, etc.)");
    }
    lines.push(f.key+'='+grabField(f.key));
  });
  lines.push("","'; Folders created after Format (/Logs is required)");
  lines.push(LOCKED_LOGS_PATH);
  dirFolderList.querySelectorAll('.folder-path').forEach(inp=>{
    if(inp.disabled||inp.readOnly)return;
    let p=inp.value.trim();
    if(!p)return;
    if(!p.startsWith('/'))p='/'+p;
    if(isLogsPath(p))return;
    lines.push(p);
  });
  lines.push('');
  return lines.join('\n');
}

export function bindSdPanel() {
document.getElementById('dir-add').addEventListener('click',()=>{
  dirFolderList.appendChild(folderRow('/'));
  markDirty();
});

document.getElementById('sd-refresh').addEventListener('click',()=>{
  if(sdBusy)return;
  setMsg('Checking card…');
  refreshSdStatus().then(()=>setMsg('Card refreshed','ok'));
});

sdExpose.addEventListener('change',async()=>{
  if(sdBusy){sdExpose.checked=!sdExpose.checked;return;}
  const want=sdExpose.checked;
  sdBusy=true;
  sdExpose.disabled=true;
  setMsg(want?'Turning Expose on…':'Turning Expose off…','warn');
  try{
    const r=await fetch('/api/sd/expose',{
      method:'POST',
      headers:{'Content-Type':'text/plain'},
      body:want?'on':'off'
    });
    const t=await r.text();
    if(!r.ok)throw new Error(t||('HTTP '+r.status));
    applySdStatus(JSON.parse(t));
    setMsg(want?'Expose on':'Expose off','ok');
  }catch(err){
    sdExpose.checked=!want;
    setMsg(String(err.message||err),'err');
  }finally{
    sdBusy=false;
    sdExpose.disabled=false;
  }
});

if (sdExposeBoot) {
  sdExposeBoot.addEventListener('change', async () => {
    if (sdBusy) { sdExposeBoot.checked = !sdExposeBoot.checked; return; }
    const want = sdExposeBoot.checked;
    sdBusy = true;
    sdExposeBoot.disabled = true;
    setMsg(want ? 'Saving: Expose on boot…' : 'Saving: no Expose on boot…', 'warn');
    try {
      const r = await fetch('/api/sd/expose-boot', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: want ? 'on' : 'off'
      });
      const t = await r.text();
      if (!r.ok) throw new Error(t || ('HTTP ' + r.status));
      applySdStatus(JSON.parse(t));
      setMsg(want ? 'Will start Expose on boot' : 'Won’t start Expose on boot', 'ok');
    } catch (err) {
      sdExposeBoot.checked = !want;
      setMsg(String(err.message || err), 'err');
    } finally {
      sdBusy = false;
      sdExposeBoot.disabled = false;
    }
  });
}

function setFormatProgressUi(pct, status){
  const bar=document.getElementById('fmt-bar-fill');
  const pctEl=document.getElementById('fmt-pct');
  const stEl=document.getElementById('fmt-status');
  const p=Math.max(0,Math.min(100,Number(pct)||0));
  if(bar)bar.style.width=p+'%';
  if(pctEl)pctEl.textContent=p+'%';
  if(stEl)stEl.textContent=status||'';
}

function showFormatProgress(on){
  const box=document.getElementById('fmt-progress');
  if(box)box.hidden=!on;
}

async function pollFormatProgress(){
  const r=await fetch('/api/sd/format/progress');
  const d=await r.json();
  if(!r.ok)throw new Error('progress HTTP '+r.status);
  setFormatProgressUi(d.percent, d.status||'');
  return d;
}

document.getElementById('dir-format').addEventListener('click',async()=>{
  if(sdBusy)return;
  if(state.dirty){
    if(!confirm('You have unsaved changes. Format uses the last saved settings, not this form. Continue?'))return;
  }
  if(!confirm('Format microSD now? This erases everything on the card.'))return;
  if(!confirm('Final confirm: wipe card and recreate PocketOps folders?'))return;

  const btn=document.getElementById('dir-format');
  sdBusy=true;
  if(btn)btn.disabled=true;
  sdExpose.disabled=true;
  showFormatProgress(true);
  setFormatProgressUi(0,'starting');
  setMsg('Formatting card…','warn');
  try{
    const r=await fetch('/api/sd/format',{method:'POST',headers:{'Content-Type':'text/plain'},body:'FORMAT'});
    const t=await r.text();
    if(!r.ok)throw new Error(t||('HTTP '+r.status));
    let d;
    for(;;){
      d=await pollFormatProgress();
      if(d.done)break;
      await new Promise(res=>setTimeout(res,250));
    }
    setFormatProgressUi(d.ok?100:d.percent, d.status||'');
    if(!d.ok)throw new Error(d.message||'format fail');
    setMsg(d.message||'Format OK','ok');
    await refreshSdStatus();
  }catch(err){
    setMsg(String(err.message||err),'err');
  }finally{
    sdBusy=false;
    if(btn)btn.disabled=false;
    sdExpose.disabled=false;
  }
});
}

