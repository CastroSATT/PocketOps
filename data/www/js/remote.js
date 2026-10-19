import { parseIni, statusEl, textEl } from './util.js';


let ws,lx=0,ly=0,active=false,sx=0,sy=0,downT=0,moved=false,scrollMode=false,lastScrollY=0;
const pointers=new Map();
const TAP_MS=280,TAP_PX=14;
const mouseCfg={speed:100,scrollSpeed:100,twoFingerScroll:true};
const special={Backspace:8,Enter:13,Tab:9,Escape:27,ArrowLeft:37,ArrowRight:39,ArrowUp:38,ArrowDown:40,' ':32};
export function connect(){
  const port = (window.__PO__ && window.__PO__.wsPort) || 80;
  ws = new WebSocket(`ws://${location.hostname}:${port}/`);
  ws.onopen=()=>{statusEl.textContent='connected';statusEl.style.color='#6d6'};
  ws.onclose=()=>{statusEl.textContent='reconnecting…';statusEl.style.color='#d86';setTimeout(connect,1200)};
  ws.onerror=()=>ws.close();
}
function send(o){if(ws&&ws.readyState===1)ws.send(JSON.stringify(o))}
export async function loadMouseCfg(){
  try{
    const r=await fetch('/api/config/mouse');
    const t=await r.text();
    if(!r.ok)return;
    const {kv}=parseIni(t);
    const sp=parseInt(kv.touchpad_speed||'100',10);
    mouseCfg.speed=Number.isFinite(sp)?Math.max(25,Math.min(300,sp)):100;
    const sc=parseInt(kv.scroll_speed||'100',10);
    mouseCfg.scrollSpeed=Number.isFinite(sc)?Math.max(25,Math.min(300,sc)):100;
    const tf=String(kv.two_finger_scroll==null?'1':kv.two_finger_scroll).toLowerCase();
    mouseCfg.twoFingerScroll=!(tf==='0'||tf==='false'||tf==='off'||tf==='no');
  }catch(_){}
}
export function startRemote() {
const pad=document.getElementById('pad');
function pos(e){return [e.clientX,e.clientY]}
function avgPointerY(){
  let s=0,n=0;
  pointers.forEach(p=>{s+=p.y;n++;});
  return n?s/n:0;
}
function leftClick(){send({t:'b',b:0,s:1});send({t:'b',b:0,s:0})}
pad.addEventListener('pointerdown',e=>{
  e.preventDefault();
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
  if(!pointers.has(e.pointerId))return;
  const [x,y]=pos(e);
  pointers.set(e.pointerId,{x,y});
  if(scrollMode&&pointers.size>=2&&mouseCfg.twoFingerScroll){
    const cy=avgPointerY();
    let w=Math.round((lastScrollY-cy)*mouseCfg.scrollSpeed/100/6);
    lastScrollY=cy;
    if(w){
      w=Math.max(-127,Math.min(127,w));
      send({t:'m',dx:0,dy:0,w});
    }
    return;
  }
  if(!active||pointers.size!==1)return;
  if(Math.abs(x-sx)>TAP_PX||Math.abs(y-sy)>TAP_PX)moved=true;
  let dx=Math.round((x-lx)*mouseCfg.speed/100);
  let dy=Math.round((y-ly)*mouseCfg.speed/100);
  lx=x;ly=y;
  if(dx||dy){
    dx=Math.max(-127,Math.min(127,dx));
    dy=Math.max(-127,Math.min(127,dy));
    send({t:'m',dx,dy,w:0});
  }
});
function bindBtn(id,btn){
  const el=document.getElementById(id);
  el.addEventListener('pointerdown',e=>{e.preventDefault();send({t:'b',b:btn,s:1})});
  el.addEventListener('pointerup',e=>{e.preventDefault();send({t:'b',b:btn,s:0})});
  el.addEventListener('pointercancel',()=>send({t:'b',b:btn,s:0}));
}
bindBtn('lbtn',0);bindBtn('rbtn',1);bindBtn('mbtn',2);
document.querySelectorAll('#keys button').forEach(el=>{
  el.addEventListener('click',()=>{send({t:'k',k:parseInt(el.dataset.k,10)});textEl.focus()});
});
textEl.addEventListener('keydown',e=>{
  if(e.ctrlKey||e.metaKey||e.altKey)return;
  if(special[e.key]!=null){e.preventDefault();send({t:'k',k:special[e.key]});return;}
  if(e.key.length===1){e.preventDefault();send({t:'x',v:e.key});}
});
textEl.addEventListener('input',()=>{
  const v=textEl.value;
  if(!v)return;
  send({t:'x',v});
  textEl.value='';
});
}
