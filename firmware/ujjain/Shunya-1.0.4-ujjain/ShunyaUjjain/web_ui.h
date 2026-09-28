#pragma once
// Gurgaon HTML/CSS/SVG dial port, using the existing Ujjain JSON API.
static const char WEB_UI[] PROGMEM = R"SHUNYA(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Shunya Ujjain</title><style>
:root{--bg:#0a0a0a;--card:#161616;--bd:#262626;--tx:#f5f5f5;--dim:#888;--ac:#3b82f6;--on:#10b981;--of:#3a3a3a;--bad:#e0626f;--r:18px}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
html,body{min-height:100%}
body{font:clamp(14px,1.4vw,16px) -apple-system,system-ui,sans-serif;background:var(--bg);color:var(--tx);padding:clamp(12px,2.5vw,24px);max-width:min(760px,100%);margin:0 auto;overscroll-behavior:contain}
.hdr{display:flex;justify-content:space-between;align-items:center;padding:6px 6px clamp(14px,2vw,22px);font-size:clamp(11px,1.1vw,12px);color:var(--dim);letter-spacing:1.2px;text-transform:uppercase}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;background:var(--on);margin-right:8px;animation:pulse 2s infinite}
.hdr-stat{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
.chip{font-size:10px;letter-spacing:1px;padding:4px 8px;border-radius:8px;background:var(--of);color:var(--dim);font-weight:600;text-transform:uppercase}
.chip.on{background:rgba(16,185,129,.18);color:var(--on)}
.chip.purge{background:var(--card);color:var(--dim);text-transform:none;letter-spacing:.5px;border:1px solid var(--bd)}
.chip.purge.active{background:rgba(224,98,111,.18);color:var(--bad);animation:pulse 1.4s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}

/* SENSORS */
.sensors{display:grid;grid-template-columns:1fr 1fr;gap:clamp(10px,1.5vw,16px);margin-bottom:clamp(14px,2vw,20px)}
.sensor{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:clamp(18px,3vw,26px) clamp(16px,2.5vw,22px);position:relative;overflow:hidden}
.s-l{font-size:clamp(10px,1vw,11px);color:var(--dim);letter-spacing:1.6px;text-transform:uppercase;margin-bottom:clamp(10px,1.5vw,14px)}
.s-rh{font-size:clamp(44px,9vw,68px);font-weight:200;line-height:1;letter-spacing:-2px}
.s-rh .u{font-size:clamp(20px,4vw,28px);color:var(--dim);font-weight:300;margin-left:3px;letter-spacing:0}
.s-temp{font-size:clamp(13px,1.6vw,16px);color:var(--dim);margin-top:clamp(8px,1.2vw,10px);font-weight:400}
.sensor.bad .s-rh,.sensor.bad .s-temp{opacity:.35}
.sensor.bad::after{content:'OFFLINE';position:absolute;top:14px;right:14px;font-size:9px;letter-spacing:1.5px;color:var(--bad);font-weight:600}

/* Cards */
.card{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:clamp(18px,2.5vw,24px);margin-bottom:clamp(12px,1.8vw,16px)}
.card-t{font-size:clamp(10px,1.1vw,11px);color:var(--dim);letter-spacing:1.6px;text-transform:uppercase;margin-bottom:clamp(12px,2vw,16px);display:flex;justify-content:space-between;align-items:center;gap:10px}
.hint{font-size:clamp(9px,1vw,10px);color:#555;letter-spacing:.8px;text-transform:none;font-weight:400}

/* Dials */
.dial-wrap{position:relative;width:clamp(220px,55vw,280px);height:clamp(220px,55vw,280px);margin:0 auto;touch-action:none;user-select:none}
.dial-svg{width:100%;height:100%;cursor:grab;display:block}
.dial-svg:active{cursor:grabbing}
.dial-arc{fill:none;stroke-linecap:round;stroke-width:9}
.dial-bg{stroke:#222}
.dial-val-arc{stroke:var(--ac)}
.dial-cur-tick{fill:var(--dim)}
.dial-h{fill:#fff;stroke:var(--ac);stroke-width:3;filter:drop-shadow(0 2px 10px rgba(0,0,0,.6));transition:r .15s}
.dial-svg:active .dial-h{r:17}
.dial-c{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;pointer-events:none}
.dial-num{font-size:clamp(52px,11vw,72px);font-weight:200;line-height:1;letter-spacing:-3px}
.dial-unit{font-size:clamp(20px,4vw,26px);color:var(--dim);font-weight:300;margin-left:3px}
.dial-now{font-size:clamp(10px,1.1vw,11px);color:var(--dim);margin-top:clamp(8px,1.2vw,12px);letter-spacing:1.2px;text-transform:uppercase}

/* Devices */
.dev{display:flex;align-items:center;justify-content:space-between;padding:clamp(12px,1.8vw,16px) 2px;border-bottom:1px solid var(--bd);gap:10px;background:var(--card);position:relative}
.dev:last-child{border-bottom:none;padding-bottom:2px}
.dev-l{display:flex;align-items:center;gap:12px;min-width:0;flex:1}
.dev-d{width:9px;height:9px;border-radius:50%;background:var(--of);transition:background .3s,box-shadow .3s;flex-shrink:0}
.dev-d.on{background:var(--on);box-shadow:0 0 10px rgba(16,185,129,.55)}
.dev-i{display:flex;flex-direction:column;min-width:0}
.dev-n{font-size:clamp(14px,1.5vw,16px);font-weight:500}
.dev-s{font-size:clamp(10px,1.05vw,11px);color:var(--dim);letter-spacing:1px;text-transform:uppercase;margin-top:3px}

/* Mode pill */
.pill{display:grid;grid-template-columns:repeat(3,1fr);background:#0c0c0c;border:1px solid var(--bd);border-radius:11px;padding:3px;position:relative;width:clamp(132px,17vw,156px);flex-shrink:0}
.pill button{background:transparent;border:0;color:var(--dim);font-size:clamp(10px,1.05vw,11px);font-weight:600;padding:7px 0;letter-spacing:1.2px;cursor:pointer;border-radius:8px;z-index:1;position:relative;transition:color .2s;font-family:inherit}
.pill button.act{color:#fff}
.pill .ind{position:absolute;top:3px;bottom:3px;width:calc(33.33% - 2px);left:3px;border-radius:8px;transition:transform .28s cubic-bezier(.4,0,.2,1),background .28s}
.pill[data-m="auto"] .ind{transform:translateX(0);background:var(--ac)}
.pill[data-m="on"] .ind{transform:translateX(calc(100% + 2px));background:var(--on)}
.pill[data-m="off"] .ind{transform:translateX(calc(200% + 4px));background:#666}

/* Advanced */
details summary{cursor:pointer;font-size:clamp(10px,1.1vw,11px);color:var(--dim);letter-spacing:1.5px;text-transform:uppercase;padding:clamp(12px,2vw,16px) clamp(16px,2.2vw,20px);background:var(--card);border:1px solid var(--bd);border-radius:var(--r);list-style:none;display:flex;justify-content:space-between;align-items:center;gap:10px}
details summary::-webkit-details-marker{display:none}
details[open] summary{border-radius:var(--r) var(--r) 0 0;border-bottom:none}
.saved{font-size:10px;color:var(--on);letter-spacing:1.5px;opacity:0;transition:opacity .25s}
.saved.show{opacity:1}
.adv{padding:8px clamp(16px,2.2vw,20px) clamp(14px,2vw,18px);background:var(--card);border:1px solid var(--bd);border-top:none;border-radius:0 0 var(--r) var(--r)}
.adv-grp{font-size:clamp(9px,1vw,10px);color:#666;letter-spacing:1.8px;text-transform:uppercase;margin:clamp(12px,1.8vw,16px) 0 4px;font-weight:600}
.adv-grp:first-child{margin-top:0}
.adv-r{display:flex;justify-content:space-between;align-items:center;padding:8px 0;gap:14px}
.adv-r label{font-size:clamp(12px,1.3vw,14px);color:#bbb}
.adv-r input{background:#0c0c0c;border:1px solid var(--bd);color:var(--tx);padding:8px 12px;border-radius:8px;width:88px;font-size:clamp(12px,1.3vw,14px);text-align:right;font-family:inherit}
.adv-r input:focus{outline:none;border-color:var(--ac)}
.note{font-size:clamp(10px,1.05vw,11px);color:var(--dim);margin-top:10px;line-height:1.55}

/* Wider screens — dials side by side */
@media (min-width:760px){
  .dials{display:grid;grid-template-columns:1fr 1fr;gap:16px}
  .dials .card{margin-bottom:0}
  .dials-wrap{margin-bottom:clamp(12px,1.8vw,16px)}
}
@media (max-width:340px){
  .sensors{grid-template-columns:1fr}
}
:root{--amber:#f9a600}
[hidden]{display:none!important}button,input,select{font:inherit}button,.btn{cursor:pointer;background:#202020;border:1px solid var(--bd);border-radius:9px;padding:10px 14px;color:var(--tx);text-decoration:none}button:hover,.btn:hover{border-color:var(--dim)}button:disabled{opacity:.4;cursor:default}button:focus-visible,a:focus-visible,summary:focus-visible{outline:2px solid var(--ac);outline-offset:3px}
input,select{max-width:100%;background:#0c0c0c;color:var(--tx);border:1px solid var(--bd);border-radius:8px;padding:10px}input:not([type=checkbox]),select{width:100%}input:focus,select:focus{outline:1px solid var(--ac)}label{display:block;color:#bbb;margin:12px 0 6px}h2{font-size:18px;font-weight:500;margin-bottom:12px}p,small{line-height:1.6;margin:10px 0;color:var(--dim)}pre{white-space:pre-wrap;overflow-wrap:anywhere;color:var(--dim);line-height:1.7;font-size:12px}a{color:var(--ac)}
.hdr{gap:10px;flex-wrap:wrap}.hdr-name{display:flex;align-items:center}.hdr-stat{letter-spacing:0}.dot.off{background:var(--amber);animation:none}#name{max-width:240px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.version{margin-bottom:12px;color:var(--dim);font-size:11px}.nav{display:flex;gap:5px;overflow:auto;margin-bottom:18px;padding-bottom:4px}.nav button{padding:8px 11px;font-size:11px;white-space:nowrap;background:transparent}.nav .active{background:var(--card);color:var(--ac)}.tab{display:none}.tab.visible{display:block}
.banner{border:1px solid var(--amber);border-radius:12px;color:var(--amber);background:rgba(249,166,0,.07);padding:12px 14px;margin-bottom:14px;line-height:1.5;font-size:12px}.banner.fault{color:var(--bad);border-color:var(--bad);background:rgba(224,98,111,.08)}.banner.notice{color:var(--dim);border-color:var(--bd);background:var(--card)}
.dev-s{letter-spacing:0;text-transform:none;line-height:1.5;overflow-wrap:anywhere}.pill.two{grid-template-columns:repeat(2,1fr);width:clamp(108px,15vw,130px)}.pill.two .ind{width:calc(50% - 3px)}.pill.two[data-m=off] .ind{transform:translateX(calc(100% + 0px))}.pill[data-m=mist] .ind{transform:translateX(calc(100% + 2px));background:var(--amber)}.adv-r label{margin:0}.adv-r input{width:100px}.adv-r select{width:130px}.buttons,.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.primary{background:var(--ac)}.danger{color:var(--bad)}.scroll{overflow:auto}table{width:100%;border-collapse:collapse;font-size:11px;white-space:nowrap}td,th{text-align:left;padding:9px;border-bottom:1px solid var(--bd)}canvas{width:100%;height:auto;margin:15px 0}#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#262626;color:var(--tx);padding:12px 20px;border-radius:12px;max-width:90%;z-index:4;box-shadow:0 5px 30px #0008}footer{margin:24px 0 6px;color:var(--dim);font-size:11px;text-align:center}.auth{border-color:var(--ac)}.auth p{font-size:12px}.dial-wrap:focus-visible{outline:2px solid var(--ac);border-radius:50%}
@media(max-width:400px){.dev{flex-wrap:wrap}.dev-l{flex-basis:100%}.pill{margin-left:21px;width:170px}.pill.two{width:130px}.hdr-stat{width:100%}}
</style></head><body>
<div id="alerts" role="status" aria-live="polite" hidden></div>
<div class="hdr"><span class="hdr-name"><span id="linkDot" class="dot off"></span><span id="name">Shunya Ujjain</span></span><div class="hdr-stat"><span class="chip" id="chip-heater">HEAT</span><span class="chip" id="chip-humidifier">HUM</span><span class="chip" id="chip-mist">MIST</span><span class="chip" id="chip-exhaust">EXH</span><span class="chip purge" id="state">CONNECTING</span></div><span id="ip">—</span></div>
<div class="version"><span id="connection">Connecting to chamber…</span> · <span id="footer">1.0.2-ujjain</span></div>
<nav id="nav" class="nav" aria-label="Sections"><button data-tab="dashboard">Dashboard</button><button data-tab="history">History</button><button data-tab="eventsSection">Events</button><button data-tab="wifi">Wi-Fi</button><button data-tab="system">System</button><button data-tab="help">Help</button></nav>
<div class="card auth" id="authPrompt" hidden><h2>Local control code</h2><p id="authMessage">Needed only for Wi-Fi, firmware, restart and reset actions. The code is at the bottom of the chamber screen.</p><form id="authForm"><label for="key">8-character code</label><input id="key" type="password" autocomplete="off" maxlength="8" required><p class="buttons"><button class="primary">Unlock controls</button><button type="button" id="authCancel">Continue viewing</button></p></form></div>
<main><section class="tab visible" id="dashboard"><div class="sensors"><div class="sensor" id="sen-top"><div class="s-l">TOP</div><div class="s-rh"><span id="topRH">--</span><span class="u">%</span></div><div class="s-temp"><span id="topT">--</span> &deg;C</div></div><div class="sensor" id="sen-bottom"><div class="s-l">BOTTOM</div><div class="s-rh"><span id="bottomRH">--</span><span class="u">%</span></div><div class="s-temp"><span id="bottomT">--</span> &deg;C</div></div></div><div class="dials-wrap"><div class="dials"><div class="card"><div class="card-t"><span>Temperature target</span><span class="hint">20–35°C</span></div><div class="dial-wrap" id="dt" tabindex="0" role="slider" aria-label="Temperature target" aria-valuemin="20" aria-valuemax="35" aria-valuenow="27"><svg class="dial-svg" viewBox="-120 -120 240 240" aria-hidden="true"><path class="dial-arc dial-bg" d="M -60.81 60.81 A 86 86 0 1 1 60.81 60.81"/><path class="dial-arc dial-val-arc"/><circle class="dial-cur-tick" r="3" cx="0" cy="-102"/><circle class="dial-h" r="15" cx="0" cy="-86"/></svg><div class="dial-c"><div class="dial-num"><span>27</span><span class="dial-unit">°C</span></div><div class="dial-now">now --°C</div></div></div></div><div class="card"><div class="card-t"><span>Humidity target</span><span class="hint">30–95%</span></div><div class="dial-wrap" id="dh" tabindex="0" role="slider" aria-label="Humidity target" aria-valuemin="30" aria-valuemax="95" aria-valuenow="85"><svg class="dial-svg" viewBox="-120 -120 240 240" aria-hidden="true"><path class="dial-arc dial-bg" d="M -60.81 60.81 A 86 86 0 1 1 60.81 60.81"/><path class="dial-arc dial-val-arc"/><circle class="dial-cur-tick" r="3" cx="0" cy="-102"/><circle class="dial-h" r="15" cx="0" cy="-86"/></svg><div class="dial-c"><div class="dial-num"><span>85</span><span class="dial-unit">%</span></div><div class="dial-now">now --%</div></div></div></div></div></div><div class="card"><div class="card-t"><span>Devices</span><span class="hint">Local safety interlocks apply</span></div><div id="devs"></div></div><details id="advanced"><summary><span>Advanced</span><span class="saved" id="sv">SAVED</span></summary><div class="adv"><div id="advancedFields"></div><div class="adv-r"><label for="humidityReducer">Humidity demand reducer</label><select id="humidityReducer"><option value="driest">Driest</option><option value="wettest">Wettest</option></select></div><p class="note">Changes save automatically. The wettest sensor always limits water addition. Offsets apply before validation and control.</p></div></details><p id="reason" class="note"></p></section><section id="history" class="tab"><div class="card"><div class="row"><h2>Recent behavior</h2><select id="historyRange" style="width:auto"><option value="1h">Last hour (RAM)</option><option value="24h">Last 24 operating hours</option></select><button id="loadHistory">Refresh</button><a class="btn" id="csv" href="/api/history?range=1h&format=csv">Download CSV</a></div><canvas id="graph" width="900" height="230"></canvas><small>Top (green), bottom (orange) temperature. One-minute averages; gaps show missing readings. Time falls back to uptime before NTP sync. Persistent history is saved in five-minute batches.</small><div class="scroll"><table><thead><tr><th>Time</th><th>Top °C</th><th>Top RH</th><th>Bottom °C</th><th>Bottom RH</th><th>State</th></tr></thead><tbody id="historyRows"></tbody></table></div></div></section>
<section id="eventsSection" class="tab"><div class="card"><h2>Events</h2><button id="loadEvents">Refresh</button><div id="events"></div></div></section><section id="wifi" class="tab"><div class="card"><h2>Wi-Fi setup</h2><p>Join the SHUNYA-UJ network and use the password on the screen. If this page does not open automatically, visit <b>http://192.168.4.1</b>. Keep the phone connected even if it says “No internet”.</p><p>Your local control code is requested when you save.</p><form id="wifiForm"><label>Network name (2.4 GHz)</label><input id="ssid" maxlength="32" required list="networks"><datalist id="networks"></datalist><label>Password</label><input id="wifiPassword" type="password" maxlength="63" autocomplete="new-password"><p class="buttons"><button type="button" id="scan">Find networks</button><button class="primary">Save Wi-Fi & restart safely</button></p></form><p id="wifiDetails"></p><p class="muted">After connecting, use the IP address displayed on the chamber. The setup AP closes after 3 minutes online. If Wi-Fi stays unavailable, setup returns automatically.</p></div></section>
<section id="system" class="tab"><div class="card"><h2>Device</h2><form id="nameForm"><label>Chamber name</label><input id="friendlyName" maxlength="48" required><p><button>Save name</button></p></form><pre id="systemInfo"></pre><button id="changeCode">Local control code</button><details><summary>Diagnostics</summary><pre id="diagnostics"></pre></details></div><div class="card"><h2>Firmware updates</h2><form id="otaForm"><label>HTTPS manifest URL</label><input id="otaUrl" type="url" placeholder="https://raw.githubusercontent.com/…/manifest.json"><label>Check interval (minutes)</label><input id="otaMin" type="number" min="1" max="1440" value="10"><p><button>Save update settings</button></p></form><p id="otaStatus"></p><div class="buttons"><button id="checkUpdate">Check now</button><button class="primary" id="installUpdate">Install update…</button></div><p class="muted">Checks are automatic. Installation needs your approval and temporarily stops all outputs. Keep power connected until the chamber returns.</p></div><div class="card"><h2>Optional remote service</h2><form id="remoteForm"><label>HTTPS base URL</label><input id="remoteUrl" type="url"><label>Bearer token (leave blank to preserve)</label><input id="remoteToken" type="password" autocomplete="new-password"><label>Telemetry interval (seconds)</label><input id="remoteSec" type="number" min="30" max="3600" value="60"><label><input id="remoteEnabled" type="checkbox"> Enable outbound remote service</label><p><button>Save remote settings</button></p></form><p class="muted">Optional companion service only. Do not expose this chamber’s HTTP port to the public internet.</p></div><div class="card"><h2>Reset & recovery</h2><div class="buttons"><button id="restart">Restart device</button><button id="defaults">Restore climate defaults</button><button id="forget">Forget Wi-Fi & start setup</button><button class="danger" id="factory">Factory reset…</button></div><p>Restart preserves settings. Climate defaults preserve Wi-Fi and system settings. Forget Wi-Fi preserves climate settings. Factory reset removes user settings and generates a new local control code; hardware identity, history and safety records remain.</p></div></section><section id="help" class="tab"><div class="card"><h2>Offline help</h2><details open><summary>Everyday operation</summary><p>Use Controls to change temperature (20–35°C) and humidity (30–95%). AUTO resumes after valid sensors and startup cooldown. Heating stops at the average target or top early ceiling, then water remains blocked for at least 120 seconds. The heater may remain physically hot after its command turns off.</p></details><details><summary>Manual mist and ventilation</summary><p>MIST queues one pulse using the duration in Advanced. A delayed event remains pending until safe. Repeated taps do not extend an active pulse. Mist OFF cancels pending mist. Exhaust ON requests one bounded ventilation cycle. Setting AUTO cancels an outstanding manual request.</p></details><details><summary>Faults and advisories</summary><p>Red faults disable outputs. A sensor must deliver three valid readings to qualify; failures are retried automatically. Any invalid sample stops heating immediately. One or two glitches keep non-heater control using recent good readings. After three failures, SENSOR DEGRADED allows water and exhaust on the remaining usable sensor; both unusable stops all outputs. HIGH TEMPERATURE trips at 38°C and recovers only when both sensors stay at or below 35°C for two minutes. Purge ineffective is an advisory: ambient humidity may be high; check room humidity and exhaust airflow.</p></details><details><summary>Connectivity and reset</summary><p>Control continues without Wi-Fi. Setup AP returns after three minutes offline. Use the screen’s IP address; .local names do not work on every router. Save Wi-Fi to switch networks. The four reset options above have separate effects. A restart waits for residual heater cooldown.</p></details><details><summary>Updates and logs</summary><p>Set a direct HTTPS manifest URL, use Check now, then approve Install update. A failed download resumes local control. Valid NTP time is needed for TLS, but never for climate control. History stores one-minute aggregates; up to five recent minutes can be lost on power failure. Output bits indicate whether a command was ON at any time in that minute.</p></details></div></section></main><footer>Shunya Ujjain · Local control</footer><div id="toast" role="status" hidden></div><script>
function dial(id, min, max, unit, onCommit, step=1){
  const w=document.getElementById(id), svg=w.querySelector('svg'), va=w.querySelector('.dial-val-arc'),
        h=w.querySelector('.dial-h'), vt=w.querySelector('.dial-num span'),
        cn=w.querySelector('.dial-now'), ct=w.querySelector('.dial-cur-tick');
  const R=86, RT=102;
  const polar=(a,r)=>{const rad=(a-90)*Math.PI/180;return[Math.cos(rad)*r,Math.sin(rad)*r]};
  const v2a=v=>-135+(v-min)/(max-min)*270;
  let val=(min+max)/2, drag=false, hold=false;
  function render(){
    const a=v2a(val), [hx,hy]=polar(a,R);
    h.setAttribute('cx',hx); h.setAttribute('cy',hy);
    if(a>-134.5){
      const [sx,sy]=polar(-135,R), [ex,ey]=polar(a,R);
      const lg=(a+135)>180?1:0;
      va.setAttribute('d',`M ${sx} ${sy} A ${R} ${R} 0 ${lg} 1 ${ex} ${ey}`);
    } else va.setAttribute('d','');
    vt.textContent=Number(val.toFixed(1)); w.setAttribute("aria-valuenow",val);
  }
  function p2v(e){
    const r=svg.getBoundingClientRect(), cx=r.left+r.width/2, cy=r.top+r.height/2;
    let d=Math.atan2(e.clientY-cy, e.clientX-cx)*180/Math.PI+90;
    if(d>180)d-=360; if(d<-135)d=-135; if(d>135)d=135;
    return Math.round((min+(d+135)/270*(max-min))/step)*step;
  }
  svg.addEventListener('pointerdown',e=>{e.preventDefault();drag=true;hold=true;svg.setPointerCapture(e.pointerId);const v=p2v(e);if(v!==val){val=v;render();if(navigator.vibrate)navigator.vibrate(3)}});
  svg.addEventListener('pointermove',e=>{if(!drag)return;const v=p2v(e);if(v!==val){val=v;render();if(navigator.vibrate)navigator.vibrate(3)}});
  const end=e=>{if(!drag)return;drag=false;try{svg.releasePointerCapture(e.pointerId)}catch(_){}Promise.resolve(onCommit(val)).finally(()=>{hold=false})};
  w.addEventListener('keydown',e=>{if(!['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();val=e.key==='Home'?min:e.key==='End'?max:Math.max(min,Math.min(max,val+(['ArrowUp','ArrowRight'].includes(e.key)?step:-step)));hold=true;render();Promise.resolve(onCommit(val)).finally(()=>{hold=false})});
  svg.addEventListener('pointerup',end); svg.addEventListener('pointercancel',end);
  return {
    set(v){if(hold)return;val=v;render()},
    cur(c){if(c===null||c===undefined||!Number.isFinite(c)){ct.setAttribute('visibility','hidden');cn.textContent='now --'+unit;return}ct.setAttribute('visibility','visible');const cl=Math.max(min,Math.min(max,c));const[x,y]=polar(v2a(cl),RT);ct.setAttribute('cx',x);ct.setAttribute('cy',y);cn.textContent='now '+Math.round(c)+unit}
  };
}

const fields=[['top_t_offset','Top temperature offset (°C)',-3,3,.1],['top_rh_offset','Top humidity offset (% RH)',-10,10,.1],['bottom_t_offset','Bottom temperature offset (°C)',-3,3,.1],['bottom_rh_offset','Bottom humidity offset (% RH)',-10,10,.1],['temperature_band','Temperature band (°C)',.5,8,.5],['top_margin','Top early-off margin (°C)',.5,2,.5],['humidity_band','Humidity band (% RH)',1,20,1],['post_heat_sec','Post-heater water lockout (s)',120,900,1],['post_mist_sec','Post-mist heater lockout (s)',60,900,1],['hum_max_sec','Humidifier max continuous run (s)',30,600,1],['mist_period_min','Mist interval (min; 0 disables)',0,720,1],['mist_duration_sec','Mist pulse (s)',1,120,1],['fae_period_min','FAE interval (min; 0 disables)',0,720,1],['fae_duration_sec','FAE/manual exhaust duration (s)',10,300,1],['high_rh','High RH purge threshold (%)',93,98,1],['preheat_rh','Preheat purge threshold (%)',80,95,1],['purge_max_sec','Maximum purge (s)',10,120,1],['purge_cooldown_sec','Purge cooldown (s)',60,1800,1]];
'use strict';
const $=id=>document.getElementById(id);
let state=null,loaded=false,systemLoaded=false,key='';try{key=localStorage.getItem('shunya-key')||''}catch(_){}
let toastTimer,savedTimer,authResolve=null,authWait=null;const optimistic=new Map(),dirty=new Map();
function toast(s){$('toast').textContent=s||'Saved';$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,5000)}
function flashSaved(){$('sv').classList.add('show');clearTimeout(savedTimer);savedTimer=setTimeout(()=>$('sv').classList.remove('show'),1200)}
function askCode(message='Enter the local control code to make changes.'){
 $('authPrompt').hidden=false;$('authMessage').textContent=message;
 if(!authWait)authWait=new Promise(resolve=>authResolve=resolve);
 $('key').focus();return authWait;
}
function finishAuth(ok){$('authPrompt').hidden=ok;const resolve=authResolve;authWait=authResolve=null;if(resolve)resolve(ok)}
async function api(path,options={}){
 const headers={'Content-Type':'application/json',...options.headers};if(key)headers['X-Shunya-Key']=key;
 const r=await fetch(path,{...options,headers,signal:AbortSignal.timeout(15000)});
 if(r.status===401){const e=new Error('Code not accepted. Enter the local control code from this chamber.');e.status=401;throw e;}
 const d=await r.json();if(!r.ok)throw Error(d.message||r.statusText);return d;
}
async function post(path,data={}){
 for(let attempt=0;attempt<2;attempt++){
  try{const d=await api(path,{method:'POST',body:JSON.stringify(data)});await poll();return d}
  catch(e){if(e.status===401){key='';try{localStorage.removeItem('shunya-key')}catch(_){}if(await askCode(e.message))continue;return null}toast(e.message);return null}
 }
 return null;
}
$('authForm').onsubmit=async e=>{e.preventDefault();key=$('key').value.trim().toUpperCase();if(!/^[0-9A-F]{8}$/.test(key)){$('authMessage').textContent='Enter the eight-character code shown on the chamber.';return}
 try{const d=await api('/api/status');if(!d.system){key='';$('authMessage').textContent='Code not accepted. Check this chamber’s setup screen or Serial.';return}try{localStorage.setItem('shunya-key',key)}catch(_){}systemLoaded=false;render(d);finishAuth(true);toast('Controls unlocked')}
 catch(e){$('authMessage').textContent=e.message}
};
$('authCancel').onclick=()=>{finishAuth(false);$('authPrompt').hidden=true};$('changeCode').onclick=()=>askCode();
function tab(id){if(!['dashboard','history','eventsSection','wifi','system','help'].includes(id))id='dashboard';document.querySelectorAll('.tab').forEach(e=>e.classList.toggle('visible',e.id===id));document.querySelectorAll('#nav button').forEach(e=>e.classList.toggle('active',e.dataset.tab===id));if(id==='history')loadHistory();if(id==='eventsSection')loadEvents()}
$('nav').onclick=e=>{if(e.target.dataset.tab){location.hash=e.target.dataset.tab;tab(e.target.dataset.tab)}};window.onhashchange=()=>tab(location.hash.slice(1));
const fmt=(v,s='')=>v===null||v===undefined?'—':Number(v).toFixed(1)+s;
function fmtTime(sec){sec=Math.max(0,Math.ceil(sec||0));const h=Math.floor(sec/3600),m=Math.floor(sec/60)%60,s=sec%60;return h?`${h}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`:`${m}:${String(s).padStart(2,'0')}`}
function setIfBlur(id,v){const el=$(id);if(document.activeElement!==el&&!dirty.has(id))el.value=v??''}
function blockedText(dev,d){const why=d.blocked_by?.[dev];if(!why)return '';let sec=0;
 if(why==='post-heat lockout'||why==='startup lockout')sec=dev==='heater'?d.heat_lock_remaining_sec:d.water_lock_remaining_sec;
 if(why==='post-mist settle')sec=d.heat_lock_remaining_sec;
 if(why==='purge cooldown')sec=d.purge_cooldown_remaining_sec;
 if(why==='humidifier pause')sec=d.hum_pause_remaining_sec;
 if(why==='preheat settle')sec=d.preheat_settle_remaining_sec;
 return why+(sec?' '+fmtTime(sec):'');
}
function deviceWhy(dev,d){
 const cfg=d.config,on=d.outputs[dev],manual=dev==='mist'?d.manual_mist:dev==='humidifier'?d.manual_humidifier:dev==='exhaust'?d.manual_exhaust:false;
 const prefix=manual?'manual':'auto';if(cfg[dev]==='OFF')return 'OFF · disabled by mode';
 if(on){const remaining=dev==='heater'?d.heat_runtime_remaining_sec:dev==='humidifier'?d.hum_runtime_remaining_sec:dev==='mist'?d.mist_remaining_sec:d.purge_remaining_sec||d.fae_remaining_sec;
 const action=dev==='heater'?'heating':dev==='humidifier'?'humidifying':dev==='mist'?'misting':d.state==='FAE'?'ventilating':'purging';return `${prefix} · ${action}${remaining?', '+fmtTime(remaining)+' left':''}`}
 const why=blockedText(dev,d);
 if(dev==='mist'&&d.mist_pending)return `${prefix} · mist pending${why?' ('+why+')':''}`;
 if(why)return `${prefix} · blocked: ${why}`;
 if(dev==='mist')return d.mist_next_sec<0?'auto · schedule disabled':`next mist in ${fmtTime(d.mist_next_sec)}`;
 if(dev==='heater')return 'auto · waiting for temperature demand';
 if(dev==='humidifier')return manual?'manual · waiting for safe conditions':'auto · humidity within band';
 return d.fae_pending||d.manual_exhaust?`${prefix} · ventilation pending`:'auto · waiting for ventilation demand';
}
const deviceSpecs=[['heater','Heater',['AUTO','OFF']],['humidifier','Humidifier',['AUTO','ON','OFF']],['mist','Misting',['AUTO','MIST','OFF']],['exhaust','Exhaust',['AUTO','ON','OFF']]];
function selectPill(dev,action){const p=$('pill-'+dev);p.dataset.m=action.toLowerCase();p.querySelectorAll('button').forEach(b=>{const active=b.dataset.action===action;b.classList.toggle('act',active);b.setAttribute('aria-pressed',String(active))})}
for(const [dev,name,actions] of deviceSpecs){
 const row=document.createElement('div');row.className='dev';
 row.innerHTML=`<div class="dev-l"><div class="dev-d" id="dot-${dev}"></div><div class="dev-i"><div class="dev-n">${name}</div><div class="dev-s" id="why-${dev}">Waiting for status</div></div></div><div class="pill${actions.length===2?' two':''}" id="pill-${dev}" data-m="auto" role="group" aria-label="${name} mode">${actions.map(a=>`<button data-action="${a}" aria-pressed="false">${a}</button>`).join('')}<div class="ind"></div></div>`;
 $('devs').append(row);
 $('pill-'+dev).querySelectorAll('button').forEach(b=>b.onclick=async()=>{
  const mark={action:b.dataset.action,until:Infinity};optimistic.set(dev,mark);selectPill(dev,mark.action);$('why-'+dev).textContent='Requesting '+mark.action.toLowerCase()+'…';
  if(navigator.vibrate)navigator.vibrate(8);
  const result=await post('/api/device/'+dev+'/'+mark.action,{});
  if(optimistic.get(dev)===mark){if(result)mark.until=Date.now()+300;else optimistic.delete(dev)}
  if(!result&&state)render(state);await poll();
 });
}
function render(d){state=d;$('name').textContent=d.name;$('state').textContent=d.state.replaceAll('_',' ');$('state').classList.toggle('active',d.state.includes('PURGE'));$('ip').textContent=d.wifi.connected?d.wifi.ip:d.wifi.ap_active?'AP '+d.wifi.ap_name:'Wi-Fi offline';$('linkDot').classList.toggle('off',!d.wifi.connected);
 $('connection').textContent='Connected to chamber';$('reason').textContent=d.reason;$('footer').textContent=d.version;
 const banner=d.banner_message||{text:d.faults?.[0]||d.advisories?.[0]||d.notice||'',kind:d.faults?.length?'fault':d.advisories?.length?'advisory':'notice'};
 $('alerts').textContent=banner.text;$('alerts').className='banner '+banner.kind;$('alerts').hidden=!banner.text;
 for(const sensor of ['top','bottom']){$(sensor+'T').textContent=d[sensor].valid?fmt(d[sensor].temperature):'—';$(sensor+'RH').textContent=d[sensor].valid?Math.round(d[sensor].rh):'—';$('sen-'+sensor).classList.toggle('bad',!d[sensor].valid)}
 dT.set(d.config.temperature_target);dT.cur(d.average);dH.set(d.config.humidity_target);dH.cur(d.config.humidity_reducer==='wettest'?d.rh_high:d.rh_low);
 for(const [dev] of deviceSpecs){$('dot-'+dev).classList.toggle('on',d.outputs[dev]);$('chip-'+dev).classList.toggle('on',d.outputs[dev]);let action=d.config[dev];if(action!=='OFF'){if(dev==='humidifier'&&d.manual_humidifier||dev==='exhaust'&&d.manual_exhaust)action='ON';if(dev==='mist'&&d.manual_mist)action='MIST'}
  const pending=optimistic.get(dev);if(pending&&Date.now()<pending.until){selectPill(dev,pending.action);continue}optimistic.delete(dev);selectPill(dev,action);$('why-'+dev).textContent=deviceWhy(dev,d);
 }
 for(const [id] of fields)setIfBlur('cfg-'+id,d.config[id]);setIfBlur('humidityReducer',d.config.humidity_reducer);
 $('otaStatus').textContent=d.ota.result;$('installUpdate').disabled=!d.ota.available||d.ota.approved||d.ota.busy;
 $('wifiDetails').textContent=(d.wifi.ip?'Address: http://'+d.wifi.ip+' · ':'')+d.wifi.hostname+'.local · '+(d.wifi.ap_active?'Setup AP active':'Setup AP inactive');
 $('diagnostics').textContent=JSON.stringify({state:d.state,blocked_by:d.blocked_by,heat_lock_remaining_sec:d.heat_lock_remaining_sec,water_lock_remaining_sec:d.water_lock_remaining_sec,temperature_spread:d.temperature_spread,rh_spread:d.rh_spread,free_heap:d.free_heap,reset_reason:d.reset_reason},null,2);
 $('systemInfo').textContent=`ID ${d.device_id}\nFirmware ${d.version}\nESP32 core ${d.core_version}\nRunning ${d.ota.running_partition} → OTA ${d.ota.target_partition}\nOTA slot ${d.ota.slot_bytes} bytes\nHistory ${d.history_persistent?'persistent':'RAM only'}\nUptime ${Math.floor(d.uptime_ms/60000)} minutes`;
 if(!loaded){loaded=true;setIfBlur('friendlyName',d.name);setIfBlur('ssid',d.wifi.ssid)}
 if(d.system&&!systemLoaded){systemLoaded=true;setIfBlur('otaUrl',d.system.ota_url);setIfBlur('otaMin',d.system.ota_min);setIfBlur('remoteUrl',d.system.remote_url);setIfBlur('remoteSec',d.system.remote_sec);$('remoteEnabled').checked=d.system.remote_enabled}
}
let pollBusy=false;
async function poll(){if(pollBusy)return;pollBusy=true;try{render(await api('/api/status'))}catch(e){$('connection').textContent='Connection lost · control continues locally';$('linkDot').classList.add('off')}finally{pollBusy=false}}
function debounce(fn,ms){let t;return(...args)=>{clearTimeout(t);t=setTimeout(()=>fn(...args),ms)}}
let cfgPending={},cfgSaving=false;
const saveCfg=debounce(async()=>{
 if(cfgSaving){saveCfg();return}const batch=cfgPending;cfgPending={};if(!Object.keys(batch).length)return;cfgSaving=true;
 const result=await post('/api/config',batch);
 for(const [field,value] of Object.entries(batch)){const id=field==='humidity_reducer'?'humidityReducer':'cfg-'+field;if(dirty.get(id)===value){dirty.delete(id);if(!result&&state)setIfBlur(id,state.config[field])}}
 cfgSaving=false;if(result)flashSaved();if(Object.keys(cfgPending).length)saveCfg();
},500);
for(const [id,label,min,max,step] of fields){const row=document.createElement('div');row.className='adv-r';const l=document.createElement('label'),i=document.createElement('input');l.textContent=label;l.htmlFor='cfg-'+id;i.id='cfg-'+id;i.type='number';i.min=min;i.max=max;i.step=step;i.required=true;
 i.addEventListener('input',()=>{if(!i.checkValidity()||i.value==='')return;const value=Number(i.value);dirty.set(i.id,value);cfgPending[id]=value;saveCfg()});row.append(l,i);$('advancedFields').append(row)
}
$('humidityReducer').onchange=()=>{const value=$('humidityReducer').value;dirty.set('humidityReducer',value);cfgPending.humidity_reducer=value;saveCfg()};
const dT=dial('dt',20,35,'°C',async v=>{const r=await post('/api/target',{temperature_target:v});if(!r)toast('Temperature change was not saved')},.5);
const dH=dial('dh',30,95,'%',async v=>{const r=await post('/api/target',{humidity_target:v});if(!r)toast('Humidity change was not saved')});
async function loadEvents(){try{const d=await api('/api/events');$('events').replaceChildren();for(const e of d.events.slice().reverse()){const p=document.createElement('p');p.textContent=(e.epoch?new Date(e.epoch*1000).toLocaleTimeString():Math.floor(e.uptime_ms/1000)+'s')+' · '+e.type+' · '+e.message;$('events').append(p)}}catch(e){toast(e.message)}}
$('wifiForm').onsubmit=e=>{e.preventDefault();post('/api/wifi',{ssid:$('ssid').value,password:$('wifiPassword').value})};
$('scan').onclick=async()=>{try{let d;try{d=await api('/api/wifi/scan')}catch(e){if(e.status!==401||!await askCode(e.message))throw e;d=await api('/api/wifi/scan')}$('networks').replaceChildren();for(const n of d.networks){let o=document.createElement('option');o.value=n.ssid;$('networks').append(o)}toast(d.scanning?'Scanning; tap Find networks again in a few seconds':'Networks ready; select or type the network name')}catch(e){toast(e.message)}};
$('nameForm').onsubmit=e=>{e.preventDefault();post('/api/name',{name:$('friendlyName').value})};
$('otaForm').onsubmit=e=>{e.preventDefault();post('/api/system',{ota_url:$('otaUrl').value,ota_min:Number($('otaMin').value)})};
$('remoteForm').onsubmit=e=>{e.preventDefault();let d={remote_url:$('remoteUrl').value,remote_sec:Number($('remoteSec').value),remote_enabled:$('remoteEnabled').checked};if($('remoteToken').value)d.token=$('remoteToken').value;post('/api/system',d)};
$('checkUpdate').onclick=()=>post('/api/check-update');$('installUpdate').onclick=()=>{if(confirm('Install the available update? All climate outputs will stop until installation finishes or fails.'))post('/api/install-update',{confirm:'INSTALL'})};
$('restart').onclick=()=>{if(confirm('Restart the device after safe cooldown?'))post('/api/restart')};$('defaults').onclick=async()=>{if(confirm('Restore climate defaults? Wi-Fi and system settings stay.')){loaded=false;await post('/api/reset-climate')}};$('forget').onclick=()=>{if(confirm('Forget building Wi-Fi and restart into setup?'))post('/api/forget-wifi')};$('factory').onclick=()=>{const value=prompt('Erase all user configuration? Type RESET to confirm.');if(value==='RESET')post('/api/factory-reset',{confirm:value})};
function rowTime(s){return s.epoch?new Date(s.epoch*1000).toLocaleString():'Boot '+s.boot_id+' +'+Math.floor(s.uptime_ms/60000)+'m'}
async function loadHistory(){try{const range=$('historyRange').value;$('csv').href='/api/history?range='+range+'&format=csv';const d=await api('/api/history?range='+range);$('historyRows').replaceChildren();for(const s of d.samples.slice(-120).reverse()){const tr=document.createElement('tr');for(const v of [rowTime(s),fmt(s.top),fmt(s.top_rh),fmt(s.bottom),fmt(s.bottom_rh),s.state]){let td=document.createElement('td');td.textContent=v;tr.append(td)}$('historyRows').append(tr)}drawGraph(d.samples);}catch(e){toast(e.message)}}
function drawGraph(samples){const canvas=$('graph'),ctx=canvas.getContext('2d'),w=canvas.width,h=canvas.height;ctx.clearRect(0,0,w,h);let values=samples.flatMap(s=>[s.top,s.bottom]).filter(v=>v!==null);if(!values.length){ctx.fillText('History appears after the first minute.',25,80);return}let lo=Math.floor(Math.min(...values))-1,hi=Math.ceil(Math.max(...values))+1;ctx.font='13px system-ui';for(let i=0;i<5;i++){let v=lo+(hi-lo)*i/4,y=h-25-i*(h-50)/4;ctx.strokeStyle='#262626';ctx.beginPath();ctx.moveTo(40,y);ctx.lineTo(w,y);ctx.stroke();ctx.fillStyle='#888';ctx.fillText(v.toFixed(1)+'°',0,y)}for(const [field,color] of [['top','#10b981'],['bottom','#f9a600']]){ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();let active=false,boot=null;for(let i=0;i<samples.length;i++){let s=samples[i],v=s[field];if(v===null){active=false;continue}let x=45+i*(w-55)/Math.max(1,samples.length-1),y=h-25-(v-lo)/(hi-lo)*(h-50);if(!active||boot!==s.boot_id)ctx.moveTo(x,y);else ctx.lineTo(x,y);active=true;boot=s.boot_id}ctx.stroke()}}

$('loadHistory').onclick=loadHistory;$('historyRange').onchange=loadHistory;$('loadEvents').onclick=loadEvents;tab(location.hash.slice(1)||'dashboard');poll();setInterval(poll,2000);
</script></body></html>)SHUNYA";
