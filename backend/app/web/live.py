"""GET /live — página mínima para probar la vista en vivo (B5/C8) desde un navegador.

Pide una sesión con el token de usuario de ?token= y muestra los cuadros que llegan por el
WebSocket de espectador. ?node=cam-01 elige la cámara.
"""

from fastapi import APIRouter
from fastapi.responses import HTMLResponse

router = APIRouter(include_in_schema=False)

_HTML = """<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MasterGrow · en vivo</title>
<style>
:root{--bg:#f6f7f5;--fg:#1d231c;--muted:#6b7468;--card:#fff;--line:#dfe3dc;--accent:#2f7d32}
@media (prefers-color-scheme:dark){:root{--bg:#121512;--fg:#e6ebe4;--muted:#94a08f;
--card:#1b201a;--line:#2b3229;--accent:#7cc47f}}
*{box-sizing:border-box}body{margin:0;padding:16px;background:var(--bg);color:var(--fg);
font:14px/1.4 system-ui,sans-serif}
header{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:12px}
h1{font-size:18px;margin:0 8px 0 0}#st{color:var(--muted)}
input,select,button{background:var(--card);color:var(--fg);border:1px solid var(--line);
border-radius:6px;padding:5px 9px;font:inherit}button{cursor:pointer}
button.on{background:var(--accent);color:var(--bg);border-color:var(--accent)}
#v{display:block;width:100%;max-width:960px;aspect-ratio:4/3;object-fit:contain;
background:#000;border-radius:8px}
#g{position:relative;max-width:960px}
#g:after{content:'';position:absolute;inset:0;pointer-events:none;
background:linear-gradient(var(--line),var(--line)) 33.3% 0/1px 100% no-repeat,
linear-gradient(var(--line),var(--line)) 66.6% 0/1px 100% no-repeat,
linear-gradient(var(--line),var(--line)) 0 33.3%/100% 1px no-repeat,
linear-gradient(var(--line),var(--line)) 0 66.6%/100% 1px no-repeat;opacity:.35}
</style></head><body>
<header><h1>En vivo</h1>
<input id="n" size="7" value="cam-01" aria-label="nodo">
<select id="r"><option>qvga</option><option>vga</option></select>
<button id="b">Ver</button><span id="st">detenido</span></header>
<div id="g"><img id="v" alt=""></div>
<script>
const P=new URLSearchParams(location.search),T=P.get('token')||'';
if(P.get('node'))n.value=P.get('node');
const H={'Authorization':'Bearer '+T,'Content-Type':'application/json'};
let ws=null,sid=null,url=null,count=0,times=[],bytes=0;
function show(t){st.textContent=t}
async function start(){
  show('pidiendo sesión…');
  const r=await fetch('/api/v1/stream/sessions',{method:'POST',headers:H,
    body:JSON.stringify({node_id:n.value,resolution:r_.value})});
  const d=await r.json();
  if(!r.ok){show('error '+r.status+': '+(d.message||''));return}
  sid=d.session_id;count=0;times=[];
  show(d.session_id+' · '+d.resolution+' · '+d.fps_max+' fps máx · esperando la cámara…');
  ws=new WebSocket(d.ws_view_url+'?token='+encodeURIComponent(d.token));
  ws.binaryType='blob';
  ws.onmessage=e=>{if(typeof e.data==='string')return;
    const u=URL.createObjectURL(e.data);v.src=u;if(url)URL.revokeObjectURL(url);url=u;
    count++;bytes=e.data.size;const now=performance.now();times.push(now);
    times=times.filter(t=>now-t<3000);
    const fps=times.length>1?((times.length-1)*1000/(now-times[0])).toFixed(1):'–';
    show(sid+' · cuadro '+count+' · '+fps+' fps · '+Math.round(bytes/1024)+' KB')};
  ws.onclose=e=>{show('cerrado ('+e.code+(e.reason?': '+e.reason:'')+')');ws=null;sid=null;
    b.textContent='Ver';b.className=''};
  b.textContent='Detener';b.className='on';
}
async function stop(){
  if(sid)await fetch('/api/v1/stream/sessions/'+sid,{method:'DELETE',headers:H});
  if(ws)ws.close();
}
const r_=document.getElementById('r');
b.onclick=()=>ws?stop():start();
addEventListener('pagehide',()=>{if(sid)fetch('/api/v1/stream/sessions/'+sid,
  {method:'DELETE',headers:H,keepalive:true})});
</script></body></html>"""


@router.get("/live", response_class=HTMLResponse)
async def live() -> str:
    return _HTML
