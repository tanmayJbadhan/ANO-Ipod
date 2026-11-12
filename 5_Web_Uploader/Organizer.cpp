#include "Organizer.h"
#include "SongUpload.h"
#include "Library.h"
#include "Playlists.h"
#include "MusicPlayer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <strings.h>
#include <esp_heap_caps.h>

namespace {

WebServer *srv = nullptr;
uint32_t uploadsAtScan = 0;

const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jigglypuff · Organize</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font:16px/1.45 -apple-system,BlinkMacSystemFont,"SF Pro Text",system-ui,sans-serif;color:#f4f2ff;
background:radial-gradient(60% 50% at 15% 10%,#2a5bff88,transparent),radial-gradient(55% 50% at 90% 95%,#dc288c88,transparent),
radial-gradient(40% 40% at 80% 5%,#00c8be55,transparent),#0b0d22;background-attachment:fixed;display:flex;justify-content:center;padding:28px 16px 110px}
main{width:100%;max-width:720px;display:flex;flex-direction:column;gap:16px}
.glass{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.22);border-radius:22px;padding:18px 20px;
backdrop-filter:blur(18px) saturate(1.4);-webkit-backdrop-filter:blur(18px) saturate(1.4);box-shadow:inset 0 1px 0 rgba(255,255,255,.25)}
h1{margin:0;font-size:26px;letter-spacing:-.02em}
p{margin:6px 0 0;color:#c9c5e6}
a{color:#ff8ab0}
.head{display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap}
.tabs,.chips{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
button,select{font:inherit;color:#f4f2ff;background:rgba(255,255,255,.12);border:1px solid rgba(255,255,255,.22);border-radius:999px;
padding:7px 14px;cursor:pointer}
button.on{background:#ff2d55;border-color:#ff2d55}
button.plain{background:none;border:0;padding:4px 6px;color:#ff8ab0}
button.danger{color:#ff7a8a}
button.play{background:rgba(255,255,255,.12);border:0;width:34px;height:34px;padding:0;flex:none;color:#fff;font-size:13px}
button.play.now{background:#ff2d55}
input[type=search],input[type=text]{width:100%;font:inherit;color:#f4f2ff;background:rgba(0,0,0,.25);border:1px solid rgba(255,255,255,.22);
border-radius:14px;padding:10px 14px}
ul{list-style:none;margin:0;padding:0}
.song{display:flex;gap:12px;align-items:center;padding:10px 4px;border-top:1px solid rgba(255,255,255,.1)}
.song:first-child{border-top:0}
.song input{width:20px;height:20px;flex:none;accent-color:#ff2d55}
.song .t{flex:1;min-width:0;cursor:pointer}
.song b,.song span,.song small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.song span{color:#c9c5e6;font-size:14px}
.song small{color:#8d88b3;font-size:12px}
.miss{color:#ffb35c}
.meta{display:flex;justify-content:space-between;align-items:center;gap:10px;margin:12px 0 4px;color:#a9a4cc;font-size:14px}
.selbar{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);width:calc(100% - 32px);max-width:720px;display:none;
gap:8px;align-items:center;flex-wrap:wrap;z-index:5}
.selbar.show{display:flex}
.selbar b{margin-right:auto}
#player{position:sticky;top:8px;z-index:4;display:none;flex-direction:column;gap:8px}
#player.show{display:flex}
#player .now{display:flex;align-items:center;gap:10px}
#player .now div{flex:1;min-width:0}
#player .now b,#player .now span{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#player .now span{color:#c9c5e6;font-size:14px}
audio{width:100%;height:36px}
dialog{border:0;border-radius:22px;padding:22px;width:min(460px,calc(100% - 32px));color:#f4f2ff;background:#1b1c3a;
box-shadow:0 20px 60px #000a}
dialog::backdrop{background:#0006}
dialog label{display:block;margin:12px 0 4px;color:#a9a4cc;font-size:14px}
.row{display:flex;gap:8px;justify-content:flex-end;align-items:center;margin-top:18px;flex-wrap:wrap}
.pl{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:10px 0;border-top:1px solid rgba(255,255,255,.1)}
.pl:first-child{border-top:0}
.pl input{width:20px;height:20px;flex:none;accent-color:#ff2d55}
.pl .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pl .n small{color:#a9a4cc;margin-left:6px}
.pl .n i{display:block;font-style:normal;color:#8d88b3;font-size:13px;overflow:hidden;text-overflow:ellipsis}
.inside{padding:0 0 8px 14px}
.chip{display:inline-flex;gap:6px;align-items:center;background:#ff2d5533;border:1px solid #ff2d55;border-radius:999px;padding:4px 6px 4px 12px;margin-top:12px}
.toast{position:fixed;top:16px;left:50%;transform:translateX(-50%);background:#30d178;color:#04140a;font-weight:600;
padding:8px 16px;border-radius:999px;display:none;z-index:9}
.busy{opacity:.55;pointer-events:none}
</style></head><body><main>
<section class="glass head"><div><h1>Organize</h1><p>Fix song details, listen, and build playlists.</p></div>
<a href="/">&larr; Add songs</a></section>

<section class="glass" id="player"><div class="now"><button class="play now" id="pToggle">&#10074;&#10074;</button>
<div><b id="pTitle"></b><span id="pSub"></span></div>
<button class="plain" id="pEdit">Edit</button><button class="plain" id="pNext">Next &rsaquo;</button></div>
<audio id="audio" preload="auto"></audio></section>

<div class="tabs"><button data-tab="songs" class="on">Songs</button><button data-tab="artists">Artists</button>
<button data-tab="albums">Albums</button><button data-tab="lists">Playlists</button>
<button id="rescan" class="plain" style="margin-left:auto">Rescan card</button></div>

<section class="glass" id="songsTab">
<input type="search" id="q" placeholder="Search title, artist, album or file">
<div id="gf"></div>
<div class="chips" style="margin-top:12px">
<button data-f="all" class="on">All</button><button data-f="garbled">Garbled ??</button><button data-f="artist">Missing artist</button>
<button data-f="album">Missing album</button><button data-f="title">Untitled</button></div>
<div class="meta"><span id="count">Loading…</span><button class="plain" id="selAll">Select all shown</button></div>
<ul id="list"></ul>
<div style="text-align:center;margin-top:10px"><button id="more" hidden>Show more</button></div>
</section>

<section class="glass" id="groupTab" hidden>
<div style="display:flex;gap:8px"><input type="search" id="gq" placeholder="Search">
<button id="newAlbum" hidden style="white-space:nowrap">+ New album</button></div>
<div class="meta"><span id="gcount"></span><span>Tick several to merge them</span></div>
<ul id="groups"></ul>
</section>

<section class="glass" id="listsTab" hidden>
<div style="display:flex;gap:8px"><input type="text" id="newName" placeholder="New playlist name" maxlength="36">
<button id="create">Create</button></div>
<ul id="pls" style="margin-top:12px"></ul>
</section>
</main>

<div id="bar" class="glass selbar"><b id="selN"></b>
<button id="setArtist">Set artist</button><button id="setAlbum">Set album</button><button id="mkAlbum">New album…</button>
<select id="addTo"></select><button class="plain" id="clear">Clear</button></div>

<div id="gbar" class="glass selbar"><b id="gselN"></b><button class="on" id="merge">Merge…</button>
<button class="plain" id="gclear">Clear</button></div>

<dialog id="dlg"><h3 id="dTitle" style="margin:0">Edit song</h3><p id="dSub" style="font-size:13px;word-break:break-all"></p>
<button id="dPlay" style="margin-top:8px">&#9654; Listen</button>
<div id="fTitle"><label>Title</label><input type="text" id="eTitle"></div>
<div id="fArtist"><label>Artist</label><input type="text" id="eArtist" list="artists"></div>
<div id="fAlbum"><label>Album</label><input type="text" id="eAlbum" list="albums"></div>
<datalist id="artists"></datalist><datalist id="albums"></datalist>
<div class="row"><button class="plain" id="dCancel">Cancel</button><button id="dNext">Save &amp; next</button>
<button class="on" id="dSave">Save</button></div></dialog>
<div class="toast" id="toast"></div>

<script>
const $=s=>document.querySelector(s);
const audio=$('#audio');
let S=[],P=[],U={},tab='songs',filter='all',q='',gq='',gf=null,sel=new Set(),gsel=new Set(),limit=200,shown=[],
 editIds=[],editOnly=null,openPl=-1,playing=-1;
const el=(tag,cls,text)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;};
function toast(m){const t=$('#toast');t.textContent=m;t.style.display='block';clearTimeout(t.h);t.h=setTimeout(()=>t.style.display='none',2000);}
async function post(url,data){const b=new URLSearchParams(data);document.body.classList.add('busy');
 try{const r=await fetch(url,{method:'POST',body:b});const j=await r.json();if(!r.ok||j.error)throw new Error(j.error||r.status);return j;}
 catch(e){toast('Failed: '+e.message);throw e;}finally{document.body.classList.remove('busy');}}

async function load(){$('#count').textContent='Reading the library… (can take a moment after new uploads)';
 let j;try{const r=await fetch('/api/library');j=await r.json();if(!j.songs)throw new Error(j.error||'no songs list');}
 catch(e){$('#count').innerHTML='';const m=el('span',null,"Couldn't load the library ("+e.message+"). The console's WiFi may be weak - move it closer to the router. ");
  const b=el('button','plain','Try again');b.onclick=load;$('#count').append(m,b);return;}
 S=j.songs;P=j.playlists;U=j.unknown;sel.clear();gsel.clear();refresh();
 if(!S.length)$('#count').textContent='No songs found on the card. If there should be, tap Rescan card.';}
function refresh(){render();renderGroups();renderLists();fillLists();bars();}

// --- songs ---------------------------------------------------------------
function missing(s){return {artist:s[1]==U.artist,album:s[2]==U.album,title:s[0]==U.title,garbled:/\?\?/.test(s[0]+s[1]+s[2])};}
function matches(s){if(gf&&s[gf.f]!=gf.name)return false;const m=missing(s);if(filter!='all'&&!m[filter])return false;
 if(!q)return true;return (s[0]+'\n'+s[1]+'\n'+s[2]+'\n'+s[3]).toLowerCase().includes(q);}
function render(){shown=[];S.forEach((s,i)=>{if(matches(s))shown.push(i);});
 $('#count').textContent=shown.length+' of '+S.length+' songs';
 const g=$('#gf');g.innerHTML='';
 if(gf){const c=el('span','chip',(gf.f==2?'Album: ':'Artist: ')+gf.name);const x=el('button','plain','✕');x.onclick=()=>{gf=null;render();};c.appendChild(x);g.appendChild(c);}
 const ul=$('#list');ul.innerHTML='';
 for(const i of shown.slice(0,limit)){const s=S[i],m=missing(s),li=el('li','song');
  const pb=el('button','play'+(i==playing?' now':''),i==playing&&!audio.paused?'❚❚':'▶');pb.onclick=()=>i==playing?toggle():play(i);
  const cb=el('input');cb.type='checkbox';cb.checked=sel.has(i);cb.onchange=()=>{cb.checked?sel.add(i):sel.delete(i);bars();};
  const t=el('div','t');t.onclick=()=>edit([i]);
  const b=el('b',m.title||m.garbled?'miss':'',s[0]);
  const sp=el('span');const a=el('i',m.artist?'miss':'',s[1]),al=el('i',m.album?'miss':'',s[2]);a.style.fontStyle=al.style.fontStyle='normal';
  sp.append(a,' · ',al);t.append(b,sp,el('small',null,s[3]));li.append(pb,cb,t);ul.appendChild(li);}
 $('#more').hidden=shown.length<=limit;bars();}
function fillLists(){const ar=new Set(),al=new Set();S.forEach(s=>{if(s[1]!=U.artist)ar.add(s[1]);if(s[2]!=U.album)al.add(s[2]);});
 const f=(id,set)=>{const d=$(id);d.innerHTML='';[...set].sort().forEach(v=>{const o=el('option');o.value=v;d.appendChild(o);});};
 f('#artists',ar);f('#albums',al);}

// --- player --------------------------------------------------------------
function play(i){playing=i;audio.src='/api/song?id='+i;audio.play().catch(()=>{});showPlayer();render();}
function toggle(){if(audio.paused)audio.play();else audio.pause();}
function showPlayer(){if(playing<0)return;const s=S[playing];$('#player').classList.add('show');
 $('#pTitle').textContent=s[0];$('#pSub').textContent=s[1]+' · '+s[2];$('#pToggle').innerHTML=audio.paused?'&#9654;':'&#10074;&#10074;';}
function nextOf(i){const k=shown.indexOf(i);return shown.length?shown[(k+1)%shown.length]:-1;}
audio.onplay=audio.onpause=()=>{showPlayer();render();};
audio.onended=()=>{const n=nextOf(playing);if(n>=0)play(n);};
audio.onerror=()=>{if(playing>=0)toast("Couldn't play this song in the browser");};
$('#pToggle').onclick=toggle;$('#pNext').onclick=()=>{const n=nextOf(playing);if(n>=0)play(n);};
$('#pEdit').onclick=()=>{if(playing>=0)edit([playing]);};

// --- editing -------------------------------------------------------------
function edit(ids,only){editIds=ids;editOnly=only||null;const one=ids.length==1,s=S[ids[0]];
 $('#dTitle').textContent=one?'Edit song':only=='newalbum'?'New album for '+ids.length+' songs':'Edit '+ids.length+' songs';
 $('#dSub').textContent=one?s[3]:'Leave a box empty to keep what each song has.';
 $('#dPlay').hidden=!one;$('#dNext').hidden=!one;
 $('#fTitle').hidden=!one||!!only;$('#fArtist').hidden=only=='album'||only=='newalbum';$('#fAlbum').hidden=only=='artist';
 $('#eTitle').value=one&&s[0]!=U.title?s[0]:'';$('#eArtist').value=one&&s[1]!=U.artist?s[1]:'';
 $('#eAlbum').value=one&&s[2]!=U.album&&only!='newalbum'?s[2]:'';
 $('#eAlbum').placeholder=only=='newalbum'?'Name of the new album':'';
 if(!$('#dlg').open)$('#dlg').showModal();(only=='album'||only=='newalbum'?$('#eAlbum'):only?$('#eArtist'):$('#eTitle')).focus();}
async function save(goNext){const d={ids:editIds.join(',')};
 if(!$('#fTitle').hidden&&$('#eTitle').value.trim())d.title=$('#eTitle').value.trim();
 if(!$('#fArtist').hidden&&$('#eArtist').value.trim())d.artist=$('#eArtist').value.trim();
 if(!$('#fAlbum').hidden&&$('#eAlbum').value.trim())d.album=$('#eAlbum').value.trim();
 const nxt=goNext?nextOf(editIds[0]):-1,wasPlaying=playing==editIds[0]&&!audio.paused;
 if(d.title||d.artist||d.album){const j=await post('/api/edit',d);for(const k in j.songs)S[k]=j.songs[k];toast('Saved');}
 if(editOnly)sel.clear();
 refresh();if(playing>=0)showPlayer();
 if(goNext&&nxt>=0&&nxt!=editIds[0]){edit([nxt]);if(wasPlaying)play(nxt);}else $('#dlg').close();}
$('#dCancel').onclick=()=>$('#dlg').close();
$('#dSave').onclick=()=>save(false);$('#dNext').onclick=()=>save(true);
$('#dPlay').onclick=()=>play(editIds[0]);
$('#dlg').addEventListener('keydown',e=>{if(e.key=='Enter'&&e.target.tagName=='INPUT'){e.preventDefault();save(!$('#dNext').hidden);}});

document.querySelectorAll('[data-f]').forEach(b=>b.onclick=()=>{document.querySelectorAll('[data-f]').forEach(x=>x.classList.remove('on'));
 b.classList.add('on');filter=b.dataset.f;limit=200;render();});
$('#q').oninput=e=>{q=e.target.value.trim().toLowerCase();limit=200;render();};
$('#more').onclick=()=>{limit+=300;render();};
$('#selAll').onclick=()=>{const all=shown.length&&shown.every(i=>sel.has(i));shown.forEach(i=>all?sel.delete(i):sel.add(i));render();};
$('#clear').onclick=()=>{sel.clear();render();};
$('#setArtist').onclick=()=>edit([...sel],'artist');
$('#setAlbum').onclick=()=>edit([...sel],'album');
$('#mkAlbum').onclick=()=>edit([...sel],'newalbum');
$('#addTo').onchange=async e=>{let v=e.target.value;if(v==='')return;
 if(v=='new'){const n=prompt('Name for the new playlist');if(!n){bars();return;}
  const j=await post('/api/playlist',{action:'create',name:n});P=j.playlists;v=j.index;}
 const j=await post('/api/playlist',{action:'add',p:v,ids:[...sel].join(',')});P=j.playlists;
 toast(j.msg);sel.clear();render();renderLists();};
function bars(){$('#bar').classList.toggle('show',tab=='songs'&&sel.size>0);$('#selN').textContent=sel.size+' selected';
 $('#gbar').classList.toggle('show',(tab=='artists'||tab=='albums')&&gsel.size>0);
 $('#gselN').textContent=gsel.size+(tab=='albums'?' albums':' artists');$('#merge').textContent=gsel.size>1?'Merge…':'Rename…';
 const o=$('#addTo');o.innerHTML='<option value="">Add to playlist…</option>'+P.map(()=>'<option></option>').join('')+'<option value="new">+ New playlist…</option>';
 P.forEach((p,i)=>{o.options[i+1].textContent=p.name;o.options[i+1].value=i;});}

// --- artists & albums ----------------------------------------------------
function field(){return tab=='albums'?2:4;}
function groups(){const f=field(),m=new Map();
 S.forEach((s,i)=>{const k=s[f];if(!m.has(k))m.set(k,{name:k,ids:[],who:new Set()});const g=m.get(k);g.ids.push(i);if(f==2)g.who.add(s[4]);});
 return [...m.values()].sort((a,b)=>a.name.localeCompare(b.name,undefined,{sensitivity:'base'}));}
function renderGroups(){if(tab!='artists'&&tab!='albums')return;const f=field(),all=groups(),ul=$('#groups');ul.innerHTML='';
 const list=all.filter(g=>!gq||g.name.toLowerCase().includes(gq));
 $('#gcount').textContent=list.length+(f==2?' albums':' artists');$('#newAlbum').hidden=f!=2;
 for(const g of list.slice(0,600)){const li=el('li','pl');
  const cb=el('input');cb.type='checkbox';cb.checked=gsel.has(g.name);cb.onchange=()=>{cb.checked?gsel.add(g.name):gsel.delete(g.name);bars();};
  const n=el('span','n',g.name);if(g.name==U.artist||g.name==U.album)n.classList.add('miss');
  n.appendChild(el('small',null,g.ids.length+(g.ids.length==1?' song':' songs')));
  if(f==2)n.appendChild(el('i',null,[...g.who].slice(0,3).join(', ')+(g.who.size>3?' …':'')));
  const sb=el('button','plain','Songs');sb.onclick=()=>{gf={f,name:g.name};filter='all';
   document.querySelectorAll('[data-f]').forEach(x=>x.classList.toggle('on',x.dataset.f=='all'));setTab('songs');};
  const rb=el('button','plain','Rename');rb.onclick=()=>regroup([g.name]);
  li.append(cb,n,sb,rb);ul.appendChild(li);}
 bars();}
async function regroup(names){const f=field(),what=f==2?'album':'artist';
 const nn=prompt(names.length>1?'Merge '+names.length+' '+what+'s into one called:':'New name for this '+what+':',names[0]);
 if(!nn||!nn.trim()||(names.length==1&&nn.trim()==names[0]))return;
 if(names.includes(U.artist)||names.includes(U.album)){if(!confirm('This gives every song without a '+what+' the name "'+nn.trim()+'". Continue?'))return;}
 const ids=[];S.forEach((s,i)=>{if(names.includes(s[f]))ids.push(i);});
 const d={ids:ids.join(',')};d[what]=nn.trim();
 const j=await post('/api/edit',d);for(const k in j.songs)S[k]=j.songs[k];
 toast((names.length>1?'Merged into ':'Renamed to ')+nn.trim());gsel.clear();refresh();}
$('#merge').onclick=()=>regroup([...gsel]);
$('#gclear').onclick=()=>{gsel.clear();renderGroups();};
$('#gq').oninput=e=>{gq=e.target.value.trim().toLowerCase();renderGroups();};
$('#newAlbum').onclick=()=>{if(!sel.size){toast('First tick the songs for it on the Songs tab');setTab('songs');return;}edit([...sel],'newalbum');};

// --- playlists -----------------------------------------------------------
function renderLists(){const ul=$('#pls');ul.innerHTML=P.length?'':'<li class="pl"><span class="n">No playlists yet</span></li>';
 P.forEach((p,i)=>{const li=el('li');const row=el('div','pl');
  const n=el('span','n',p.name);n.appendChild(el('small',null,p.ids.length+' songs'));
  const o=el('button','plain',openPl==i?'Close':'Open');o.onclick=()=>{openPl=openPl==i?-1:i;renderLists();};
  const r=el('button','plain','Rename');
  r.onclick=async()=>{const nn=prompt('New name',p.name);if(!nn||nn==p.name)return;const j=await post('/api/playlist',{action:'rename',p:i,name:nn});P=j.playlists;openPl=-1;renderLists();bars();};
  const d=el('button','plain danger','Delete');
  d.onclick=async()=>{if(!confirm('Delete "'+p.name+'"? The songs stay on the card.'))return;const j=await post('/api/playlist',{action:'delete',p:i});P=j.playlists;openPl=-1;renderLists();bars();};
  row.append(n,o,r,d);li.appendChild(row);
  if(openPl==i){const inner=el('ul','inside');
   if(!p.ids.length){const e=el('li',null,'Empty - tick songs on the Songs tab and use "Add to playlist".');e.style.color='#a9a4cc';inner.appendChild(e);}
   p.ids.forEach(id=>{const s=S[id];if(!s)return;const e=el('li','song');
    const pb=el('button','play'+(id==playing?' now':''),'▶');pb.onclick=()=>play(id);
    const t=el('div','t');t.append(el('b',null,s[0]),el('span',null,s[1]+' · '+s[2]));
    const x=el('button','plain danger','Remove');
    x.onclick=async()=>{const j=await post('/api/playlist',{action:'remove',p:i,ids:id});P=j.playlists;renderLists();};
    e.append(pb,t,x);inner.appendChild(e);});
   li.appendChild(inner);}
  ul.appendChild(li);});}
$('#create').onclick=async()=>{const n=$('#newName').value.trim();if(!n)return;
 const j=await post('/api/playlist',{action:'create',name:n});P=j.playlists;$('#newName').value='';toast(j.msg);renderLists();bars();};

// --- tabs ----------------------------------------------------------------
function setTab(t){tab=t;document.querySelectorAll('[data-tab]').forEach(b=>b.classList.toggle('on',b.dataset.tab==t));
 $('#songsTab').hidden=t!='songs';$('#groupTab').hidden=t!='artists'&&t!='albums';$('#listsTab').hidden=t!='lists';
 if(t=='artists'||t=='albums'){gsel.clear();gq='';$('#gq').value='';renderGroups();}
 if(t=='songs')render();bars();}
document.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>setTab(b.dataset.tab));
$('#rescan').onclick=async()=>{$('#count').textContent='Rescanning the card…';await post('/api/rescan',{});await load();toast('Library updated');};
load();
</script></body></html>)HTML";

// --- JSON out, sent in chunks (the library is too big for one String) -------

// Big chunks matter: each chunk costs WiFi round trips, and 1.5 KB chunks
// took over a minute for a thousand songs.
//
// Big scratch buffers come from PSRAM for one request and go back after,
// so the organizer holds no fast internal RAM the rest of the time.
template <typename T> struct Scratch {
  T *p;
  explicit Scratch(size_t n) : p((T *)heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {}
  ~Scratch() { heap_caps_free(p); }
  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;
};

struct Json {
  static const size_t CAP = 8192;
  Scratch<char> mem{CAP};
  char *buf = mem.p; // (if PSRAM is somehow out, replies are sent in tiny pieces)
  char tiny[64];
  size_t cap = buf ? CAP : sizeof(tiny);
  size_t n = 0;
  Json() { if (!buf) buf = tiny; }
  void flush() {
    if (n) srv->sendContent(buf, n);
    n = 0;
  }
  void raw(const char *s) {
    while (*s) {
      if (n >= cap - 8) flush();
      buf[n++] = *s++;
    }
  }
  void num(long v) {
    char t[16];
    snprintf(t, sizeof(t), "%ld", v);
    raw(t);
  }
  void str(const char *s) {
    raw("\"");
    for (; *s; s++) {
      if (n >= cap - 8) flush();
      uint8_t c = (uint8_t)*s;
      if (c == '"' || c == '\\') { buf[n++] = '\\'; buf[n++] = c; }
      else if (c < 0x20) n += snprintf(buf + n, 8, "\\u%04x", c);
      else buf[n++] = c;
    }
    raw("\"");
  }
};

void begin(Json &) {
  // No WiFi power saving (it adds ~100 ms to every round trip) and no
  // Nagle delay on the small chunk headers. WiFi only runs while Add songs
  // is open, so the battery cost is small.
  WiFi.setSleep(false);
  srv->client().setNoDelay(true);
  srv->setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv->send(200, "application/json", "");
}

void end(Json &j) {
  // (j.flush sends whatever is left; the empty chunk ends the reply)
  j.flush();
  srv->sendContent("");
}

void song(Json &j, uint16_t id) {
  const Track &t = Library::track(id);
  j.raw("[");
  j.str(t.title); j.raw(",");
  j.str(t.artist); j.raw(",");
  j.str(t.album); j.raw(",");
  j.str(t.path); j.raw(",");
  j.str(t.albumArtist); // who the console's Artists list files it under
  j.raw("]");
}

void playlists(Json &j) {
  Scratch<uint16_t> idMem(Library::MAX_TRACKS);
  uint16_t *ids = idMem.p;
  if (!ids) return j.raw("[]");
  j.raw("[");
  for (uint8_t i = 0; i < Playlists::count(); i++) {
    if (i) j.raw(",");
    j.raw("{\"name\":");
    j.str(Playlists::name(i));
    j.raw(",\"ids\":[");
    uint16_t n = Playlists::songs(i, ids, Library::MAX_TRACKS);
    for (uint16_t k = 0; k < n; k++) {
      if (k) j.raw(",");
      j.num(ids[k]);
    }
    j.raw("]}");
  }
  j.raw("]");
}

void fail(int code, const char *msg) {
  String body = String("{\"error\":\"") + msg + "\"}";
  srv->send(code, "application/json", body);
}

// "3,17,42" -> ids (only ones that exist). Returns how many.
uint16_t parseIds(const String &s, uint16_t *out, uint16_t max) {
  uint16_t n = 0;
  const char *p = s.c_str();
  while (*p && n < max) {
    char *end;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) { p++; continue; }
    if (v < Library::count()) out[n++] = (uint16_t)v;
    p = end;
  }
  return n;
}

// --- library state ---------------------------------------------------------

void rescan() {
  // Ids change on a rescan: anything holding them starts over.
  MusicPlayer::clearQueue();
  Library::scan();
  Playlists::load();
  uploadsAtScan = SongUpload::filesAdded();
}

bool ensureLibrary() {
  if (!MusicPlayer::sdReady()) return false;
  if (!Library::ready() || SongUpload::filesAdded() != uploadsAtScan) rescan();
  return Library::ready();
}

// --- handlers ----------------------------------------------------------------

void handlePage() {
  WiFi.setSleep(false); // see begin()
  srv->send_P(200, "text/html", PAGE);
}

void handleLibrary() {
  if (!ensureLibrary()) return fail(503, "No SD card");
  Json j;
  begin(j);
  j.raw("{\"unknown\":{\"artist\":");
  j.str(Library::UNKNOWN_ARTIST);
  j.raw(",\"album\":");
  j.str(Library::UNKNOWN_ALBUM);
  j.raw(",\"title\":");
  j.str(Library::UNTITLED);
  j.raw("},\"songs\":[");
  for (uint16_t i = 0; i < Library::count(); i++) {
    if (i) j.raw(",");
    song(j, i);
  }
  j.raw("],\"playlists\":");
  playlists(j);
  j.raw("}");
  end(j);
}

void handleEdit() {
  if (!ensureLibrary()) return fail(503, "No SD card");
  Scratch<uint16_t> idMem(Library::MAX_TRACKS);
  uint16_t *ids = idMem.p;
  if (!ids) return fail(503, "Out of memory");
  uint16_t n = parseIds(srv->arg("ids"), ids, Library::MAX_TRACKS);
  if (!n) return fail(400, "No songs chosen");
  String title = srv->arg("title"), artist = srv->arg("artist"), album = srv->arg("album");
  for (uint16_t k = 0; k < n; k++) {
    Library::setText(ids[k], n == 1 ? title.c_str() : nullptr, artist.c_str(), album.c_str());
  }
  Library::commitEdits();

  // Send back the edited songs as the console now has them (text folded to
  // what its screen can show).
  Json j;
  begin(j);
  j.raw("{\"songs\":{");
  for (uint16_t k = 0; k < n; k++) {
    if (k) j.raw(",");
    j.raw("\"");
    j.num(ids[k]);
    j.raw("\":");
    song(j, ids[k]);
  }
  j.raw("}}");
  end(j);
}

void handlePlaylist() {
  if (!ensureLibrary()) return fail(503, "No SD card");
  String action = srv->arg("action");
  int p = srv->hasArg("p") ? srv->arg("p").toInt() : -1;
  bool validP = p >= 0 && p < Playlists::count();
  Scratch<uint16_t> idMem(Library::MAX_TRACKS);
  uint16_t *ids = idMem.p;
  if (!ids) return fail(503, "Out of memory");
  char msg[64] = "Done";
  int index = -1;

  if (action == "create") {
    index = Playlists::create(srv->arg("name").c_str());
    if (index < 0) return fail(400, "That name is taken or can't be used");
    snprintf(msg, sizeof(msg), "Made %s", Playlists::name(index));
  } else if (action == "rename") {
    if (!validP || !Playlists::rename(p, srv->arg("name").c_str())) return fail(400, "That name is taken or can't be used");
  } else if (action == "delete") {
    if (!validP || !Playlists::erase(p)) return fail(400, "No such playlist");
  } else if (action == "add" || action == "remove") {
    if (!validP) return fail(400, "No such playlist");
    uint16_t n = parseIds(srv->arg("ids"), ids, Library::MAX_TRACKS);
    uint16_t changed = 0;
    for (uint16_t k = 0; k < n; k++) {
      if (action == "add" ? Playlists::add(p, ids[k]) : Playlists::remove(p, ids[k])) changed++;
    }
    if (action == "add") {
      snprintf(msg, sizeof(msg), "Added %u to %s", changed, Playlists::name(p));
      if (changed < n) snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), " (%u already in it)", n - changed);
    } else {
      snprintf(msg, sizeof(msg), "Removed");
    }
  } else {
    return fail(400, "Unknown action");
  }

  Json j;
  begin(j);
  j.raw("{\"msg\":");
  j.str(msg);
  j.raw(",\"index\":");
  j.num(index);
  j.raw(",\"playlists\":");
  playlists(j);
  j.raw("}");
  end(j);
}

// Streams a song to the browser's player. Replies to range requests with
// at most CHUNK bytes at a time (the browser just asks for the next part),
// so one song never holds up the console - or the next request - for long,
// and the player can jump around in the song.
void handleSong() {
  if (!ensureLibrary()) return fail(503, "No SD card");
  long id = srv->arg("id").toInt();
  if (!srv->hasArg("id") || id < 0 || id >= Library::count()) return fail(404, "No such song");
  const char *path = Library::track(id).path;
  File f = SD.open(path, FILE_READ);
  if (!f) return fail(404, "Can't open the song");
  uint32_t size = f.size();

  const char *type = "audio/mpeg";
  const char *dot = strrchr(path, '.');
  if (dot) {
    if (!strcasecmp(dot, ".m4a")) type = "audio/mp4";
    else if (!strcasecmp(dot, ".aac")) type = "audio/aac";
    else if (!strcasecmp(dot, ".flac")) type = "audio/flac";
    else if (!strcasecmp(dot, ".wav")) type = "audio/wav";
  }

  const uint32_t CHUNK = 256 * 1024;
  uint32_t start = 0, end = size ? size - 1 : 0;
  bool ranged = false;
  String range = srv->header("Range"); // "bytes=START-" or "bytes=START-END"
  if (range.startsWith("bytes=")) {
    ranged = true;
    start = strtoul(range.c_str() + 6, nullptr, 10);
    int dash = range.indexOf('-');
    if (dash > 0 && dash + 1 < (int)range.length()) end = strtoul(range.c_str() + dash + 1, nullptr, 10);
    if (start >= size) {
      f.close();
      srv->sendHeader("Content-Range", String("bytes */") + size);
      srv->send(416, "text/plain", "");
      return;
    }
    if (end >= size) end = size - 1;
    if (end - start + 1 > CHUNK) end = start + CHUNK - 1;
  }

  WiFi.setSleep(false);
  srv->client().setNoDelay(true);
  srv->sendHeader("Accept-Ranges", "bytes");
  if (ranged) srv->sendHeader("Content-Range", String("bytes ") + start + "-" + end + "/" + size);
  srv->setContentLength(end - start + 1);
  srv->send(ranged ? 206 : 200, type, "");

  f.seek(start);
  uint32_t left = end - start + 1;
  const uint32_t BUF = 8192;
  Scratch<uint8_t> mem(BUF);
  uint8_t *buf = mem.p;
  while (buf && left > 0) {
    int n = f.read(buf, left < BUF ? left : BUF);
    if (n <= 0) break;
    if (srv->client().write(buf, n) != (size_t)n) break; // browser gave up on this part
    left -= n;
  }
  f.close();
}

void handleRescan() {
  if (!MusicPlayer::sdReady()) return fail(503, "No SD card");
  rescan();
  srv->send(200, "application/json", "{\"ok\":true}");
}

} // namespace

namespace Organizer {

void attach(WebServer *server) {
  srv = server;
  srv->on("/organize", HTTP_GET, handlePage);
  srv->on("/api/library", HTTP_GET, handleLibrary);
  srv->on("/api/edit", HTTP_POST, handleEdit);
  srv->on("/api/playlist", HTTP_POST, handlePlaylist);
  srv->on("/api/rescan", HTTP_POST, handleRescan);
  srv->on("/api/song", HTTP_GET, handleSong);
  // The player's range requests. (Only affects which request headers the
  // server keeps; uploads don't read any.)
  static const char *headers[] = {"Range"};
  srv->collectHeaders(headers, 1);
}

uint32_t uploadsAtLastScan() { return uploadsAtScan; }

} // namespace Organizer
