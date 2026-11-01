#include "SongUpload.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>

namespace SongUpload {

static WebServer *s_server = nullptr;
static bool s_running = false;
static bool s_busy = false;
static File s_file;
static char s_name[96] = "";
static uint32_t s_bytes = 0;
static uint32_t s_added = 0;
static const char *s_error = "";

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jigglypuff · Add songs</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font:16px/1.45 -apple-system,BlinkMacSystemFont,"SF Pro Text",system-ui,sans-serif;color:#f4f2ff;
background:radial-gradient(60% 50% at 15% 10%,#2a5bff88,transparent),radial-gradient(55% 50% at 90% 95%,#dc288c88,transparent),
radial-gradient(40% 40% at 80% 5%,#00c8be55,transparent),#0b0d22;display:flex;justify-content:center;padding:28px 16px}
main{width:100%;max-width:520px;display:flex;flex-direction:column;gap:16px}
.glass{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.22);border-radius:22px;padding:20px;
backdrop-filter:blur(18px) saturate(1.4);-webkit-backdrop-filter:blur(18px) saturate(1.4);box-shadow:inset 0 1px 0 rgba(255,255,255,.25)}
h1{margin:0;font-size:26px;letter-spacing:-.02em}
p{margin:6px 0 0;color:#c9c5e6}
#drop{display:flex;flex-direction:column;align-items:center;gap:8px;padding:34px 16px;text-align:center;border-style:dashed;cursor:pointer}
#drop.over{background:rgba(255,255,255,.16)}
#drop b{font-size:18px}
input{display:none}
ul{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:8px}
li{display:flex;justify-content:space-between;gap:12px;font-variant-numeric:tabular-nums}
li span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
li span:last-child{color:#a9a4cc;flex:none}
.bar{height:6px;border-radius:3px;background:rgba(255,255,255,.12);overflow:hidden;margin-top:6px}
.bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,#ff2d55,#ff8ab0)}
.ok{color:#5ee49a}.err{color:#ff7a8a}
h2{margin:0 0 10px;font-size:15px;text-transform:uppercase;letter-spacing:.08em;color:#a9a4cc}
button{background:none;border:0;color:#ff7a8a;font:inherit;cursor:pointer;padding:0}
</style></head><body><main>
<section class="glass"><h1>Add songs</h1><p>Drop music here and it goes straight onto Jigglypuff.</p></section>
<label id="drop" class="glass" for="f"><b>Choose files or drop them here</b><span>MP3, WAV, M4A, AAC or FLAC</span></label>
<input id="f" type="file" multiple accept=".mp3,.wav,.m4a,.aac,.flac,audio/*">
<section class="glass" id="q" hidden><h2>Uploading</h2><ul id="ql"></ul></section>
<section class="glass"><h2>On the console</h2><ul id="songs"><li><span>Loading…</span></li></ul><p id="free"></p></section>
</main><script>
const $=s=>document.querySelector(s);
const mb=n=>(n/1048576).toFixed(1)+' MB';
async function list(){
 const r=await fetch('/songs');const j=await r.json();
 $('#songs').innerHTML=j.songs.length?'':'<li><span>No songs yet</span></li>';
 for(const s of j.songs){const li=document.createElement('li');
  const n=document.createElement('span');n.textContent=s.name;
  const right=document.createElement('span');right.textContent=mb(s.size)+'  ';
  const del=document.createElement('button');del.textContent='Remove';
  del.onclick=async()=>{if(!confirm('Remove '+s.name+'?'))return;
   await fetch('/delete?name='+encodeURIComponent(s.name),{method:'POST'});list();};
  right.appendChild(del);li.append(n,right);$('#songs').appendChild(li);}
 $('#free').textContent=mb(j.free)+' free';
}
function upload(file,row){return new Promise(res=>{
 const x=new XMLHttpRequest();x.open('POST','/upload');
 x.upload.onprogress=e=>{if(e.lengthComputable)row.bar.style.width=(e.loaded/e.total*100)+'%';};
 x.onload=()=>{const ok=x.status==200;row.st.textContent=ok?'Added':x.responseText||'Failed';row.st.className=ok?'ok':'err';res();};
 x.onerror=()=>{row.st.textContent='Connection lost';row.st.className='err';res();};
 const fd=new FormData();fd.append('file',file,file.name);x.send(fd);});}
async function send(files){
 $('#q').hidden=false;const rows=[];
 for(const f of files){const li=document.createElement('li');li.style.flexDirection='column';
  const top=document.createElement('div');top.style.cssText='display:flex;justify-content:space-between;gap:12px';
  const n=document.createElement('span');n.textContent=f.name;const st=document.createElement('span');st.textContent='Waiting';
  top.append(n,st);const b=document.createElement('div');b.className='bar';const bar=document.createElement('i');b.appendChild(bar);
  li.append(top,b);$('#ql').appendChild(li);rows.push({f,st,bar});}
 for(const r of rows){r.st.textContent='Sending';r.st.className='';await upload(r.f,r);}
 list();
}
$('#f').onchange=e=>send([...e.target.files]);
const d=$('#drop');
['dragenter','dragover'].forEach(t=>d.addEventListener(t,e=>{e.preventDefault();d.classList.add('over');}));
['dragleave','drop'].forEach(t=>d.addEventListener(t,e=>{e.preventDefault();d.classList.remove('over');}));
d.addEventListener('drop',e=>send([...e.dataTransfer.files]));
list();
</script></body></html>)HTML";

static bool isPlayable(const String &name) {
  String l = name;
  l.toLowerCase();
  return l.endsWith(".mp3") || l.endsWith(".wav") || l.endsWith(".m4a") ||
         l.endsWith(".aac") || l.endsWith(".flac");
}

// Keeps just the base name and swaps anything a FAT filesystem or our
// path handling might choke on for '_'.
static String cleanName(const String &raw) {
  String n = raw;
  int slash = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (slash >= 0) n = n.substring(slash + 1);
  String out;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    bool ok = isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.' ||
              c == '(' || c == ')' || c == '\'' || c == '&' || c == ',';
    out += ok ? c : '_';
  }
  if (out.length() > 90) out = out.substring(out.length() - 90);
  return out;
}

static void handleRoot() { s_server->send_P(200, "text/html", PAGE); }

static void handleSongs() {
  String json = "{\"songs\":[";
  File root = SD.open("/");
  bool first = true;
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      String n = f.name();
      if (!f.isDirectory() && isPlayable(n) && !n.startsWith("._")) {
        if (!first) json += ',';
        first = false;
        n.replace("\\", "\\\\");
        n.replace("\"", "\\\"");
        json += "{\"name\":\"" + n + "\",\"size\":" + String((uint32_t)f.size()) + "}";
      }
      f.close();
    }
    root.close();
  }
  uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
  json += "],\"free\":" + String((double)freeBytes, 0) + "}";
  s_server->send(200, "application/json", json);
}

static void handleDelete() {
  // Use the name as listed, but refuse anything that could reach outside
  // the card's root - songs copied from a PC may hold characters the
  // upload path would have rewritten, so cleaning here would miss them.
  String n = s_server->arg("name");
  if (n.length() == 0 || !isPlayable(n) || n.indexOf('/') >= 0 || n.indexOf('\\') >= 0 ||
      n.indexOf("..") >= 0) {
    s_server->send(400, "text/plain", "Not a song");
    return;
  }
  bool ok = SD.remove("/" + n);
  s_server->send(ok ? 200 : 404, "text/plain", ok ? "Removed" : "Not found");
}

static void handleUploadData() {
  HTTPUpload &up = s_server->upload();

  if (up.status == UPLOAD_FILE_START) {
    s_error = "";
    String n = cleanName(up.filename);
    snprintf(s_name, sizeof(s_name), "%s", n.c_str());
    s_bytes = 0;
    if (!isPlayable(n)) {
      s_error = "Not an audio file";
      return;
    }
    if (SD.exists("/" + n)) SD.remove("/" + n);
    s_file = SD.open("/" + n, FILE_WRITE);
    if (!s_file) {
      s_error = "Couldn't write to the card";
      return;
    }
    s_busy = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_file && s_file.write(up.buf, up.currentSize) != up.currentSize) {
      s_error = "Card is full or failed";
      s_file.close();
      SD.remove(String("/") + s_name);
    }
    s_bytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_file) {
      s_file.close();
      if (!*s_error) s_added++;
    }
    s_busy = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_file) s_file.close();
    SD.remove(String("/") + s_name);
    s_error = "Upload was interrupted";
    s_busy = false;
  }
}

static void handleUploadDone() {
  if (*s_error) s_server->send(400, "text/plain", s_error);
  else s_server->send(200, "text/plain", "Added");
}

bool start() {
  if (s_running) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!s_server) {
    s_server = new WebServer(80);
    s_server->on("/", HTTP_GET, handleRoot);
    s_server->on("/songs", HTTP_GET, handleSongs);
    s_server->on("/delete", HTTP_POST, handleDelete);
    s_server->on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
    s_server->onNotFound([]() { s_server->send(404, "text/plain", "Not found"); });
  }
  s_server->begin();
  MDNS.begin("jigglypuff");
  MDNS.addService("http", "tcp", 80);
  s_running = true;
  return true;
}

void stop() {
  if (!s_running) return;
  if (s_file) s_file.close();
  s_busy = false;
  s_server->stop();
  MDNS.end();
  s_running = false;
}

void poll() {
  if (s_running) s_server->handleClient();
}

bool running() { return s_running; }
bool busy() { return s_busy; }
const char *currentName() { return s_name; }
uint32_t bytesReceived() { return s_bytes; }
uint32_t filesAdded() { return s_added; }
const char *lastError() { return s_error; }

} // namespace SongUpload
