"""GET /gallery — página mínima para validar la cámara de punta a punta (plan §B1).

No es la app: no tiene datos propios; usa la API con el token de usuario que va en ?token=.
"""

from fastapi import APIRouter
from fastapi.responses import HTMLResponse

router = APIRouter(include_in_schema=False)

_HTML = """<!doctype html><html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MasterGrow · capturas</title>
<style>
:root{--bg:#f6f7f5;--fg:#1d231c;--muted:#6b7468;--card:#fff;--line:#dfe3dc;--accent:#2f7d32}
@media (prefers-color-scheme:dark){:root{--bg:#121512;--fg:#e6ebe4;--muted:#94a08f;
--card:#1b201a;--line:#2b3229;--accent:#7cc47f}}
*{box-sizing:border-box}body{margin:0;padding:16px;background:var(--bg);color:var(--fg);
font:14px/1.4 system-ui,sans-serif}
header{display:flex;gap:12px;align-items:baseline;flex-wrap:wrap;margin-bottom:16px}
h1{font-size:18px;margin:0}#st{color:var(--muted)}
select{background:var(--card);color:var(--fg);border:1px solid var(--line);border-radius:6px;
padding:4px 8px}
#g{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}
.c{background:var(--card);border:1px solid var(--line);border-radius:8px;overflow:hidden}
.c img{display:block;width:100%;aspect-ratio:4/3;object-fit:cover;background:var(--line)}
.c div{padding:8px 10px;font-size:12px;color:var(--muted)}.c b{color:var(--fg)}
.hi{color:var(--accent);font-weight:600}
</style></head><body>
<header><h1>Capturas</h1>
<select id="n"><option value="">todos los nodos</option></select>
<span id="st">cargando…</span></header>
<div id="g"></div>
<script>
const P=new URLSearchParams(location.search),T=P.get('token')||'';
const api=p=>'/api/v1'+p+(p.includes('?')?'&':'?')+'token='+encodeURIComponent(T);
const nodes=new Set();
async function load(){
  const n=document.getElementById('n').value;
  const r=await fetch(api('/captures?limit=48'+(n?'&node_id='+encodeURIComponent(n):'')));
  const st=document.getElementById('st');
  if(!r.ok){st.textContent='error '+r.status+(r.status==401?' (falta ?token=)':'');return}
  const d=await r.json(),g=document.getElementById('g');g.replaceChildren();
  for(const c of d.items){
    if(!nodes.has(c.node_id)){nodes.add(c.node_id);
      document.getElementById('n').add(new Option(c.node_id,c.node_id))}
    const e=document.createElement('a');e.className='c';e.href=api('/captures/'+c.capture_id+'/image');
    e.target='_blank';
    const im=new Image();im.loading='lazy';im.src=api('/captures/'+c.capture_id+'/thumb');
    const t=document.createElement('div');
    const when=new Date(c.timestamp).toLocaleString()+(c.timestamp_approx?' (aprox.)':'');
    t.innerHTML='<b></b> · <span></span><br>';t.querySelector('b').textContent=c.node_id;
    t.querySelector('span').textContent=c.trigger_type;
    if(c.priority==='high')t.querySelector('span').className='hi';
    t.append(when+' · '+c.resolution+' · '+Math.round(c.image_bytes/1024)+' KB');
    e.append(im,t);g.append(e)}
  st.textContent=d.items.length+' capturas · '+new Date().toLocaleTimeString();
}
document.getElementById('n').onchange=load;load();setInterval(load,30000);
</script></body></html>"""


@router.get("/gallery", response_class=HTMLResponse)
async def gallery() -> str:
    return _HTML
