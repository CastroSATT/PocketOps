import {
  state, clearDirty, setMsg, esc, cssEsc, grabField, setWifiModeBadge, updateStaChrome,
  signalBarsHtml, signalText, wifiLockHtml, wifiSavedHtml, wifiAutoHtml,
  cfgFields, fieldHtml, bindDirty
} from './util.js';

export const SOFTAP_ROW_ID = '__pocketops_softap__';

/** SoftAP settings — permanent Live Wi-Fi row (never deleted). */
export const HOTSPOT_FIELDS = [
  {key:'wifi_ap_ssid',label:'Hotspot SSID'},
  {key:'wifi_ap_pass',label:'Hotspot password',type:'password'},
  {key:'wifi_ap_channel',label:'Hotspot channel',type:'number'},
  {key:'web_port',label:'Web port',type:'number'},
  {key:'ws_port',label:'WebSocket port',type:'number'}
];

export const WIFI_JOIN_HTML = 
  `<div class="wifi-box" id="wifi-join">`+
  `<div class="wifi-head">`+
  `<p class="section-title">Live Wi-Fi</p>`+
  `<button type="button" class="wifi-scan-btn" id="wifi-scan" title="Scan again" aria-label="Scan again">↻</button>`+
  `</div>`+
  `<p id="wifi-status">Checking…</p>`+
  `<div id="wifi-signal" class="sig-line"></div>`+
  `<div class="wifi-list" id="wifi-list"><p class="hint">Scanning…</p></div>`+
  `<p class="hint">SoftAP hotspot is always listed first (tap to open settings). Tap a nearby network to join. Successful joins are saved.</p>`+
  `<div class="cfg-actions">`+
  `<button type="button" class="icon-btn" id="wifi-profiles-btn">Saved profiles (0)</button>`+
  `</div>`+
  `</div>`;
let wifiPollTimer=0;
export let lastWifiScan = [];
export let lastWifiProfiles = [];
export let openWifiSsid = '';
export let wifiDraftPass = '';
export let networkKvCache = {};
export let lastConnectedSsid = '';
export let focusProfileSsid = '';
let lastApStatus={ap_ssid:'',ap_ip:'',ap_up:false,mode:''};
/** Live status painted onto the Connect button for the open row. */
let joinUi={ssid:'',label:'',kind:'',busy:false};

function joinStatusLabel(code,status,fallback){
  const c=String(code||'').toLowerCase();
  const s=String(status||'').toLowerCase();
  const fb=String(fallback||'');
  if(c==='wrong_password'||/wrong\s*password/i.test(fb)||s==='failed')return 'Wrong password';
  if(c==='not_found'||s==='no_ssid')return 'Network not found';
  if(c==='timeout'||s==='disconnected'||s==='idle')return 'Timed out';
  if(c==='lost'||s==='lost')return 'Connection lost';
  if(fb)return fb.slice(0,42);
  return 'Join failed';
}

function setJoinButtonUi(label,kind,opts){
  const busy=!!(opts&&opts.busy);
  const active=(opts&&opts.source)||joinUi.source||'go';
  joinUi={ssid:openWifiSsid||joinUi.ssid,label:label||'',kind:kind||'',busy,source:active};
  const go=document.getElementById('wifi-join-go');
  const saved=document.getElementById('wifi-join-saved');
  const passEl=document.getElementById('wifi-join-pass');
  [go,saved].filter(Boolean).forEach(btn=>{
    btn.classList.remove('join-busy','join-ok','join-err','join-warn');
    btn.disabled=busy;
  });
  const target=active==='saved'&&saved?saved:go;
  if(target){
    if(kind)target.classList.add('join-'+kind);
    target.textContent=label||(target.id==='wifi-join-saved'?'Connect with saved':'Connect & save');
  }
  // Idle companion button keeps its default caption.
  if(!busy){
    if(go&&target!==go){go.classList.remove('join-busy','join-ok','join-err','join-warn');go.textContent='Connect & save';}
    if(saved&&target!==saved){saved.classList.remove('join-busy','join-ok','join-err','join-warn');saved.textContent='Connect with saved';}
  }
  if(passEl)passEl.disabled=busy;
}

function resetJoinButtonUi(){
  joinUi={ssid:'',label:'',kind:'',busy:false,source:'go'};
  const go=document.getElementById('wifi-join-go');
  const saved=document.getElementById('wifi-join-saved');
  const passEl=document.getElementById('wifi-join-pass');
  if(go){
    go.disabled=false;
    go.classList.remove('join-busy','join-ok','join-err','join-warn');
    go.textContent='Connect & save';
  }
  if(saved){
    saved.disabled=false;
    saved.classList.remove('join-busy','join-ok','join-err','join-warn');
    saved.textContent='Connect with saved';
  }
  if(passEl)passEl.disabled=false;
}

function syncSoftApFieldsToCache(){
  HOTSPOT_FIELDS.forEach(f=>{
    const el=document.getElementById('f-'+f.key);
    if(el)networkKvCache[f.key]=el.value.trim();
  });
}

function softApModalOpen(){
  const modal=document.getElementById('wifi-softap-modal');
  return !!(modal&&modal.classList.contains('open'));
}

function fillSoftApModal(){
  const box=document.getElementById('wifi-softap-fields');
  if(!box)return;
  box.innerHTML=HOTSPOT_FIELDS.map(f=>fieldHtml(f,networkKvCache[f.key]||'')).join('');
  bindDirty(box);
}

function setSoftApModalOpen(open){
  const modal=document.getElementById('wifi-softap-modal');
  if(!modal)return;
  if(open){
    if(openWifiSsid){
      wifiDraftPass='';
      resetJoinButtonUi();
      openWifiSsid='';
      paintWifiScan(lastWifiScan,lastConnectedSsid);
    }
    fillSoftApModal();
    modal.classList.add('open');
    modal.setAttribute('aria-hidden','false');
  }else{
    syncSoftApFieldsToCache();
    modal.classList.remove('open');
    modal.setAttribute('aria-hidden','true');
  }
}

function listRowBusy(){
  const passEl=document.getElementById('wifi-join-pass');
  const softFocus=!!(softApModalOpen()&&document.activeElement&&
    document.activeElement.closest&&document.activeElement.closest('#wifi-softap-modal'));
  return !!(openWifiSsid||joinUi.busy||softFocus||(passEl&&document.activeElement===passEl));
}

/** SoftAP-only mode: can view/edit nearby rows, but Connect is blocked. */
function canJoinNetworks(){
  const modeEl=document.getElementById('f-wifi_mode');
  const mode=(modeEl&&modeEl.value)||networkKvCache.wifi_mode||lastApStatus.mode||'ap';
  return mode==='apsta'||mode==='sta';
}

function appendSoftApRow(list){
  const ssid=networkKvCache.wifi_ap_ssid||lastApStatus.ap_ssid||'PocketOps';
  const up=!!lastApStatus.ap_up;
  const row=document.createElement('div');
  row.className='wifi-row softap'+(up?' connected':'');
  row.dataset.softap='1';
  const head=document.createElement('button');
  head.type='button';
  head.className='wifi-item';
  const metaBits=[
    up?'hotspot up':'hotspot',
    lastApStatus.ap_ip?esc(lastApStatus.ap_ip):'',
    'ch '+(networkKvCache.wifi_ap_channel||'-'),
    'edit',
    wifiLockHtml(true)
  ].filter(Boolean).join(' · ');
  head.innerHTML=`<span><span class="name-tag">SOFTAP</span>${esc(ssid)}</span><span class="meta">${metaBits}</span>`;
  head.addEventListener('click',()=>{
    if(joinUi.busy)return;
    setSoftApModalOpen(true);
  });
  row.appendChild(head);
  list.appendChild(row);
}

export function applyWifiStatus(d){
  const el=document.getElementById('wifi-status');
  const sig=document.getElementById('wifi-signal');
  if(!el)return;
  const ok=!!d.sta_connected;
  lastApStatus={
    ap_ssid:d.ap_ssid||'',
    ap_ip:d.ap_ip||'',
    ap_up:!!d.ap_up,
    mode:d.mode||''
  };
  if(d.mode)networkKvCache.wifi_mode=d.mode;
  setWifiModeBadge(d.mode||networkKvCache.wifi_mode||'ap',{
    apUp:!!d.ap_up,
    staUp:!!d.sta_connected
  });
  updateStaChrome(d);
  if(d.ap_ssid&&!networkKvCache.wifi_ap_ssid)networkKvCache.wifi_ap_ssid=d.ap_ssid;
  el.className=ok?'ok':'bad';
  el.textContent=[
    'Mode: '+(d.mode||'-'),
    'Hotspot: '+(d.ap_ssid||'-')+' @ '+(d.ap_ip||'-')+(d.ap_up?' (up)':' (down)'),
    'Joined: '+(ok?((d.connected_ssid||d.sta_ssid||'-')+' @ '+(d.sta_ip||'-')):'not joined'),
    ok&&d.sta_bssid?('AP MAC: '+d.sta_bssid):'',
    'Status: '+(d.status||'-'),
    'Saved profiles: '+(d.profiles!=null?d.profiles:0),
    d.fallback_ready&&!ok&&d.mode==='sta'?'Fallback: SoftAP is up because join-only could not join — use the hotspot to set up Wi-Fi':''
  ].filter(Boolean).join('\n');
  const pbtn=document.getElementById('wifi-profiles-btn');
  if(pbtn&&d.profiles!=null)pbtn.textContent='Saved profiles ('+d.profiles+')';
  if(sig){
    if(ok){
      sig.innerHTML=signalBarsHtml(d.rssi)+'<span>Signal: '+esc(signalText(d.rssi))+'</span>';
    }else{
      sig.innerHTML='<span class="hint">Signal: no LAN link</span>';
    }
  }
  lastConnectedSsid=ok?(d.connected_ssid||d.sta_ssid||''):'';
  // Don't rebuild the scan list while a join/softap row is open / connecting.
  if(!listRowBusy())paintWifiScan(lastWifiScan,lastConnectedSsid);
}

export function syncScanKnownFromProfiles(){
  if(!lastWifiScan.length)return;
  lastWifiScan=lastWifiScan.map(n=>{
    const p=lastWifiProfiles.find(x=>(x.ssid||'')===(n.ssid||''));
    return Object.assign({},n,{
      known:!!p,
      auto_join:p?!!p.auto_join:false
    });
  });
}

export function refreshScanListIfIdle(){
  if(!listRowBusy())paintWifiScan(lastWifiScan,lastConnectedSsid);
}

export function paintWifiScan(nets,connectedSsid){
  const list=document.getElementById('wifi-list');
  if(!list)return;
  lastWifiScan=(nets||[]).slice();
  syncScanKnownFromProfiles();
  list.innerHTML='';
  appendSoftApRow(list);
  if(!lastWifiScan.length){
    const hint=document.createElement('p');
    hint.className='hint';
    hint.textContent='No join networks found — tap ↻ to scan';
    list.appendChild(hint);
    return;
  }
  const joinOk=canJoinNetworks();
  lastWifiScan.slice().sort((a,b)=>(b.rssi||0)-(a.rssi||0)).forEach(n=>{
    const ssid=n.ssid||'';
    const isConn=!!(connectedSsid&&ssid&&ssid===connectedSsid);
    const isOpen=openWifiSsid===ssid;
    const prof=lastWifiProfiles.find(p=>(p.ssid||'')===ssid);
    const known=!!(n.known||prof);
    const autoOn=!!(n.auto_join||(prof&&prof.auto_join));
    const row=document.createElement('div');
    row.className='wifi-row'+(known?' known':'')+(isConn?' connected':'')+(isOpen?' open':'')+(!joinOk?' join-disabled':'');
    const head=document.createElement('button');
    head.type='button';
    head.className='wifi-item';
    const metaBits=[
      signalBarsHtml(n.rssi),
      esc(signalText(n.rssi)||((n.rssi!=null?n.rssi+' dBm':''))),
      wifiLockHtml(!!n.secure),
      known?wifiSavedHtml():'',
      known&&autoOn?wifiAutoHtml():'',
      !joinOk?'SoftAP only':'',
      n.bssid?esc(n.bssid):''
    ].filter(Boolean).join(' · ');
    head.innerHTML=`<span>${esc(ssid||'(hidden)')}</span><span class="meta">${metaBits}</span>`;
    head.addEventListener('click',()=>{
      if(joinUi.busy)return;
      if(isOpen){
        const draft=document.getElementById('wifi-join-pass');
        if(draft)wifiDraftPass=draft.value;
        openWifiSsid='';
        resetJoinButtonUi();
      }else{
        if(softApModalOpen())setSoftApModalOpen(false);
        if(openWifiSsid!==ssid){
          wifiDraftPass='';
          resetJoinButtonUi();
        }
        openWifiSsid=ssid;
      }
      paintWifiScan(lastWifiScan,connectedSsid||lastConnectedSsid);
      const passEl=document.getElementById('wifi-join-pass');
      if(passEl&&!known&&joinOk)passEl.focus();
    });
    row.appendChild(head);
    const body=document.createElement('div');
    body.className='wifi-row-body';
    if(isOpen){
      body.innerHTML=
        (!joinOk
          ?`<p class="hint">SoftAP only mode — you can save password / auto-join options, but Connect is off. Switch mode to SoftAP + join (or Join only) to connect.</p>`
          :(known
            ?`<p class="hint">Saved profile linked — connect with saved password, edit the profile, or set auto-join.</p>`
            :(n.secure
              ?`<p class="hint">Enter the Wi-Fi password, then Connect.</p>`
              :`<p class="hint">Open network — Connect with no password.</p>`)))+
        (known
          ?`<div class="cfg-actions">`+
           `<button type="button" class="icon-btn" id="wifi-join-saved"${!joinOk?' disabled':''}>Connect with saved</button>`+
           `<button type="button" class="icon-btn" id="wifi-join-profile">Open profile</button>`+
           `</div>`+
           `<label class="wifi-check"><input type="checkbox" id="wifi-join-auto"${autoOn?' checked':''}/> Auto-join when this network is found</label>`
          :'')+
        `<div class="pass-wrap">`+
        `<input type="password" id="wifi-join-pass" placeholder="${known?'New password (optional)':'Wi-Fi password'}" autocomplete="current-password" enterkeyhint="go"/>`+
        `<button type="button" class="pass-toggle" id="wifi-join-pass-tog">Show</button>`+
        `</div>`+
        `<button type="button" class="icon-btn" id="wifi-join-go"${!joinOk?' disabled':''}>${!joinOk?'Connect unavailable':'Connect &amp; save'}</button>`;
      row.appendChild(body);
      list.appendChild(row);
      const passEl=document.getElementById('wifi-join-pass');
      const tog=document.getElementById('wifi-join-pass-tog');
      const go=document.getElementById('wifi-join-go');
      const savedBtn=document.getElementById('wifi-join-saved');
      const profBtn=document.getElementById('wifi-join-profile');
      const autoEl=document.getElementById('wifi-join-auto');
      if(passEl){
        passEl.value=wifiDraftPass||'';
        passEl.addEventListener('input',()=>{
          wifiDraftPass=passEl.value;
          if(joinUi.kind==='err'||joinUi.kind==='warn')resetJoinButtonUi();
        });
        passEl.addEventListener('keydown',e=>{
          if(e.key==='Enter'){e.preventDefault();connectWifi(ssid,passEl.value,{source:'go'});}
        });
        if(!known&&!joinUi.busy&&joinOk){
          setTimeout(()=>{try{passEl.focus({preventScroll:true});}catch(_){passEl.focus();}},0);
        }
      }
      if(tog&&passEl){
        tog.addEventListener('click',()=>{
          const show=passEl.type==='password';
          passEl.type=show?'text':'password';
          tog.textContent=show?'Hide':'Show';
        });
      }
      if(go)go.addEventListener('click',()=>connectWifi(ssid,passEl?passEl.value:'',{source:'go'}));
      if(savedBtn)savedBtn.addEventListener('click',()=>connectWifi(ssid,'',{source:'saved'}));
      if(profBtn)profBtn.addEventListener('click',()=>openSavedProfile(ssid));
      if(autoEl){
        autoEl.addEventListener('change',async()=>{
          try{
            await updateProfileAutoJoin(ssid,autoEl.checked);
            setMsg((autoEl.checked?'Auto-join on':'Auto-join off')+' for '+ssid,'ok');
            syncScanKnownFromProfiles();
          }catch(err){
            autoEl.checked=!autoEl.checked;
            setMsg(String(err.message||err),'err');
          }
        });
      }
      // Restore in-progress / last result after a list rebuild.
      if(joinUi.ssid===ssid&&joinUi.label){
        setJoinButtonUi(joinUi.label,joinUi.kind,{busy:joinUi.busy,source:joinUi.source||'go'});
      }
      return;
    }
    row.appendChild(body);
    list.appendChild(row);
  });
}

export function setProfilesModalOpen(open){
  const modal=document.getElementById('wifi-profiles-modal');
  if(!modal)return;
  modal.classList.toggle('open',!!open);
  modal.setAttribute('aria-hidden',open?'false':'true');
}

export async function updateProfileAutoJoin(ssid,on){
  const r=await fetch('/api/wifi/profiles/update',{
    method:'POST',
    headers:{'Content-Type':'text/plain'},
    body:'ssid='+ssid+'\nauto_join='+(on?'1':'0')+'\n'
  });
  const t=await r.text();
  if(!r.ok)throw new Error(t||('HTTP '+r.status));
  lastWifiProfiles=lastWifiProfiles.map(p=>p.ssid===ssid?Object.assign({},p,{auto_join:!!on}):p);
  await refreshWifiProfiles();
}

export async function openSavedProfile(ssid){
  focusProfileSsid=ssid||'';
  await refreshWifiProfiles();
  setProfilesModalOpen(true);
  setTimeout(()=>{
    const el=document.querySelector('[data-profile-ssid="'+cssEsc(ssid||'')+'"]');
    if(el){
      el.classList.add('flash');
      el.scrollIntoView({block:'nearest',behavior:'smooth'});
      setTimeout(()=>el.classList.remove('flash'),1600);
    }
  },60);
}

export async function refreshWifiProfiles(){
  const box=document.getElementById('wifi-profiles-list');
  const pbtn=document.getElementById('wifi-profiles-btn');
  try{
    const r=await fetch('/api/wifi/profiles');
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    const list=d.profiles||[];
    lastWifiProfiles=list.slice();
    if(pbtn)pbtn.textContent='Saved profiles ('+list.length+')';
    if(!box){
      syncScanKnownFromProfiles();
      refreshScanListIfIdle();
      return;
    }
    if(!list.length){
      box.innerHTML='<p class="hint">No saved venues yet — connect from the scan list to remember one.</p>';
      syncScanKnownFromProfiles();
      refreshScanListIfIdle();
      return;
    }
    box.innerHTML='';
    list.forEach(p=>{
      const row=document.createElement('div');
      row.className='profile-row';
      row.dataset.profileSsid=p.ssid||'';
      const name=document.createElement('span');
      name.className='name';
      name.textContent=p.ssid||'';
      const autoLab=document.createElement('label');
      autoLab.className='wifi-check';
      const autoCb=document.createElement('input');
      autoCb.type='checkbox';
      autoCb.checked=!!p.auto_join;
      autoCb.addEventListener('change',async()=>{
        try{
          await updateProfileAutoJoin(p.ssid||'',autoCb.checked);
          setMsg((autoCb.checked?'Auto-join on':'Auto-join off')+' for '+(p.ssid||''),'ok');
        }catch(err){
          autoCb.checked=!autoCb.checked;
          setMsg(String(err.message||err),'err');
        }
      });
      autoLab.appendChild(autoCb);
      autoLab.appendChild(document.createTextNode(' Auto-join when found'));
      const meta=document.createElement('div');
      meta.className='meta';
      meta.textContent='AP MAC: '+(p.bssid||'not stored yet');
      const passWrap=document.createElement('div');
      passWrap.className='pass-wrap pass-edit';
      const passIn=document.createElement('input');
      passIn.type='password';
      passIn.value=p.pass||'';
      passIn.placeholder='Password';
      passIn.autocomplete='off';
      const tog=document.createElement('button');
      tog.type='button';
      tog.className='pass-toggle';
      tog.textContent='Show';
      tog.addEventListener('click',()=>{
        const show=passIn.type==='password';
        passIn.type=show?'text':'password';
        tog.textContent=show?'Hide':'Show';
      });
      passWrap.appendChild(passIn);
      passWrap.appendChild(tog);
      const bssidIn=document.createElement('input');
      bssidIn.type='text';
      bssidIn.value=p.bssid||'';
      bssidIn.placeholder='AP MAC (AA:BB:CC:DD:EE:FF)';
      bssidIn.spellcheck=false;
      const save=document.createElement('button');
      save.type='button';
      save.className='icon-btn';
      save.textContent='Save';
      save.addEventListener('click',async()=>{
        try{
          const ur=await fetch('/api/wifi/profiles/update',{
            method:'POST',
            headers:{'Content-Type':'text/plain'},
            body:'ssid='+(p.ssid||'')+'\npass='+passIn.value+'\nbssid='+bssidIn.value.trim()+
              '\nauto_join='+(autoCb.checked?'1':'0')+'\n'
          });
          const ut=await ur.text();
          if(!ur.ok)throw new Error(ut||('HTTP '+ur.status));
          setMsg('Saved profile '+(p.ssid||''),'ok');
          await refreshWifiProfiles();
          await refreshWifiStatus();
        }catch(err){
          setMsg(String(err.message||err),'err');
        }
      });
      const join=document.createElement('button');
      join.type='button';
      join.className='icon-btn';
      join.textContent='Connect';
      join.addEventListener('click',()=>connectWifi(p.ssid||'',passIn.value));
      const del=document.createElement('button');
      del.type='button';
      del.className='icon-btn danger';
      del.textContent='Del';
      del.addEventListener('click',async()=>{
        if(!confirm('Delete saved Wi-Fi profile "'+p.ssid+'"?'))return;
        try{
          const dr=await fetch('/api/wifi/profiles/delete',{
            method:'POST',
            headers:{'Content-Type':'text/plain'},
            body:'ssid='+p.ssid+'\n'
          });
          const dt=await dr.text();
          if(!dr.ok)throw new Error(dt||('HTTP '+dr.status));
          setMsg('Deleted '+p.ssid,'ok');
          await refreshWifiProfiles();
          await refreshWifiStatus();
        }catch(err){
          setMsg(String(err.message||err),'err');
        }
      });
      row.appendChild(name);
      row.appendChild(autoLab);
      row.appendChild(meta);
      row.appendChild(passWrap);
      row.appendChild(bssidIn);
      row.appendChild(save);
      row.appendChild(join);
      row.appendChild(del);
      box.appendChild(row);
    });
    if(focusProfileSsid){
      const el=box.querySelector('[data-profile-ssid="'+cssEsc(focusProfileSsid)+'"]');
      if(el){
        el.classList.add('flash');
        el.scrollIntoView({block:'nearest'});
      }
      focusProfileSsid='';
    }
    syncScanKnownFromProfiles();
    refreshScanListIfIdle();
  }catch(err){
    if(box)box.innerHTML='<p class="hint">'+esc(String(err.message||err))+'</p>';
  }
}

export async function refreshWifiStatus(){
  try{
    const r=await fetch('/api/wifi/status');
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    applyWifiStatus(d);
  }catch(err){
    const el=document.getElementById('wifi-status');
    if(el){el.className='bad';el.textContent=String(err.message||err);}
  }
}

export async function scanWifi(opts){
  const quiet=!!(opts&&opts.quiet);
  const btn=document.getElementById('wifi-scan');
  const list=document.getElementById('wifi-list');
  // Don't wipe an open password row mid-type.
  if(openWifiSsid){
    if(!quiet)setMsg('Close the network row first, then scan again','warn');
    return;
  }
  if(softApModalOpen())syncSoftApFieldsToCache();
  if(btn){btn.disabled=true;btn.classList.add('spin');}
  if(list)list.innerHTML='<p class="hint">Scanning…</p>';
  if(!quiet)setMsg('Scanning Wi-Fi…','warn');
  try{
    const r=await fetch('/api/wifi/scan');
    const d=await r.json();
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    const nets=d.networks||[];
    if(!list)return;
    let connected='';
    try{
      const sr=await fetch('/api/wifi/status');
      const sd=await sr.json();
      if(sr.ok&&sd.sta_connected)connected=sd.connected_ssid||'';
    }catch(_){}
    paintWifiScan(nets,connected);
    setMsg(nets.length?'Found '+nets.length+' networks':'No networks','ok');
  }catch(err){
    if(list)list.innerHTML='<p class="hint">Scan failed</p>';
    setMsg(String(err.message||err),'err');
  }finally{
    if(btn){btn.disabled=false;btn.classList.remove('spin');}
  }
}

export async function connectWifi(ssid,pass,opts){
  ssid=String(ssid||'').trim();
  pass=String(pass||'');
  const source=(opts&&opts.source)||'go';
  if(!ssid){setMsg('Pick a network from the list first','err');return;}
  if(joinUi.busy)return;
  if(!canJoinNetworks()){
    openWifiSsid=ssid;
    setJoinButtonUi('SoftAP only','warn',{busy:false,source});
    setMsg('Switch to SoftAP + join (or Join only) before connecting to a network','warn');
    return;
  }
  // Keep this row open so the Connect button can show status.
  openWifiSsid=ssid;
  const modeEl=document.getElementById('f-wifi_mode');
  // Join always persists SoftAP+join so boot / Wi-Fi tool keep both paths.
  if(modeEl)modeEl.value='apsta';
  setJoinButtonUi('Connecting…','busy',{busy:true,source});
  setMsg('Joining '+ssid+'…','warn');
  try{
    const r=await fetch('/api/wifi/connect',{
      method:'POST',
      headers:{'Content-Type':'text/plain'},
      body:'ssid='+ssid+'\npass='+pass+'\n'
    });
    const t=await r.text();
    let d={};
    try{d=JSON.parse(t);}catch(_){}
    if(!r.ok){
      const label=joinStatusLabel(d.error,d.status,d.message||d.error||t);
      setJoinButtonUi(label,'err',{busy:false,source});
      setMsg(
        label==='Wrong password'
          ? 'Wrong password — check it and try again (hotspot still up)'
          : (label+' — hotspot should still be up'),
        'err'
      );
      await refreshWifiStatus();
      return;
    }
    networkKvCache.wifi_sta_ssid=d.connected_ssid||d.sta_ssid||ssid;
    networkKvCache.wifi_sta_pass=pass;
    networkKvCache.wifi_mode=d.mode||'apsta';
    if(modeEl)modeEl.value=networkKvCache.wifi_mode;
    setJoinButtonUi('Connected','ok',{busy:false,source});
    setMsg('Joined & saved '+(d.connected_ssid||ssid)+' — mode SoftAP+join kept for boot','ok');
    clearDirty();
    setProfilesModalOpen(false);
    await refreshWifiProfiles();
    applyWifiStatus(d);
    setTimeout(()=>{
      if(openWifiSsid===ssid){
        openWifiSsid='';
        wifiDraftPass='';
        resetJoinButtonUi();
        if(lastWifiScan.length)paintWifiScan(lastWifiScan,lastConnectedSsid);
      }
    },1200);
  }catch(err){
    setJoinButtonUi('Failed','err',{busy:false,source});
    setMsg(String(err.message||err)+' — hotspot should still be up','err');
    await refreshWifiStatus();
  }
}

export function stopWifiPoll(){
  if(wifiPollTimer){clearInterval(wifiPollTimer);wifiPollTimer=0;}
}

export function startWifiPoll(){
  stopWifiPoll();
  wifiPollTimer=setInterval(()=>{
    if(state.currentCfg!=='network')return;
    refreshWifiStatus();
  },3000);
}

export function bindWifiPanel(){
  const scan=document.getElementById('wifi-scan');
  const profilesBtn=document.getElementById('wifi-profiles-btn');
  const profilesClose=document.getElementById('wifi-profiles-close');
  const profilesModal=document.getElementById('wifi-profiles-modal');
  const softApClose=document.getElementById('wifi-softap-close');
  const softApModal=document.getElementById('wifi-softap-modal');
  const softApSave=document.getElementById('wifi-softap-save');
  if(scan)scan.onclick=()=>scanWifi();
  if(profilesBtn)profilesBtn.onclick=async()=>{
    await refreshWifiProfiles();
    setProfilesModalOpen(true);
  };
  if(profilesClose)profilesClose.onclick=()=>setProfilesModalOpen(false);
  if(profilesModal)profilesModal.addEventListener('click',e=>{
    if(e.target===profilesModal)setProfilesModalOpen(false);
  });
  if(softApClose)softApClose.onclick=()=>setSoftApModalOpen(false);
  if(softApModal&&!softApModal.dataset.bound){
    softApModal.dataset.bound='1';
    softApModal.addEventListener('click',e=>{
      if(e.target===softApModal)setSoftApModalOpen(false);
    });
  }
  if(softApSave&&!softApSave.dataset.bound){
    softApSave.dataset.bound='1';
    softApSave.addEventListener('click',async()=>{
      syncSoftApFieldsToCache();
      softApSave.disabled=true;
      softApSave.textContent='Saving…';
      softApSave.classList.add('join-busy');
      softApSave.classList.remove('join-ok','join-err');
      try{
        const {saveNetworkNow}=await import('./config.js');
        await saveNetworkNow('SoftAP settings saved — reconnect if SSID / password / ports changed');
        softApSave.textContent='Saved';
        softApSave.classList.remove('join-busy');
        softApSave.classList.add('join-ok');
        paintWifiScan(lastWifiScan,lastConnectedSsid);
        setTimeout(()=>{
          softApSave.disabled=false;
          softApSave.textContent='Save hotspot settings';
          softApSave.classList.remove('join-ok');
        },1200);
      }catch(err){
        softApSave.disabled=false;
        softApSave.textContent='Save failed';
        softApSave.classList.remove('join-busy');
        softApSave.classList.add('join-err');
        setMsg(String(err.message||err),'err');
      }
    });
  }
}


export function resetJoinDraft() {
  openWifiSsid = '';
  wifiDraftPass = '';
}
export function setNetworkKvCache(kv) {
  networkKvCache = Object.assign({}, kv);
}
export function insertLiveWifiBlock() {
  cfgFields.insertAdjacentHTML('beforeend', WIFI_JOIN_HTML);
}
