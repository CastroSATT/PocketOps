import { parseIni, setLinkStatus, textEl } from './util.js';

let ws,lx=0,ly=0,active=false,sx=0,sy=0,downT=0,moved=false,scrollMode=false,lastScrollY=0;
const pointers=new Map();
const TAP_MS=280,TAP_PX=14;
const mouseCfg={speed:100,scrollSpeed:100,twoFingerScroll:true};
const special={Backspace:8,Enter:13,Tab:9,Escape:27,ArrowLeft:37,ArrowRight:39,ArrowUp:38,ArrowDown:40,' ':32};

// Desktop mouse over pad: hover-to-move; leave pad = stop. Touch keeps press-drag.
let mouseArmed=false;
let mouseLx=0,mouseLy=0,mouseHavePos=false;
const mouseHeld=new Set(); // HID button indices currently down from pad mouse

let remoteActive=true; // default tab is Remote
let reconnectTimer=0;

function clearReconnect(){
  if(reconnectTimer){clearTimeout(reconnectTimer);reconnectTimer=0;}
}

/** Open HID WebSocket — only while Remote tab is active. */
export function connect(){
  if(!remoteActive)return;
  clearReconnect();
  if(ws&&(ws.readyState===WebSocket.OPEN||ws.readyState===WebSocket.CONNECTING))return;
  const port=(window.__PO__&&window.__PO__.wsPort)||80;
  setLinkStatus('warn');
  try{
    ws=new WebSocket(`ws://${location.hostname}:${port}/`);
  }catch(_){
    setLinkStatus('bad');
    if(remoteActive)reconnectTimer=setTimeout(connect,1200);
    return;
  }
  ws.onopen=()=>{if(remoteActive)setLinkStatus('ok');};
  ws.onclose=()=>{
    ws=null;
    if(!remoteActive){setLinkStatus('warn');return;}
    setLinkStatus('bad');
    reconnectTimer=setTimeout(connect,1200);
  };
  ws.onerror=()=>{try{ws&&ws.close();}catch(_){}};
}

/** Drop HID link when leaving Remote (LAN / Config / Files get the airwaves). */
export function disconnect(){
  clearReconnect();
  const sock=ws;
  ws=null;
  if(sock){
    sock.onclose=null;
    sock.onerror=null;
    sock.onopen=null;
    try{sock.close();}catch(_){}
  }
  setLinkStatus('warn');
}

export function enterRemotePanel(){
  remoteActive=true;
  connect();
}

export function leaveRemotePanel(){
  remoteActive=false;
  disconnect();
}
function wsOpen(){return ws&&ws.readyState===1}

// Binary HID (firmware HidProtocol::processBinary) — no JSON on the hot path.
// 'M' dx dy wheel | 'B' btn state | 'K' keycode | 'X' + utf8 text
const moveBin=new Uint8Array(4);
moveBin[0]=0x4d; // 'M'
const btnBin=new Uint8Array(3);
btnBin[0]=0x42; // 'B'
const keyBin=new Uint8Array(2);
keyBin[0]=0x4b; // 'K'
const textEnc=new TextEncoder();

function sendMove(dx,dy,w){
  if(!wsOpen())return;
  moveBin[1]=dx;moveBin[2]=dy;moveBin[3]=w||0;
  ws.send(moveBin);
}
function sendBtn(b,s){
  if(!wsOpen())return;
  btnBin[1]=b&0xff;btnBin[2]=s?1:0;
  ws.send(btnBin);
}
function sendKey(k){
  if(!wsOpen())return;
  keyBin[1]=k&0xff;
  ws.send(keyBin);
}
function sendText(v){
  if(!wsOpen()||v==null||v==='')return;
  const u8=textEnc.encode(v);
  const buf=new Uint8Array(1+u8.length);
  buf[0]=0x58; // 'X'
  buf.set(u8,1);
  ws.send(buf);
}

// Coalesce moves to ~1 WebSocket packet per frame + keep sub-pixel leftovers.
// Phones fire many pointermoves; flooding SoftAP makes the cursor feel jerky/slow.
let moveFracX=0,moveFracY=0,movePendX=0,movePendY=0,moveRaf=0,wheelPend=0;
function flushMove(){
  moveRaf=0;
  moveFracX+=movePendX;moveFracY+=movePendY;
  movePendX=0;movePendY=0;
  let dx=Math.trunc(moveFracX);
  let dy=Math.trunc(moveFracY);
  let w=Math.trunc(wheelPend);
  moveFracX-=dx;moveFracY-=dy;wheelPend-=w;
  while(dx||dy||w){
    const sx=Math.max(-127,Math.min(127,dx));
    const sy=Math.max(-127,Math.min(127,dy));
    const sw=Math.max(-127,Math.min(127,w));
    sendMove(sx,sy,sw);
    dx-=sx;dy-=sy;w-=sw;
  }
}
function queueRelMove(x,y,prevX,prevY){
  movePendX+=(x-prevX)*mouseCfg.speed/100;
  movePendY+=(y-prevY)*mouseCfg.speed/100;
  if(!moveRaf)moveRaf=requestAnimationFrame(flushMove);
}
function queueWheel(w){
  wheelPend+=w;
  if(!moveRaf)moveRaf=requestAnimationFrame(flushMove);
}
export async function loadMouseCfg(){
  try{
    const r=await fetch('/api/config/mouse');
    const t=await r.text();
    if(!r.ok)return;
    const {kv}=parseIni(t);
    const sp=parseInt(kv.touchpad_speed||'150',10);
    mouseCfg.speed=Number.isFinite(sp)?Math.max(25,Math.min(300,sp)):100;
    const sc=parseInt(kv.scroll_speed||'100',10);
    mouseCfg.scrollSpeed=Number.isFinite(sc)?Math.max(25,Math.min(300,sc)):100;
    const tf=String(kv.two_finger_scroll==null?'1':kv.two_finger_scroll).toLowerCase();
    mouseCfg.twoFingerScroll=!(tf==='0'||tf==='false'||tf==='off'||tf==='no');
  }catch(_){}
}

/** PointerEvent.button → HID button (0=L, 1=R, 2=M) */
function hidBtn(button){
  if(button===0)return 0;
  if(button===2)return 1;
  if(button===1)return 2;
  return -1;
}

export function startRemote() {
const pad=document.getElementById('pad');
function pos(e){return [e.clientX,e.clientY]}
function avgPointerY(){
  let s=0,n=0;
  pointers.forEach(p=>{s+=p.y;n++;});
  return n?s/n:0;
}
function leftClick(){sendBtn(0,1);sendBtn(0,0)}
function setArmed(on){
  mouseArmed=on;
  pad.classList.toggle('armed',on);
}
function releaseMouseButtons(){
  mouseHeld.forEach(b=>sendBtn(b,0));
  mouseHeld.clear();
}
function disarmMouse(){
  releaseMouseButtons();
  setArmed(false);
  mouseHavePos=false;
}
function isMouse(e){return e.pointerType==='mouse'||e.pointerType==='pen'}

// —— Desktop mouse: active only while cursor is over the pad ——
pad.addEventListener('pointerenter',e=>{
  if(!isMouse(e))return;
  setArmed(true);
  mouseHavePos=false;
});
pad.addEventListener('pointerleave',e=>{
  if(!isMouse(e))return;
  disarmMouse();
});
pad.addEventListener('contextmenu',e=>{
  e.preventDefault();
});
pad.addEventListener('pointerdown',e=>{
  if(!isMouse(e))return;
  e.preventDefault();
  const b=hidBtn(e.button);
  if(b<0)return;
  mouseHeld.add(b);
  sendBtn(b,1);
});
pad.addEventListener('pointerup',e=>{
  if(!isMouse(e))return;
  e.preventDefault();
  const b=hidBtn(e.button);
  if(b<0)return;
  if(mouseHeld.has(b)){
    mouseHeld.delete(b);
    sendBtn(b,0);
  }
});
pad.addEventListener('pointercancel',e=>{
  if(!isMouse(e))return;
  disarmMouse();
});
pad.addEventListener('pointermove',e=>{
  if(!isMouse(e)||!mouseArmed)return;
  e.preventDefault();
  const [x,y]=pos(e);
  if(!mouseHavePos){
    mouseLx=x;mouseLy=y;mouseHavePos=true;
    return;
  }
  queueRelMove(x,y,mouseLx,mouseLy);
  mouseLx=x;mouseLy=y;
});
window.addEventListener('blur',()=>disarmMouse());
document.addEventListener('visibilitychange',()=>{
  if(document.hidden)disarmMouse();
});

// —— Touch / finger: press-drag + tap click (unchanged idea) ——
pad.addEventListener('pointerdown',e=>{
  if(isMouse(e))return;
  e.preventDefault();
  hideSoftKeyboard();
  try{pad.setPointerCapture(e.pointerId);}catch(_){}
  const [x,y]=pos(e);
  pointers.set(e.pointerId,{x,y});
  if(pointers.size>=2&&mouseCfg.twoFingerScroll){
    scrollMode=true;
    active=false;
    moved=true;
    lastScrollY=avgPointerY();
    return;
  }
  active=true;moved=false;downT=Date.now();scrollMode=false;
  lx=x;ly=y;sx=x;sy=y;
});
function endPointer(e){
  if(isMouse(e))return;
  pointers.delete(e.pointerId);
  if(scrollMode){
    if(pointers.size<2)scrollMode=false;
    if(pointers.size===0){active=false;moved=false;}
    return;
  }
  if(active&&pointers.size===0&&!moved&&(Date.now()-downT)<=TAP_MS)leftClick();
  if(pointers.size===0)active=false;
}
pad.addEventListener('pointerup',endPointer);
pad.addEventListener('pointercancel',endPointer);
pad.addEventListener('pointermove',e=>{
  if(isMouse(e))return;
  if(!pointers.has(e.pointerId))return;
  e.preventDefault();
  const [x,y]=pos(e);
  const prev=pointers.get(e.pointerId);
  pointers.set(e.pointerId,{x,y});
  if(scrollMode&&pointers.size>=2&&mouseCfg.twoFingerScroll){
    const cy=avgPointerY();
    let w=Math.round((lastScrollY-cy)*mouseCfg.scrollSpeed/100/6);
    lastScrollY=cy;
    if(w)queueWheel(w);
    return;
  }
  if(!active||pointers.size!==1)return;
  if(Math.abs(x-sx)>TAP_PX||Math.abs(y-sy)>TAP_PX)moved=true;
  // Prefer coalesced samples when the browser batches them (smoother on phones).
  const samples=(typeof e.getCoalescedEvents==='function')?e.getCoalescedEvents():null;
  if(samples&&samples.length>1){
    let px=prev.x,py=prev.y;
    for(const ce of samples){
      const [cx,cy]=pos(ce);
      queueRelMove(cx,cy,px,py);
      px=cx;py=cy;
    }
    lx=x;ly=y;
    return;
  }
  queueRelMove(x,y,lx,ly);
  lx=x;ly=y;
});

/** Dismiss phone soft keyboard when using pad / HID buttons (not the type field). */
function hideSoftKeyboard(){
  if(textEl&&document.activeElement===textEl){
    textEl.blur();
  }
}
function bindBtn(id,btn){
  const el=document.getElementById(id);
  el.addEventListener('pointerdown',e=>{
    e.preventDefault();
    hideSoftKeyboard();
    sendBtn(btn,1);
  });
  el.addEventListener('pointerup',e=>{e.preventDefault();sendBtn(btn,0)});
  el.addEventListener('pointercancel',()=>sendBtn(btn,0));
}
bindBtn('lbtn',0);bindBtn('rbtn',1);bindBtn('mbtn',2);
document.querySelectorAll('#keys button').forEach(el=>{
  el.addEventListener('pointerdown',e=>{
    e.preventDefault();
    hideSoftKeyboard();
    sendKey(parseInt(el.dataset.k,10));
  });
});
textEl.addEventListener('keydown',e=>{
  if(e.ctrlKey||e.metaKey||e.altKey)return;
  if(special[e.key]!=null){e.preventDefault();sendKey(special[e.key]);return;}
  if(e.key.length===1){e.preventDefault();sendText(e.key);}
});
textEl.addEventListener('input',()=>{
  const v=textEl.value;
  if(!v)return;
  sendText(v);
  textEl.value='';
});
}
