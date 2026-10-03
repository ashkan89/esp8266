#pragma once

#include <Arduino.h>

const char dashboard[] PROGMEM = R"HTML(<!doctype html>

<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">

<title>Living Room Controller</title><style>

body{font:16px system-ui;max-width:850px;margin:30px auto;padding:0 18px;background:#101820;color:#eaf3f5}

button{padding:14px;margin:4px;border:1px solid #577078;border-radius:8px;background:#22353f;color:white;cursor:pointer}

button.on{background:#007b62} select{padding:10px;background:#22353f;color:white;border:1px solid #577078;border-radius:8px} label{display:inline-block} #oledPreview{display:block;width:min(100%,512px);height:auto;image-rendering:pixelated;background:black;border:1px solid #577078;border-radius:4px;margin:12px 0} .grid{display:grid;grid-template-columns:repeat(4,1fr)}

.pwm{display:flex;align-items:center;gap:16px;margin:16px 0} input{flex:1} #error{color:#ffbe99}

</style><h1>Living Room Controller</h1><p id="status">Connecting…</p><p id="error" role="alert"></p>

<div id="relays" class="grid"></div><div id="lights"></div>

<button id="oled">OLED screen</button><button id="led">Onboard LED</button>

<button id="off">All outputs off</button><button id="restart">Restart</button><button id="weatherRefresh">Refresh weather</button>

<p><label for="oledPage">OLED page </label> <select id="oledPage"><option value="AUTO">Auto rotate</option><option value="clock">Clock</option><option value="room">Indoor climate</option><option value="weather">Outdoor weather</option><option value="sky">Clouds / precipitation</option><option value="relays">Relay map</option><option value="lights">Dimmer levels</option><option value="network">Network status</option></select> <span id="oledStatus" aria-live="polite"></span></p>

<p><label for="oledTimeout">OLED auto-off </label> <select id="oledTimeout"><option value="OFF">OFF - stay on</option><option value="1">1 minute</option><option value="2">2 minutes</option><option value="5">5 minutes</option><option value="10">10 minutes</option><option value="15">15 minutes</option><option value="30">30 minutes</option><option value="60">60 minutes</option></select> <label for="oledMinutes">Custom minutes </label><input id="oledMinutes" type="number" min="1" max="1440" step="1" value="5" style="width:75px"><button id="oledTimeoutApply">Set minutes</button><span id="oledCountdown" aria-live="polite"></span></p>
<canvas id="oledPreview" width="128" height="32" role="img" aria-label="Live OLED display preview"></canvas>

<p id="place"></p><p id="time"></p><p id="environment"></p><p id="weather"></p><p id="weatherDetails"></p><p id="diagnostics"></p><p><a href="/api/logs" style="color:#98e4d0">Device logs</a></p><script>

let state={};const $=id=>document.getElementById(id);

$('oledTimeout').onchange=()=>command('oled_timeout/set',$('oledTimeout').value);
$('oledTimeoutApply').onclick=()=>{const input=$('oledMinutes');if(!input.reportValidity())return;command('oled_timeout/set',input.value)};
$('oledPage').onchange=()=>command('oled_page/set',$('oledPage').value);

$('weatherRefresh').onclick=()=>command('weather_refresh/set','PRESS');

function environment(){
  const timeout=state.oled_timeout_minutes?String(state.oled_timeout_minutes):'OFF';
  if(!Array.from($('oledTimeout').options).some(o=>o.value===timeout))$('oledTimeout').add(new Option(timeout+' minutes',timeout));
  if(document.activeElement!==$('oledTimeout'))$('oledTimeout').value=timeout;
  if(document.activeElement!==$('oledMinutes'))$('oledMinutes').value=state.oled_timeout_minutes||5;
  for(const id of ['oledTimeout','oledMinutes','oledTimeoutApply'])$(id).disabled=!state.oled_present;
  const remaining=state.oled_remaining_s||0;
  $('oledCountdown').textContent=!state.oled_present?'':!state.oled_timeout_minutes?' Timeout disabled':state.oled==='ON'?' Off in '+Math.floor(remaining/60)+'m '+remaining%60+'s':' Screen off';

  if(document.activeElement!==$('oledPage'))$('oledPage').value=state.oled_mode||'AUTO';

  $('oledPage').disabled=!state.oled_present;

  $('oledStatus').textContent=!state.oled_present?'Display not detected':state.oled==='ON'?'Showing '+state.oled_page+(state.oled_mode==='AUTO'?' · rotating':' · pinned'):'Display off';

  $('weatherRefresh').disabled=!!state.weather_refresh_pending;

  $('weatherRefresh').textContent=state.weather_refresh_pending?'Refreshing weather…':'Refresh weather';

  $('place').textContent=state.location+' · '+state.region+' · '+Number(state.latitude).toFixed(4)+', '+Number(state.longitude).toFixed(4)+' · location from '+state.location_source;

  $('time').textContent=state.time_valid?state.local_time+' · '+state.timezone:'Waiting for clock synchronization';

  $('environment').textContent=state.sensor_healthy==='ON'?'Home / AHT20: '+Number(state.temperature).toFixed(1)+' °C · '+Number(state.humidity).toFixed(0)+'% humidity':'Home / AHT20 unavailable';

  const sources={'home-assistant':'Home Assistant','open-meteo-ha':'Open-Meteo via Home Assistant','open-meteo':'Open-Meteo direct'};

  $('weather').textContent=state.weather_available==='ON'?'Outdoor / '+(sources[state.weather_source]||state.weather_source)+': '+Number(state.weather_temperature).toFixed(1)+' °C · '+Number(state.weather_humidity).toFixed(0)+'% humidity · '+state.weather_condition.replace(/-/g,' '):'Outdoor weather unavailable · '+(state.weather_error||'Waiting for weather source');

  const metric=(v,unit)=>v==null?'unknown':Number(v).toFixed(1)+unit;

  $('weatherDetails').textContent=state.weather_available==='ON'?'Cloud '+metric(state.weather_cloud_cover,'%')+' · precipitation '+metric(state.weather_precipitation,' mm')+' · '+(state.weather_is_day==='ON'?'Day':'Night')+' · updated '+state.weather_age_s+'s ago':'';

}

async function command(topic,payload){try{const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json','X-Controller-Request':'1'},body:JSON.stringify({topic,payload})});if(!r.ok)throw Error('Command rejected ('+r.status+')');$('error').textContent='';await refresh()}catch(e){$('error').textContent=e.message}}

for(let i=1;i<=16;i++){const b=document.createElement('button');b.id='r'+String(i).padStart(2,'0');b.textContent='Relay '+i;b.onclick=()=>command('relay/'+String(i).padStart(2,'0')+'/set',state[b.id]==='ON'?'OFF':'ON');$('relays').append(b)}

for(let i=1;i<=4;i++){const row=document.createElement('div');row.className='pwm';const label=document.createElement('label');label.textContent='PWM '+i;const s=document.createElement('input');s.type='range';s.min=0;s.max=255;s.id='pwm'+i;s.onchange=()=>command('pwm/'+i+'/set',JSON.stringify({state:+s.value?'ON':'OFF',brightness:+s.value}));row.append(label,s);$('lights').append(row)}

$('oled').onclick=()=>command('oled/set',state.oled==='ON'?'OFF':'ON');$('led').onclick=()=>command('led/set',state.led==='ON'?'OFF':'ON');$('off').onclick=()=>command('all_off/set','PRESS');$('restart').onclick=()=>{if(confirm('Turn all outputs off and restart?'))command('restart/set','PRESS')};

async function preview(){

  if(!state.oled_present)return;

  const r=await fetch('/api/oled',{cache:'no-store'});if(!r.ok)return;

  const bytes=new Uint8Array(await r.arrayBuffer());if(bytes.length!==522)return;

  const ctx=$('oledPreview').getContext('2d'),frame=ctx.createImageData(128,32);

  for(let i=0;i<4096;i++){const v=state.oled==='ON'&&!(bytes[10+(i>>3)]&(128>>(i&7)))?255:0;frame.data[i*4]=frame.data[i*4+1]=frame.data[i*4+2]=v;frame.data[i*4+3]=255}ctx.putImageData(frame,0,0);

}

async function refresh(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error('Status unavailable');state=await r.json();$('status').textContent=state.ip+' · firmware '+state.firmware+' · MQTT '+(state.mqtt_connected?'connected':'offline')+(state.locked?' · OUTPUTS LOCKED':'');for(let i=1;i<=16;i++){const id='r'+String(i).padStart(2,'0');$(id).className=state[id]==='ON'?'on':''}for(let i=1;i<=4;i++){const slider=$('pwm'+i);if(document.activeElement!==slider)slider.value=state.pwm[i-1];slider.disabled=state.locked||(state.serial_diagnostics&&(i===2||i===4))}$('oled').className=state.oled==='ON'?'on':'';$('led').className=state.led==='ON'?'on':'';environment();await preview();$('diagnostics').textContent='Uptime '+state.uptime+'s · RSSI '+state.rssi+' dBm · heap '+state.heap+' B · '+state.reset}catch(e){$('status').textContent='Controller unreachable';$('error').textContent=e.message}}

refresh();setInterval(refresh,3000);

</script></html>)HTML";

