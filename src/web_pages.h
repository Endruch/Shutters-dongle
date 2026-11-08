#pragma once
#include <Arduino.h>

static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Shutters Dongle</title>
<style>
:root{--bg:#0d0d0d;--fg:#f4f1ea;--surface:#1c1c1c;--surface2:#242424;--border:#2a2a2a;--accent:#e2863a;--accent2:#f4b860;--muted:#a8a29b}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:15px;margin:0;background:var(--bg);color:var(--fg);line-height:1.4;overflow-x:hidden}
nav{display:flex;flex-wrap:wrap;gap:4px;background:var(--surface);border-bottom:1px solid var(--border);padding:6px 10px}
nav a{color:var(--muted);font-weight:600;text-decoration:none;padding:4px 10px;border-radius:6px}
nav a:hover{color:var(--accent)}
nav a.on{color:#0d0d0d;background:var(--accent)}
main{max-width:900px;margin:0 auto;padding:10px 12px;overflow-wrap:anywhere}
h1,h2{color:var(--accent2);font-family:Georgia,"Times New Roman",serif;font-size:1.1em;margin:14px 0 4px}
h1:first-child,h2:first-child{margin-top:0}
p{color:var(--muted);margin:6px 0}
b,code{color:var(--fg)}
pre{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:8px 10px;margin:6px 0;white-space:pre-wrap;word-break:break-all;color:var(--fg)}
label{display:block;margin-top:8px;font-weight:600;color:var(--fg)}
input,select{font:inherit;color:var(--fg);background:var(--surface2);border:1px solid var(--border);border-radius:8px;padding:5px 8px;max-width:100%}
input[type=text],input[type=password],input[type=number],select{width:100%}
input[type=checkbox]{width:auto;accent-color:var(--accent)}
input[type=file]{width:100%;margin:6px 0}
button{font:inherit;font-weight:600;color:#0d0d0d;background:var(--accent);border:0;border-radius:8px;padding:6px 12px;margin:2px 3px 2px 0;cursor:pointer}
button:hover{background:var(--accent2)}
button.net{display:block;width:100%;text-align:left;background:var(--surface2);color:var(--fg);border:1px solid var(--border)}
button.net:hover{background:var(--surface);color:var(--accent2)}
button.danger{background:#c0392b;color:#fff}
button.danger:hover{background:#d84a3a;color:#fff}
button.ghost{background:var(--surface2);color:var(--fg);border:1px solid var(--border)}
small{color:var(--muted);display:block;margin-top:2px}
.err{color:#e2553a}
.dot{display:inline-block;width:.75em;height:.75em;border-radius:50%;background:var(--muted);margin-right:.5em}
.dot.ok{background:#3fb950}
.dot.bad{background:#c0392b}
.blink{animation:bl 1s steps(1,end) infinite}
@keyframes bl{50%{opacity:.35}}
details{margin-top:2px}
summary{color:var(--muted);cursor:pointer}
.conf{background:var(--surface2);border:1px solid var(--border);border-radius:8px;padding:8px;margin-top:6px}
.sv{color:var(--accent2);font-size:.85em;margin-left:8px}
.top{display:flex;flex-wrap:wrap;align-items:center;gap:4px 12px;margin:0 0 6px}
.top a{color:var(--accent2)}
.top .cnt{color:var(--muted)}
table{border-collapse:collapse;width:100%}
th{color:var(--muted);text-align:left;font-weight:600;padding:3px 6px}
td{border-top:1px solid var(--border);padding:4px 6px;vertical-align:middle}
td:first-child{white-space:nowrap}
@media(max-width:720px){
 table,thead,tbody,tr,td{display:block;width:100%}
 thead{display:none}
 tr{background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:6px 8px;margin:0 0 8px}
 td{border:0;padding:2px 0}
 td[data-l]::before{content:attr(data-l);display:block;color:var(--muted);font-size:.8em}
 button{padding:8px 11px}
}
</style></head><body>
<nav><a href="/index.html">Home</a><a href="/system.html">System</a><a href="/shutter.html">Shutter</a><a href="/log.html">Log</a></nav><main>)HTML";

static const char PAGE_TAIL[] PROGMEM = "</main><script>document.querySelectorAll('nav a').forEach(function(a){var p=location.pathname;if(p==='/')p='/index.html';if(a.getAttribute('href')===p)a.classList.add('on')})</script></body></html>";

static const char PAGE_HOME[] PROGMEM = R"HTML(
<h2>Shutters Dongle</h2>
<p>Bridges <b>Somfy RTS</b> and <b>A-OK</b> shutter motors to MQTT and Home Assistant. Created by Endruch &mdash;
source code, hardware files and the 3D-printable case are on
<a href="https://github.com/Endruch/Shutters-dongle" target="_blank" rel="noopener noreferrer">GitHub</a>. If this dongle is
useful to you, you're welcome to
<a href="https://buymeacoffee.com/migli" target="_blank" rel="noopener noreferrer">buy me a coffee</a>.</p>

<h2>Access &amp; passwords</h2>
<p>Unconfigured or without Wi-Fi the dongle runs its own access point <b>ShuttersDongle-XXXXXX</b> (six characters unique to this dongle, the LED on the board is lit while it is on).
The first password is <code>shutters</code> and must be replaced at the first visit. After it joins your Wi-Fi the access
point turns off; if Wi-Fi is lost for 30 s it comes back for 3 minutes. Two Wi-Fi networks can be configured, the stronger one is used.
Forgot the password? Hold the BOOT button for 10 seconds while the dongle is running: Wi-Fi settings and password are reset (channels and MQTT settings are kept).
The <b>ShuttersDongle-XXXXXX</b> name itself can be replaced with something friendlier, like <code>shutter-kitchen</code>, under Device name on the System page.</p>

<h2>CC1101 wiring</h2>
<p>Radio module and the Wi-Fi status LED. Pins are editable on the System page.</p>
<div style="max-width:380px;margin:6px auto"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 486" width="100%" role="img" aria-label="Wiring: ESP32 GPIO4/5/18/23/19 to CC1101 GDO0/CSN/SCK/MOSI/MISO, 3V3 to VCC, GND to GND; GDO2 not connected. Status LED: GPIO2 through a 330 ohm resistor to the LED anode, cathode to GND."><title>ESP32 + CC1101 wiring, and the status LED</title><style>text{font-family:system-ui,-apple-system,Helvetica,Arial,sans-serif;fill:#f4f1ea}.dim{fill:#a8a29b}.mono{font-family:Menlo,Consolas,monospace}.hd{font-weight:700;font-size:15px}</style><rect x="1" y="1" width="398" height="484" rx="16" fill="#1c1c1c" stroke="#2a2a2a"/><text x="32" y="38" class="hd">ESP32</text><text x="368" y="38" class="hd" text-anchor="end">CC1101</text><text x="32" y="52" font-size="11" class="dim">38-pin board</text><text x="368" y="52" font-size="11" class="dim" text-anchor="end">8-pin header</text><text x="200.0" y="52" font-size="10.5" class="dim" text-anchor="middle">check the pin labels on your module</text><circle cx="130" cy="88" r="4" fill="#8b8b8b"/><circle cx="270" cy="88" r="4" fill="#8b8b8b"/><line x1="136" y1="88" x2="264" y2="88" stroke="#8b8b8b" stroke-width="3" stroke-linecap="round"/><text x="32" y="93" font-size="15" font-weight="600" text-anchor="start">GND</text><text x="368" y="93" font-size="15" font-weight="600" text-anchor="end">GND</text><text x="200.0" y="80" font-size="11" class="dim" text-anchor="middle">ground</text><circle cx="130" cy="126" r="4" fill="#ef6a5f"/><circle cx="270" cy="126" r="4" fill="#ef6a5f"/><line x1="136" y1="126" x2="264" y2="126" stroke="#ef6a5f" stroke-width="3" stroke-linecap="round"/><text x="32" y="131" font-size="15" font-weight="600" text-anchor="start">3V3</text><text x="368" y="131" font-size="15" font-weight="600" text-anchor="end">VCC</text><text x="200.0" y="118" font-size="11" class="dim" text-anchor="middle">3.3 V power</text><circle cx="130" cy="164" r="4" fill="#e2863a"/><circle cx="270" cy="164" r="4" fill="#e2863a"/><line x1="136" y1="164" x2="264" y2="164" stroke="#e2863a" stroke-width="3" stroke-linecap="round"/><text x="32" y="169" font-size="15" font-weight="600" text-anchor="start">GPIO4</text><text x="368" y="169" font-size="15" font-weight="600" text-anchor="end">GDO0</text><text x="200.0" y="156" font-size="11" class="dim" text-anchor="middle">TX data</text><circle cx="130" cy="202" r="4" fill="#b083f0"/><circle cx="270" cy="202" r="4" fill="#b083f0"/><line x1="136" y1="202" x2="264" y2="202" stroke="#b083f0" stroke-width="3" stroke-linecap="round"/><text x="32" y="207" font-size="15" font-weight="600" text-anchor="start">GPIO5</text><text x="368" y="207" font-size="15" font-weight="600" text-anchor="end">CSN</text><text x="200.0" y="194" font-size="11" class="dim" text-anchor="middle">chip select</text><circle cx="130" cy="240" r="4" fill="#e0b830"/><circle cx="270" cy="240" r="4" fill="#e0b830"/><line x1="136" y1="240" x2="264" y2="240" stroke="#e0b830" stroke-width="3" stroke-linecap="round"/><text x="32" y="245" font-size="15" font-weight="600" text-anchor="start">GPIO18</text><text x="368" y="245" font-size="15" font-weight="600" text-anchor="end">SCK</text><text x="200.0" y="232" font-size="11" class="dim" text-anchor="middle">SPI clock</text><circle cx="130" cy="278" r="4" fill="#4c9eee"/><circle cx="270" cy="278" r="4" fill="#4c9eee"/><line x1="136" y1="278" x2="264" y2="278" stroke="#4c9eee" stroke-width="3" stroke-linecap="round"/><text x="32" y="283" font-size="15" font-weight="600" text-anchor="start">GPIO23</text><text x="368" y="283" font-size="15" font-weight="600" text-anchor="end">MOSI (SI)</text><text x="200.0" y="270" font-size="11" class="dim" text-anchor="middle">ESP32 -&gt; module</text><circle cx="130" cy="316" r="4" fill="#3fb950"/><circle cx="270" cy="316" r="4" fill="#3fb950"/><line x1="136" y1="316" x2="264" y2="316" stroke="#3fb950" stroke-width="3" stroke-linecap="round"/><text x="32" y="321" font-size="15" font-weight="600" text-anchor="start">GPIO19</text><text x="368" y="321" font-size="15" font-weight="600" text-anchor="end">MISO (SO)</text><text x="200.0" y="308" font-size="11" class="dim" text-anchor="middle">module -&gt; ESP32</text><text x="368" y="350" font-size="13" class="dim" text-anchor="end">GDO2 &#183; not connected</text><line x1="32" y1="368" x2="368" y2="368" stroke="#2a2a2a" stroke-width="1"/><text x="32" y="396" class="hd" font-size="14">Status LED</text><text x="368" y="396" font-size="11" class="dim" text-anchor="end">lit while own Wi-Fi is on</text><circle cx="32" cy="442" r="4" fill="#f4b860"/><text x="32" y="464" font-size="12" text-anchor="start">GPIO2</text><line x1="32" y1="442" x2="78" y2="442" stroke="#f4b860" stroke-width="3"/><polyline points="78.0,442 89.0,452 100.0,432 111.0,452 122.0,432 133.0,452 144.0,442" fill="none" stroke="#f4b860" stroke-width="3" stroke-linejoin="round"/><text x="111.0" y="424" font-size="11" class="dim" text-anchor="middle">330 &#937;</text><line x1="144" y1="442" x2="184" y2="442" stroke="#f4b860" stroke-width="3"/><polygon points="184,431 184,453 204,442" fill="none" stroke="#f4f1ea" stroke-width="2.5"/><line x1="204" y1="429" x2="204" y2="455" stroke="#f4f1ea" stroke-width="3"/><text x="194" y="470" font-size="10" class="dim" text-anchor="middle">anode +</text><text x="204" y="422" font-size="10" class="dim" text-anchor="middle">cathode &#8722;</text><line x1="204" y1="442" x2="368" y2="442" stroke="#a8a29b" stroke-width="3"/><circle cx="368" cy="442" r="4" fill="#a8a29b"/><text x="368" y="464" font-size="12" text-anchor="end">GND</text></svg></div>

<h2>Home Assistant integration</h2>
<p>With the <b>Shutters Dongle</b> integration installed in Home Assistant (custom_components/shutters_dongle from GitHub, or via HACS)
this dongle is found automatically once it is on your Wi-Fi: it shows up under Settings &rarr; Devices &amp; services as discovered,
you enter this web interface's password and every enabled channel becomes a cover. No MQTT broker needed &mdash; MQTT is off
by default (broker address 0.0.0.0). If you turn it on, leave "HA discovery" on the System page off, otherwise every shutter shows up twice.</p>

<h2>MQTT topics</h2>
<p>Topics are <code>cmd|stat|tele / &lt;device topic&gt; / shutter / &lt;channel&gt;</code>. The device topic is unique per dongle by default, so several dongles never collide. Payloads for <code>cmd</code>:
<b>UP, DOWN, STOP, SHADE, SETSHADE, LEARN</b> (case-insensitive; OPEN and CLOSE are accepted as aliases).</p>
<pre>cmd/%T%/shutter/0 UP
cmd/%T%/shutter/4 DOWN
cmd/%T%/shutter/2 STOP
cmd/%T%/shutter/6 SHADE
cmd/%T%/shutter/12 SETSHADE
cmd/%T%/shutter/12 LEARN</pre>
<p>Status: <code>stat/%T%/shutter/N</code> = 0 open, 90 shade, 100 closed. Availability: <code>tele/%T%/LWT</code> = Online / Offline.
With "HA discovery" enabled on the System page, enabled channels appear in Home Assistant automatically &mdash; the manual example
below is only needed if discovery is off.</p>
<pre>mqtt:
  cover:
    - name: "Living room"
      command_topic: "cmd/%T%/shutter/1"
      state_topic: "stat/%T%/shutter/1"
      availability_topic: "tele/%T%/LWT"
      payload_open: "UP"
      payload_close: "DOWN"
      payload_stop: "STOP"
      state_open: "0"
      state_closed: "100"
      payload_available: "Online"
      payload_not_available: "Offline"</pre>
<script>fetch('/api/config').then(r=>r.json()).then(c=>{document.body.innerHTML=document.body.innerHTML.split('%T%').join(c.topic)})</script>
)HTML";

static const char PAGE_SYSTEM[] PROGMEM = R"HTML(
<form id="f">
<h2>Device name</h2>
<label>name</label><input type="text" name="device_name" placeholder="e.g. kitchen, living-room" autocapitalize="off" autocorrect="off">
<small>Letters, digits and hyphens only.</small>
<h2>Wi-Fi 1</h2>
<label>network name (SSID)</label><input type="text" name="ssid" id="ssid1" autocapitalize="off" autocorrect="off">
<p><button type="button" data-scan="1">Search networks</button></p><div id="nets1"></div>
<label>password</label><input type="password" name="pass" placeholder="(unchanged)">
<h2>Wi-Fi 2 (optional backup router)</h2>
<label>network name (SSID)</label><input type="text" name="ssid2" id="ssid2" autocapitalize="off" autocorrect="off">
<p><button type="button" data-scan="2">Search networks</button></p><div id="nets2"></div>
<label>password</label><input type="password" name="pass2" placeholder="(unchanged)">
<label><input type="checkbox" name="dhcp" value="1"> use DHCP</label>
<small>Static address settings below apply to Wi-Fi 1 only.</small>
<label>ip address</label><input type="text" name="ip">
<label>network mask</label><input type="text" name="mask">
<label>gateway</label><input type="text" name="gw">
<h2>MQTT configuration</h2>
<label>broker address</label><input type="text" name="mqtt_host">
<small>0.0.0.0 (the default) disables MQTT.</small>
<label>broker port</label><input type="number" name="mqtt_port">
<label>username (optional)</label><input type="text" name="mqtt_user">
<label>password (optional)</label><input type="password" name="mqtt_pass" placeholder="(unchanged)">
<label>client id</label><input type="text" name="client_id">
<label>device topic</label><input type="text" name="topic">
<label><input type="checkbox" name="discovery" value="1"> Home Assistant MQTT discovery</label>
<h2>Radio (CC1101)</h2>
<p style="margin:2px 0"><span id="dot" class="dot"></span><b id="rtxt">checking...</b></p>
<label>Somfy RTS frequency MHz</label><input type="text" name="freq">
<label>A-OK frequency MHz</label><input type="text" name="freq_aok">
<label>SCK / MISO / MOSI / CS / GDO0 pins</label>
<input type="text" name="pins">
<p><button type="submit">Save &amp; reboot</button></p>
</form>
<h2>Password</h2>
<form id="pf">
<label>current password</label><input type="password" name="old" autocomplete="current-password">
<label>new password (10-63 characters)</label><input type="password" name="pw" autocomplete="new-password">
<small>Use a random password, not words.</small>
<label>repeat password</label><input type="password" name="pw2" autocomplete="new-password">
<p><button type="submit">Change password</button> <span id="pm" class="err"></span></p>
</form>
<h2>Firmware update</h2>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin"><button type="submit">Upload</button></form>
<script>
const f=document.getElementById('f');
async function radio(){
 try{const r=await(await fetch('/api/radio')).json();
  document.getElementById('dot').className='dot '+(r.ok?'ok':'bad');
  document.getElementById('rtxt').textContent=r.ok?'CC1101 connected (chip version 0x'+r.ver.toString(16).toUpperCase().padStart(2,'0')+')':'CC1101 not found - check wiring and power';
 }catch(e){document.getElementById('dot').className='dot';document.getElementById('rtxt').textContent='no link to the dongle'}
 setTimeout(radio,3000)}
radio();
document.querySelectorAll('[data-scan]').forEach(b=>b.onclick=()=>scan(b.dataset.scan,b));
async function scan(n,btn){
 const box=document.getElementById('nets'+n),inp=document.getElementById('ssid'+n);
 btn.disabled=true;btn.textContent='Searching...';box.innerHTML='';
 await fetch('/api/scan/start',{method:'POST'});
 for(let i=0;i<20;i++){
  await new Promise(r=>setTimeout(r,1000));
  const r=await(await fetch('/api/scan')).json();
  if(r.status==='done'){
   r.nets.sort((a,b)=>b.rssi-a.rssi);
   r.nets.forEach(x=>{const e=document.createElement('button');e.type='button';e.className='net';
    e.textContent=x.ssid+'  ('+x.rssi+' dBm'+(x.lock?', secured':', open')+')';
    e.onclick=()=>{inp.value=x.ssid;box.innerHTML=''};box.appendChild(e)});
   if(!r.nets.length)box.textContent='No networks found.';
   break;
  }
 }
 btn.disabled=false;btn.textContent='Search networks';
}
document.getElementById('pf').onsubmit=async e=>{e.preventDefault();
 const r=await fetch('/api/setpw',{method:'POST',body:new URLSearchParams(new FormData(e.target))});
 if(r.ok){document.body.innerHTML='<main><h2>Password changed</h2><p>The dongle is restarting. Reconnect to its Wi-Fi with the new password.</p></main>'}
 else document.getElementById('pm').textContent=await r.text()};
fetch('/api/config').then(r=>r.json()).then(c=>{
 for(const k in c){const e=f.elements[k];if(!e)continue;if(e.type==='checkbox')e.checked=!!c[k];else e.value=c[k];}
});
f.onsubmit=async e=>{e.preventDefault();
 const d=new URLSearchParams(new FormData(f));
 await fetch('/api/config',{method:'POST',body:d});
 document.body.innerHTML='<main><h2>Saved. Rebooting...</h2></main>';setTimeout(()=>location.href='/',8000)};
</script>
)HTML";

static const char PAGE_SHUTTER[] PROGMEM = R"HTML(
<div class="top">
<span class="cnt">Channels: <b id="cnt">0</b> / <span id="tot">0</span></span>
</div>
<p style="margin:0 0 4px"><button id="add">+ Add channel</button> <label style="display:inline-block;margin:0 0 0 6px"><input type="checkbox" id="all"> show unused channels</label></p>
<table id="t"><thead><tr><th>#</th><th>name</th><th>type</th><th></th><th></th></tr></thead><tbody></tbody></table>
<script>
const tb=document.querySelector('#t tbody');
const B=(c,t,x)=>`<button data-c=${c} ${x||''}>${t}</button>`;
function row(s,open){
 const tr=document.createElement('tr');tr.dataset.ch=s.ch;
 tr.innerHTML=`<td><b>Channel ${s.ch}</b><span class=sv></span></td>
 <td data-l="name"><input type=text value="${s.name}"></td>
 <td data-l="type"><select><option value=0 ${s.proto?'':'selected'}>Somfy RTS</option><option value=1 ${s.proto?'selected':''}>A-OK</option></select></td>
 <td>${B('UP','&#9650;')}${B('STOP','&#9632;')}${B('DOWN','&#9660;')}${B('SHADE','Shade')}${B('SETSHADE','Set shade')}${B('LEARN','Learn','class=learn')}</td>
 <td><details ${open?'open':''}><summary>more</summary>
  <label>remote ID (hex)</label><input type=text value="${s.id}">
  <label><input type=checkbox ${s.en?'checked':''}> enabled</label>
  <small>rolling code (Somfy) / channel address (A-OK). Leave as is unless cloning a remote or grouping A-OK channels.</small>
  <input type=text value="${s.code}">
  <div class=pairsel><small>A-OK pairing command (UP is the official one; PROGRAM for some motors)</small>
  <select><option value=0 ${s.pair?'':'selected'}>UP</option><option value=1 ${s.pair?'selected':''}>PROGRAM</option></select></div>
  <p><button class="danger" data-c=DEL>Delete motor</button></p>
  <div class=conf hidden><b>Delete channel ${s.ch}?</b>
   <small>"Delete and unpair" makes the dongle do what a remote does to remove itself from the motor (channel removed from every motor on it, limits are kept). Takes a few seconds.</small>
   <p><button class="danger" data-c=UNPAIR>Delete and unpair from motor</button>
   <button class="ghost" data-c=DELONLY>Delete from dongle only</button>
   <button class="ghost" data-c=CANCEL>Cancel</button></p></div>
 </details></td>`;
 const inp=tr.querySelectorAll('input'),sels=tr.querySelectorAll('select');
 const name=inp[0],id=inp[1],en=inp[2],code=inp[3],proto=sels[0],pair=sels[1];
 const sv=tr.querySelector('.sv');
 const save=async()=>{await fetch('/api/shutter/save',{method:'POST',body:new URLSearchParams({ch:s.ch,en:en.checked?1:0,name:name.value,proto:proto.value,pair:pair.value,id:id.value,code:code.value})});
  sv.textContent='saved';setTimeout(()=>sv.textContent='',1500);};
 [name,id,en,code,proto,pair].forEach(e=>e.onchange=save);
 en.addEventListener('change',()=>{s.en=en.checked;count()});
 const upd=()=>{const so=proto.value==='0';
  tr.querySelectorAll('[data-c=SHADE],[data-c=SETSHADE]').forEach(b=>b.style.display=so?'':'none');
  tr.querySelector('.pairsel').style.display=so?'none':'';};
 proto.addEventListener('change',upd);upd();
 const conf=tr.querySelector('.conf');
 tr.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
  const c=b.dataset.c;
  if(c==='DEL'){conf.hidden=false;return}
  if(c==='CANCEL'){conf.hidden=true;return}
  if(c==='DELONLY'){await fetch('/api/shutter/delete',{method:'POST',body:new URLSearchParams({ch:s.ch})});load();return}
  if(c==='UNPAIR'){await save();conf.hidden=true;
   await fetch('/api/shutter/unpair',{method:'POST',body:new URLSearchParams({ch:s.ch})});tick();return}
  await save();
  await fetch('/api/shutter/cmd',{method:'POST',body:new URLSearchParams({ch:s.ch,cmd:c})});
  if(c==='LEARN'){if(proto.value==='0')tick();else flash(b,'Sent',3000);}
 });
 return tr;
}
function flash(b,t,ms){const o=b.textContent;b.classList.add('blink');b.textContent=t;setTimeout(()=>{b.classList.remove('blink');b.textContent=o},ms)}
let shown=0,data=[],active=false;
const all=document.getElementById('all');
function count(){document.getElementById('cnt').textContent=data.filter(x=>x.en).length;document.getElementById('tot').textContent=data.length}
function render(){count();tb.innerHTML='';data.forEach(s=>{if(s.en||all.checked||s.ch<shown)tb.appendChild(row(s,!s.en))})}
function load(){fetch('/api/shutters').then(r=>r.json()).then(a=>{data=a;render();tick()})}
all.onchange=render;
document.getElementById('add').onclick=()=>{const n=data.find(s=>!s.en&&s.ch>=shown);if(n){shown=n.ch+1;render()}};
const LBL={learn:'Learn',unpair:'Delete motor'};
async function tick(){
 const r=await(await fetch('/api/seq')).json();
 document.querySelectorAll('button.learn,button.danger[data-c=DEL]').forEach(b=>{if(b.dataset.t){b.classList.remove('blink');b.textContent=b.dataset.t;delete b.dataset.t}});
 if(r.active){
  active=true;
  const tr=document.querySelector(`tr[data-ch="${r.ch}"]`);
  if(tr){const b=tr.querySelector(r.kind==='learn'?'button.learn':'button.danger[data-c=DEL]');
   if(b){b.dataset.t=LBL[r.kind];b.classList.add('blink');
    b.textContent=(r.kind==='learn'?(r.heard?'PROG heard... ':'Waiting for PROG... '):'Unpairing... ')+r.left;}}
  setTimeout(tick,700);
 }else if(active){active=false;load()}
}
load();
</script>
)HTML";

static const char PAGE_LOG[] PROGMEM = R"HTML(
<pre id="l" style="margin:0;height:calc(100vh - 64px);height:calc(100dvh - 64px);overflow:auto"></pre>
<script>
const l=document.getElementById('l');
function u(){fetch('/api/log').then(r=>r.text()).then(t=>{
 const end=l.scrollTop+l.clientHeight>=l.scrollHeight-8;
 l.textContent=t;if(end)l.scrollTop=l.scrollHeight})}
u();setInterval(u,2000);
</script>
)HTML";

static const char PAGE_LOGIN[] PROGMEM = R"HTML(
<style>nav{display:none}</style>
<h2>Shutters dongle</h2>
<form id="f">
<label>Password</label><input type="password" name="password" autofocus autocomplete="current-password">
<p><button type="submit">Enter</button> <span id="m" class="err"></span></p>
</form>
<script>
document.getElementById('f').onsubmit=async e=>{e.preventDefault();
 const r=await fetch('/api/login',{method:'POST',body:new URLSearchParams(new FormData(e.target))});
 if(r.ok)location.href='/';else document.getElementById('m').textContent=r.status===429?'Too many attempts, wait a moment':'Wrong password';};
</script>
)HTML";

static const char PAGE_SETPW[] PROGMEM = R"HTML(
<style>nav{display:none}</style>
<h2>Set a password</h2>
<p>This is the first start. Choose a password (10 or more characters). It protects the dongle's own Wi-Fi
access point <b>ShuttersDongle-XXXXXX</b> and this web interface. The dongle will restart afterwards and the access point
will use the new password, so reconnect your phone with it (you may need to "forget" the old network entry first).</p>
<form id="f">
<label>new password</label><input type="password" name="pw" autocomplete="new-password">
<small>Use a random password, not words.</small>
<label>repeat password</label><input type="password" name="pw2" autocomplete="new-password">
<p><button type="submit">Save password</button> <span id="m" class="err"></span></p>
</form>
<script>
document.getElementById('f').onsubmit=async e=>{e.preventDefault();
 const r=await fetch('/api/setpw',{method:'POST',body:new URLSearchParams(new FormData(e.target))});
 if(r.ok)document.body.innerHTML='<main><h2>Saved</h2><p>The dongle is restarting. Reconnect to <b>ShuttersDongle-XXXXXX</b> with your new password, then open http://192.168.4.1/</p></main>';
 else document.getElementById('m').textContent=await r.text();};
</script>
)HTML";
