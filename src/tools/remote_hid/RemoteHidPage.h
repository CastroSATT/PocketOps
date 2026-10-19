#pragma once

#include <Arduino.h>

// Compact mobile web UI served by Remote HID tool
const char REMOTE_HID_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"/>
<title>PocketOps Remote</title>
<style>
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;font-family:system-ui,sans-serif;background:#0f1419;color:#e7ecf1}
header{padding:12px 14px;background:#1a2330;display:flex;justify-content:space-between;align-items:center}
header h1{margin:0;font-size:16px}
#status{font-size:12px;color:#8aa}
main{padding:12px;display:flex;flex-direction:column;gap:12px}
#pad{height:42vh;min-height:180px;border:1px solid #2b3a4a;border-radius:12px;background:linear-gradient(180deg,#15202b,#0f1419);touch-action:none;position:relative}
#pad:after{content:"TOUCHPAD";position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#314556;font-size:14px;pointer-events:none}
.row{display:flex;gap:8px}
button,.key{flex:1;padding:12px;border:0;border-radius:10px;background:#243447;color:#e7ecf1;font-size:14px}
button:active,.key:active{background:#3a5675}
#keys{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
input#text{width:100%;padding:12px;border-radius:10px;border:1px solid #2b3a4a;background:#101820;color:#e7ecf1;font-size:16px}
.hint{font-size:11px;color:#6b7; text-align:center}
</style>
</head>
<body>
<header>
  <h1>PocketOps Remote</h1>
  <span id="status">connecting…</span>
</header>
<main>
  <div id="pad"></div>
  <div class="row">
    <button id="lbtn">Left</button>
    <button id="rbtn">Right</button>
    <button id="mbtn">Middle</button>
  </div>
  <div id="keys">
    <button data-k="8">Backspace</button>
    <button data-k="13">Enter</button>
    <button data-k="27">Esc</button>
    <button data-k="9">Tab</button>
    <button data-k="37">←</button>
    <button data-k="39">→</button>
    <button data-k="38">↑</button>
    <button data-k="40">↓</button>
    <button data-k="32">Space</button>
  </div>
  <input id="text" placeholder="Tap here · type live…" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false"/>
  <p class="hint">Tap pad = left click · keys stream live</p>
</main>
<script>
const statusEl=document.getElementById('status');
const text=document.getElementById('text');
let ws,lx=0,ly=0,active=false,sx=0,sy=0,downT=0,moved=false;
const TAP_MS=280,TAP_PX=14;
const special={Backspace:8,Enter:13,Tab:9,Escape:27,ArrowLeft:37,ArrowRight:39,ArrowUp:38,ArrowDown:40,' ':32};
function connect(){
  ws=new WebSocket(`ws://${location.hostname}:81/`);
  ws.onopen=()=>{statusEl.textContent='connected';statusEl.style.color='#6d6';text.focus()};
  ws.onclose=()=>{statusEl.textContent='reconnecting…';statusEl.style.color='#d86';setTimeout(connect,1200)};
  ws.onerror=()=>ws.close();
}
function send(o){if(ws&&ws.readyState===1)ws.send(JSON.stringify(o))}
const pad=document.getElementById('pad');
function pos(e){const t=e.touches?e.touches[0]:e;return [t.clientX,t.clientY]}
function leftClick(){send({t:'b',b:0,s:1});send({t:'b',b:0,s:0})}
pad.addEventListener('pointerdown',e=>{
  e.preventDefault();
  active=true;moved=false;downT=Date.now();
  pad.setPointerCapture(e.pointerId);
  [lx,ly]=pos(e);sx=lx;sy=ly;
});
pad.addEventListener('pointerup',()=>{
  if(active&&!moved&&(Date.now()-downT)<=TAP_MS)leftClick();
  active=false;
});
pad.addEventListener('pointercancel',()=>{active=false});
pad.addEventListener('pointermove',e=>{
  if(!active)return;
  const [x,y]=pos(e);
  if(Math.abs(x-sx)>TAP_PX||Math.abs(y-sy)>TAP_PX)moved=true;
  let dx=Math.round(x-lx),dy=Math.round(y-ly);
  lx=x;ly=y;
  if(dx||dy){
    dx=Math.max(-127,Math.min(127,dx));
    dy=Math.max(-127,Math.min(127,dy));
    send({t:'m',dx,dy});
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
  el.addEventListener('click',()=>{send({t:'k',k:parseInt(el.dataset.k,10)});text.focus()});
});
// Desktop / hardware keyboard: each key goes out immediately
text.addEventListener('keydown',e=>{
  if(e.ctrlKey||e.metaKey||e.altKey)return;
  if(special[e.key]!=null){e.preventDefault();send({t:'k',k:special[e.key]});return;}
  if(e.key.length===1){e.preventDefault();send({t:'x',v:e.key});}
});
// Phone soft keyboards: flush whatever appeared in the box
text.addEventListener('input',()=>{
  const v=text.value;
  if(!v)return;
  send({t:'x',v});
  text.value='';
});
connect();
</script>
</body>
</html>
)HTML";
