import {
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
  sdExpose.checked=!!d.expose;
  setSdIconState(d.expose,d.activity);
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
    'Expose: '+(d.expose?'on':'off')
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
      lines.push("","'; NVS namespace for saved Expose on/off");
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

