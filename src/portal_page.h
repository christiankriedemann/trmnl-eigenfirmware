#pragma once
// Webinterface (eine Seite, laedt Zustand per JSON). Keine externen Ressourcen.
static const char PORTAL_HTML[] = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TRMNL Einrichtung</title>
<style>
body{font-family:-apple-system,system-ui,sans-serif;margin:0;background:#f4f4f2;color:#111}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:8px 0 16px}
section{background:#fff;border:1px solid #ddd;border-radius:10px;padding:14px 16px;margin:0 0 14px}
h2{font-size:17px;margin:0 0 10px}
label{display:block;font-size:14px;margin:10px 0 4px;color:#333}
input,select,textarea{width:100%;box-sizing:border-box;font-size:16px;padding:8px;border:1px solid #bbb;border-radius:6px;background:#fff}
textarea{min-height:90px;font-family:ui-monospace,monospace;font-size:12px}
.row{display:flex;gap:10px}.row>div{flex:1}
button{font-size:16px;padding:9px 14px;border-radius:7px;border:1px solid #111;background:#111;color:#fff;margin-top:10px}
button.sec{background:#fff;color:#111}
.hint{font-size:13px;color:#666;margin-top:4px}
.ok{color:#176b2c}.bad{color:#a11}
ul{padding-left:18px;margin:6px 0}
.cal{display:flex;align-items:center;gap:8px;margin:4px 0}.cal input{width:auto}
#msg{position:sticky;top:0;background:#111;color:#fff;padding:8px 12px;border-radius:7px;display:none}
</style></head><body><main>
<div id="msg"></div>
<h1>TRMNL Einrichtung</h1>

<section><h2>Status</h2><div id="status">l&auml;dt &hellip;</div>
<button class="sec" onclick="otaNow()">Nach Update suchen</button></section>

<section><h2>WLAN</h2>
<div id="wifis"></div>
<label>Netz</label><select id="ssidSel"><option value="">bitte w&auml;hlen &hellip;</option></select>
<label>oder Name von Hand</label><input id="ssid" autocomplete="off">
<label>Passwort</label><input id="pass" type="password" autocomplete="off">
<button onclick="addWifi()">WLAN speichern</button> <button class="sec" onclick="scan()">Netze suchen</button>
</section>

<section><h2>Google Workspace</h2>
<label>Dienstkonto-Schl&uuml;ssel (JSON-Datei)</label><input type="file" id="gfile" accept=".json,application/json">
<div class="hint" id="gkeyState"></div>
<label>Nutzer, dessen Kalender gelesen werden</label><input id="g_user" placeholder="name@firma.de">
<button onclick="saveKeys(['g_user'])">Speichern</button>
<label>Kalender</label><div id="cals"><span class="hint">Nach dem Speichern: </span><button class="sec" onclick="loadCals()">Kalender laden</button></div>
</section>

<section><h2>Microsoft 365</h2>
<label>Anwendungs-ID (Client-ID)</label><input id="ms_client" autocomplete="off">
<label>Verzeichnis-ID (Mandant)</label><input id="ms_tenant" placeholder="organizations">
<label><input type="checkbox" id="ms_planner_cb" style="width:auto"> Planner-Aufgaben anzeigen</label>
<button onclick="saveMs()">Speichern</button>
<div class="hint" id="msState"></div>
<div class="hint">Angemeldet wird am Display: nach dem Speichern zeigt es einen QR-Code und einen Code.</div>
<label>Kalender</label><div id="mscals"><button class="sec" onclick="loadMsCals()">Kalender laden</button></div>
<label>To-Do-Listen</label><div id="mslists"><button class="sec" onclick="loadMsLists()">Listen laden</button></div>
<button class="sec" onclick="msLogout()">Abmelden</button>
</section>

<section><h2>Todoist</h2>
<label>API-Token</label><input id="td_token" type="password" autocomplete="off" placeholder="unver&auml;ndert lassen">
<label>Filter</label><input id="td_filter">
<button onclick="saveKeys(['td_token','td_filter'])">Speichern</button>
</section>

<section><h2>Aufgaben</h2>
<div class="row"><div><label>Pl&auml;tze</label><input id="slots" type="number"></div>
<div><label>&Uuml;berf&auml;llig</label><input id="q_over" type="number"></div></div>
<div class="row"><div><label>F&auml;llig</label><input id="q_due" type="number"></div>
<div><label>Deadline</label><input id="q_dead" type="number"></div>
<div><label>Undatiert</label><input id="q_undat" type="number"></div></div>
<button onclick="saveKeys(['slots','q_over','q_due','q_dead','q_undat'])">Speichern</button>
</section>

<section><h2>Ort und Zeit</h2>
<label>Heimatzeitzone</label><input id="tz_home" placeholder="Europe/Berlin">
<label>Standort</label><select id="loc_mode"><option value="auto">automatisch (WLAN-Ortung)</option><option value="fixed">fest</option></select>
<div class="row"><div><label>Ort (fest)</label><input id="loc_name"></div><div><label>Breite</label><input id="loc_lat"></div><div><label>L&auml;nge</label><input id="loc_lon"></div></div>
<button onclick="saveKeys(['tz_home','loc_mode','loc_name','loc_lat','loc_lon'])">Speichern</button>
</section>

<section><h2>Anzeige</h2>
<div class="row"><div><label>Takt tags (Min.)</label><input id="day_min" type="number"></div>
<div><label>Nachts ab (Uhr)</label><input id="sleep_h" type="number"></div>
<div><label>Morgens ab (Uhr)</label><input id="wake_h" type="number"></div></div>
<label>Unterer Rand (Pixelzeile, Rahmen)</label><input id="safe_bot" type="number">
<button onclick="saveKeys(['day_min','sleep_h','wake_h','safe_bot'])">Speichern</button>
</section>

<section><button onclick="done()">Fertig, Anzeige starten</button>
<div class="hint">Ohne Aktivit&auml;t schlie&szlig;t die Einrichtung nach einigen Minuten von selbst.</div></section>
</main>
<script>
const $=id=>document.getElementById(id);
let st={};
function msg(t){const m=$('msg');m.textContent=t;m.style.display='block';setTimeout(()=>m.style.display='none',3000)}
async function api(p,o){o=o||{};o.headers={'X-TRMNL':'1','Content-Type':'application/json'};const r=await fetch(p,o);const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||r.status);return j}
async function load(){
  st=await api('/api/state');
  for(const k in st.cfg){const e=$(k);if(e&&e.type!=='password')e.value=st.cfg[k]}
  $('status').innerHTML='Firmware '+esc(st.version)+'<br>WLAN: '+esc(st.sta||'nicht verbunden')+'<br>Letzte Aktualisierung: '+esc(st.last||'noch nie')+(st.ota?'<br>Update: '+esc(st.ota):'')+(st.err?'<br><span class="bad">'+esc(st.err)+'</span>':'');
  $('gkeyState').innerHTML=st.secrets.g_key?'<span class="ok">Schl&uuml;ssel hinterlegt ('+esc(st.cfg.g_email)+')</span>':'noch kein Schl&uuml;ssel';
  $('ms_planner_cb').checked=(st.cfg.ms_planner||'1')!=='0';
  $('msState').innerHTML=st.secrets.ms_refresh?'<span class="ok">Angemeldet'+(st.cfg.ms_user?' als '+esc(st.cfg.ms_user):'')+'</span>':'nicht angemeldet';
  $('td_token').placeholder=st.secrets.td_token?'gesetzt, unver&auml;ndert lassen':'noch nicht gesetzt';
  $('wifis').innerHTML='<ul>'+st.wifis.map(s=>'<li>'+esc(s)+' <a href="#" data-ssid="'+esc(s)+'">entfernen</a></li>').join('')+'</ul>';
  document.querySelectorAll('#wifis a').forEach(a=>a.onclick=e=>{e.preventDefault();forget(a.dataset.ssid)});
}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function saveKeys(keys){const b={};for(const k of keys){const v=$(k).value;if($(k).type==='password'&&!v)continue;b[k]=v}
  try{await api('/api/set',{method:'POST',body:JSON.stringify(b)});msg('Gespeichert');load()}catch(e){msg('Fehler: '+e.message)}}
async function scan(){const j=await api('/api/scan');$('ssidSel').innerHTML='<option value="">bitte w&auml;hlen &hellip;</option>'+j.map(s=>'<option>'+esc(s)+'</option>').join('')}
async function addWifi(){const ssid=$('ssid').value||$('ssidSel').value;if(!ssid){msg('Netz wählen');return}
  try{await api('/api/wifi',{method:'POST',body:JSON.stringify({ssid,pass:$('pass').value})});$('pass').value='';msg('WLAN gespeichert');load()}catch(e){msg('Fehler: '+e.message)}}
async function forget(s){await api('/api/wifi/forget',{method:'POST',body:JSON.stringify({ssid:s})});load()}
$('gfile').onchange=async e=>{const f=e.target.files[0];if(!f)return;const t=await f.text();
  try{await api('/api/google-key',{method:'POST',body:t});msg('Schlüssel gespeichert');load()}catch(err){msg('Fehler: '+err.message)}};
async function loadCals(){$('cals').textContent='lädt …';
  try{const j=await api('/api/cals');const sel=(st.cfg.g_cals||'').split('\n');
  $('cals').innerHTML=j.map(c=>'<div class="cal"><input type="checkbox" value="'+esc(c.id)+'"'+(sel.includes(c.id)?' checked':'')+'><span>'+esc(c.name)+'</span></div>').join('')+'<button onclick="saveCals()">Auswahl speichern</button>'}
  catch(e){$('cals').innerHTML='<span class="bad">'+esc(e.message)+'</span>'}}
async function saveCals(){const ids=[...document.querySelectorAll('#cals input:checked')].map(i=>i.value);
  await api('/api/set',{method:'POST',body:JSON.stringify({g_cals:ids.join('\n')})});msg('Kalender gespeichert');load()}
async function saveMs(){try{await api('/api/set',{method:'POST',body:JSON.stringify({ms_client:$('ms_client').value,ms_tenant:$('ms_tenant').value,ms_planner:$('ms_planner_cb').checked?'1':'0'})});msg('Gespeichert');load()}catch(e){msg('Fehler: '+e.message)}}
function pickList(el,key,items){const sel=(st.cfg[key]||'').split('\n');
  $(el).innerHTML=items.map(c=>'<div class="cal"><input type="checkbox" value="'+esc(c.id)+'"'+(sel.includes(c.id)?' checked':'')+'><span>'+esc(c.name)+(c.flagged?' (Mails)':'')+'</span></div>').join('')+'<button>Auswahl speichern</button>';
  $(el).querySelector('button').onclick=async()=>{const ids=[...document.querySelectorAll('#'+el+' input:checked')].map(i=>i.value);const b={};b[key]=ids.join('\n');await api('/api/set',{method:'POST',body:JSON.stringify(b)});msg('Gespeichert');load()}}
async function loadMsCals(){try{pickList('mscals','ms_cals',await api('/api/mscals'))}catch(e){$('mscals').textContent=e.message}}
async function loadMsLists(){try{pickList('mslists','ms_lists',await api('/api/mslists'))}catch(e){$('mslists').textContent=e.message}}
async function msLogout(){await api('/api/mslogout',{method:'POST'});msg('Abgemeldet');load()}
async function otaNow(){await api('/api/ota',{method:'POST'});msg('Wird nach dem Schlie\u00dfen der Einrichtung gepr\u00fcft')}
async function done(){await api('/api/done',{method:'POST'});document.body.innerHTML='<main><h1>Fertig</h1><p>Das Display aktualisiert sich jetzt.</p></main>'}
load();
</script></body></html>)HTML";
