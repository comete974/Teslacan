#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "web.h"
#include "peers.h"
#include "../../shared/protocol.h"

// ─── AP credentials ───────────────────────────────────────────────────────────
#define AP_SSID     "TeslaCAN_Config"
#define AP_PASSWORD "teslacan42"
#define AP_IP       "192.168.4.1"

static WebServer server(80);

// Forward declaration of external telemetry state (defined in main.cpp)
extern struct TeslaState tesla;

// ─── Embedded HTML page ───────────────────────────────────────────────────────
static const char HTML[] PROGMEM = R"====(
<!DOCTYPE html><html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TeslaCAN</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0a0a0a;color:#e0e0e0}
h1{padding:14px 16px;background:#111;border-bottom:2px solid #e31c25;color:#e31c25;font-size:18px}
.tabs{display:flex;background:#111;border-bottom:1px solid #222}
.tab{padding:12px 18px;cursor:pointer;color:#666;font-size:14px;border-bottom:2px solid transparent}
.tab.on{color:#fff;border-bottom-color:#e31c25}
.page{display:none;padding:14px}.page.on{display:block}
.card{background:#111;border:1px solid #222;border-radius:8px;padding:14px;margin-bottom:14px}
.card h2{color:#e31c25;font-size:11px;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
.row{display:flex;align-items:center;justify-content:space-between;padding:9px 0;border-bottom:1px solid #1a1a1a}
.row:last-child{border:none}
select{background:#1a1a1a;color:#ddd;border:1px solid #333;padding:5px 8px;border-radius:4px;width:100%;font-size:13px}
.btn{padding:9px 18px;border:none;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500}
.red{background:#e31c25;color:#fff}.gray{background:#2a2a2a;color:#ddd}.green{background:#1e5c1e;color:#fff}
.dot{width:9px;height:9px;border-radius:50%;display:inline-block;margin-right:6px}
.go{background:#4caf50}.gr{background:#555}.blink{animation:bl 1s step-end infinite}
@keyframes bl{50%{opacity:0}}
table{width:100%;border-collapse:collapse;font-size:13px}
th{text-align:left;color:#666;padding:8px 4px;border-bottom:1px solid #222;font-weight:400}
td{padding:7px 4px;border-bottom:1px solid #1a1a1a;vertical-align:middle}
.spd{font-size:52px;font-weight:700;color:#fff;text-align:center;padding:10px}
.unit{font-size:13px;color:#555;text-align:center;margin-bottom:14px}
.info{color:#666;font-size:13px;padding:10px 0}
</style>
</head>
<body>
<h1>TeslaCAN Config</h1>
<div class="tabs">
  <div class="tab on" onclick="show('dev',this)">Appareils</div>
  <div class="tab"    onclick="show('cfg',this)">Boutons</div>
  <div class="tab"    onclick="show('ble',this)">Bluetooth</div>
  <div class="tab"    onclick="show('sta',this)">Statut</div>
</div>

<!-- APPAREILS -->
<div id="dev" class="page on">
  <div class="card">
    <h2>Appareils appairés</h2>
    <div id="dev-list"><span class="info">Chargement...</span></div>
  </div>
  <div class="card">
    <h2>Ajouter un nouvel ESP32-C3</h2>
    <div id="pair-zone" style="text-align:center;padding:8px">
      <button class="btn red" onclick="startPair()">Activer le mode appairage</button>
      <p class="info" style="margin-top:8px">Mettez l'ESP32-C3 sous tension après avoir cliqué.</p>
    </div>
  </div>
</div>

<!-- CONFIGURATION BOUTONS -->
<div id="cfg" class="page">
  <div class="card">
    <h2>Configuration des boutons</h2>
    <select id="dev-sel" onchange="loadCfg()" style="margin-bottom:12px">
      <option value="">Sélectionner un appareil...</option>
    </select>
    <div id="btn-tbl"></div>
    <div style="margin-top:12px;display:flex;gap:8px">
      <button class="btn green" onclick="saveCfg()">Enregistrer</button>
      <button class="btn gray"  onclick="pushCfg()">Envoyer à l'appareil</button>
    </div>
  </div>
</div>

<!-- BLUETOOTH -->
<div id="ble" class="page">
  <div class="card">
    <h2>Appareils Bluetooth LE détectés</h2>
    <div id="ble-list"><span class="info">Appuyez sur Scanner pour chercher.</span></div>
    <div style="margin-top:12px">
      <button class="btn red" onclick="bleScan()">Scanner (5 s)</button>
    </div>
    <p class="info" style="margin-top:8px">Le serveur annonce aussi "TeslaCAN" en BLE pour qu'une app mobile puisse recevoir la télémétrie et envoyer des commandes.</p>
  </div>
</div>

<!-- STATUT -->
<div id="sta" class="page">
  <div class="card">
    <div class="spd" id="spd">--</div>
    <div class="unit">km/h</div>
    <table>
      <tr><th>Rapport</th><td id="gear">--</td></tr>
      <tr><th>Limite</th><td id="lim">--</td></tr>
      <tr><th>Batterie</th><td id="soc">--</td></tr>
      <tr><th>Temp. ext.</th><td id="tmp">--</td></tr>
      <tr><th>Bip muet</th><td id="mut">--</td></tr>
      <tr><th>Sentinelle</th><td id="sen">--</td></tr>
    </table>
  </div>
</div>

<script>
const CMDS={
  "0":"— Aucune —","16":"Bip mute","17":"Sentinelle","18":"Coffre ouvrir",
  "19":"Coffre fermer","20":"Frunk","21":"Verrouiller","22":"Déverrouiller",
  "23":"Clim ON","24":"Clim OFF","25":"Volume +","26":"Volume -",
  "27":"Essuie-glace +","28":"Essuie-glace -","29":"Feux détresse",
  "30":"Dégivrage","31":"Toit ouvrant","32":"Mode Camping","33":"Mode Chien",
  "34":"Lumières Sentry","35":"Klaxon court"
};
function opts(sel){return Object.entries(CMDS).map(([v,l])=>`<option value="${v}"${v==sel?' selected':''}>${l}</option>`).join('')}

function show(id,tab){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('on'));
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('on'));
  document.getElementById(id).classList.add('on');
  tab.classList.add('on');
  if(id==='sta')startPoll();else stopPoll();
}

// ── Appareils ────────────────────────────────────────────────────────────────
async function loadDevs(){
  const r=await fetch('/api/devices');const ds=await r.json();
  const el=document.getElementById('dev-list');
  const sel=document.getElementById('dev-sel');
  if(!ds.length){el.innerHTML='<span class="info">Aucun appareil appairé.</span>';sel.innerHTML='<option value="">Aucun appareil</option>';return;}
  el.innerHTML=ds.map(d=>`<div class="row">
    <div><span class="dot ${d.online?'go':'gr'}"></span><strong>${d.name}</strong>
    <span style="color:#555;font-size:11px;margin-left:6px">${d.mac}</span>
    <span style="color:#555;font-size:11px;margin-left:6px">${d.btn_count} btn</span></div>
    <button class="btn gray" style="padding:5px 10px;font-size:12px" onclick="delDev('${d.mac}')">Retirer</button>
  </div>`).join('');
  sel.innerHTML='<option value="">Sélectionner...</option>'+ds.map(d=>`<option value="${d.id}">${d.name}</option>`).join('');
}

async function delDev(mac){
  if(!confirm('Retirer cet appareil ?'))return;
  await fetch('/api/devices/'+mac.replace(/:/g,''),{method:'DELETE'});
  loadDevs();
}

async function startPair(){
  await fetch('/api/pair/start',{method:'POST'});
  document.getElementById('pair-zone').innerHTML=`
    <div class="dot go blink" style="width:14px;height:14px;margin:0 auto 10px"></div>
    <p style="color:#4caf50;font-weight:500">Mode appairage actif (30 s)</p>
    <p class="info">Mettez l'ESP32-C3 sous tension...</p>
    <button class="btn gray" style="margin-top:10px" onclick="stopPair()">Annuler</button>`;
  setTimeout(stopPair,30000);
}
async function stopPair(){
  await fetch('/api/pair/stop',{method:'POST'});
  document.getElementById('pair-zone').innerHTML=`
    <button class="btn red" onclick="startPair()">Activer le mode appairage</button>
    <p class="info" style="margin-top:8px">Mettez l'ESP32-C3 sous tension après avoir cliqué.</p>`;
  loadDevs();
}

// ── Config boutons ───────────────────────────────────────────────────────────
let currentDevId='';
async function loadCfg(){
  currentDevId=document.getElementById('dev-sel').value;
  if(!currentDevId)return;
  const r=await fetch('/api/devices/'+currentDevId+'/config');
  const c=await r.json();
  document.getElementById('btn-tbl').innerHTML=`<table>
    <tr><th>#</th><th>Appui court</th><th>Appui long</th></tr>
    ${c.buttons.map((b,i)=>`<tr>
      <td>BTN ${i}</td>
      <td><select data-i="${i}" data-t="s">${opts(b.short)}</select></td>
      <td><select data-i="${i}" data-t="l">${opts(b.long)}</select></td>
    </tr>`).join('')}
  </table>`;
}

async function saveCfg(){
  if(!currentDevId)return;
  const sels=[...document.querySelectorAll('#btn-tbl select')];
  const n=sels.length/2;const buttons=[];
  for(let i=0;i<n;i++) buttons.push({short:+sels[i*2].value,long:+sels[i*2+1].value});
  await fetch('/api/devices/'+currentDevId+'/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({buttons})});
  alert('Enregistré !');
}

async function pushCfg(){
  if(!currentDevId)return;
  await fetch('/api/devices/'+currentDevId+'/push',{method:'POST'});
  alert('Config envoyée à l\'appareil via ESP-NOW.');
}

// ── BLE ──────────────────────────────────────────────────────────────────────
async function bleScan(){
  document.getElementById('ble-list').innerHTML='<span class="info">Scan en cours (5 s)...</span>';
  await fetch('/api/ble/scan',{method:'POST'});
  setTimeout(async()=>{
    const r=await fetch('/api/ble');const ds=await r.json();
    document.getElementById('ble-list').innerHTML=ds.length
      ? ds.map(d=>`<div class="row"><span>${d.name||'(sans nom)'}</span><span style="color:#555;font-size:11px">${d.addr}</span></div>`).join('')
      : '<span class="info">Aucun appareil trouvé.</span>';
  },5500);
}

// ── Statut ───────────────────────────────────────────────────────────────────
const GEARS=['P','R','N','D'];
let pollT;
function startPoll(){clearInterval(pollT);pollT=setInterval(async()=>{
  const r=await fetch('/api/status');const s=await r.json();
  document.getElementById('spd').textContent=Math.round(s.speed)||'--';
  document.getElementById('gear').textContent=GEARS[s.gear]||'?';
  document.getElementById('lim').textContent=s.limit>0?s.limit+' km/h':'--';
  document.getElementById('soc').textContent=s.soc+'%';
  document.getElementById('tmp').textContent=s.temp+'°C';
  document.getElementById('mut').textContent=s.muted?'Oui':'Non';
  document.getElementById('sen').textContent=s.sentinel?'Oui':'Non';
},600);}
function stopPoll(){clearInterval(pollT);}

loadDevs();
</script>
</body></html>
)====";

// ─── API handlers ──────────────────────────────────────────────────────────────
static void cors() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache");
}

static void handleRoot() {
    cors();
    server.send_P(200, "text/html", HTML);
}

static void handleGetDevices() {
    cors();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_DEVICES; i++) {
        PeerDevice *d = peers_get(i);
        if (!d) continue;
        JsonObject o = arr.add<JsonObject>();
        o["id"]        = i;
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
        o["mac"]       = mac;
        o["name"]      = d->name;
        o["btn_count"] = d->btn_count;
        o["online"]    = d->online;
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleDeleteDevice() {
    cors();
    String macStr = server.pathArg(0);  // hex without colons
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = strtoul(macStr.substring(i*2, i*2+2).c_str(), nullptr, 16);
    }
    peers_remove_by_mac(mac);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePairStart() {
    cors();
    pairing_mode = true;
    Serial.println("[Web] Pairing mode ON");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePairStop() {
    cors();
    pairing_mode = false;
    Serial.println("[Web] Pairing mode OFF");
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleGetConfig() {
    cors();
    int id = server.pathArg(0).toInt();
    PeerDevice *d = peers_get(id);
    if (!d) { server.send(404, "application/json", "{\"error\":\"not found\"}"); return; }

    JsonDocument doc;
    JsonArray buttons = doc["buttons"].to<JsonArray>();
    for (int i = 0; i < d->btn_count; i++) {
        JsonObject b = buttons.add<JsonObject>();
        b["short"] = d->buttons[i].short_cmd;
        b["long"]  = d->buttons[i].long_cmd;
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handlePostConfig() {
    cors();
    int id = server.pathArg(0).toInt();
    PeerDevice *d = peers_get(id);
    if (!d) { server.send(404, "application/json", "{\"error\":\"not found\"}"); return; }

    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    JsonArray buttons = doc["buttons"].as<JsonArray>();
    int i = 0;
    for (JsonObject b : buttons) {
        if (i >= d->btn_count) break;
        d->buttons[i].short_cmd = b["short"].as<uint8_t>();
        d->buttons[i].long_cmd  = b["long"].as<uint8_t>();
        i++;
    }
    peers_save();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePushConfig() {
    cors();
    int id = server.pathArg(0).toInt();
    peers_push_config(id);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleStatus() {
    cors();
    extern struct TeslaState tesla;
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"speed\":%.1f,\"limit\":%.0f,\"gear\":%d,"
             "\"soc\":%d,\"temp\":%d,\"muted\":%d,\"sentinel\":%d}",
             tesla.speed_kmh, tesla.speed_limit_kmh, tesla.gear,
             tesla.soc, tesla.outside_temp, (int)tesla.beep_muted,
             (int)tesla.sentinel_on);
    server.send(200, "application/json", buf);
}

// BLE scan results (filled by ble.cpp)
extern String ble_scan_json;
static void handleBleGet() {
    cors();
    server.send(200, "application/json", ble_scan_json.length() ? ble_scan_json : "[]");
}
extern void ble_start_scan();
static void handleBleScan() {
    cors();
    ble_start_scan();
    server.send(200, "application/json", "{\"ok\":true}");
}

// ─── Public init ──────────────────────────────────────────────────────────────
void web_init() {
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1);  // channel 1 — must match ESP-NOW
    Serial.printf("[Web] AP '%s' at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    server.on("/",           HTTP_GET,    handleRoot);
    server.on("/api/devices", HTTP_GET,   handleGetDevices);
    server.on("/api/devices/{mac}", HTTP_DELETE, handleDeleteDevice);
    server.on("/api/pair/start", HTTP_POST, handlePairStart);
    server.on("/api/pair/stop",  HTTP_POST, handlePairStop);
    server.on("/api/devices/{id}/config", HTTP_GET,  handleGetConfig);
    server.on("/api/devices/{id}/config", HTTP_POST, handlePostConfig);
    server.on("/api/devices/{id}/push",   HTTP_POST, handlePushConfig);
    server.on("/api/status",  HTTP_GET,  handleStatus);
    server.on("/api/ble",     HTTP_GET,  handleBleGet);
    server.on("/api/ble/scan",HTTP_POST, handleBleScan);

    server.begin();
    Serial.println("[Web] HTTP server started");
}

void web_loop() {
    server.handleClient();
}
