// Pagina web que sirve la placa. Muestra lo mismo que la pantalla.
// Pide los datos a /datos cada medio segundo.
#pragma once

const char PAGINA_WEB[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MocciaCAN</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; background: #0f1115; color: #e6e6e6; font-family: system-ui, sans-serif; }
  header { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; padding: 10px 16px; background: #1a1d24; }
  h1 { margin: 0 auto 0 0; font-size: 26px; }
  nav button { font-size: 18px; font-weight: bold; padding: 10px 22px; border: 0; border-radius: 10px;
               background: #0f1115; color: #8a8f98; cursor: pointer; }
  nav button.activa { background: #00c2ff; color: #0f1115; }
  main { max-width: 800px; margin: 0 auto; padding: 16px; }
  .pagina { display: none; }
  .pagina.activa { display: block; }
  .rejilla { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
  @media (max-width: 560px) { .rejilla { grid-template-columns: 1fr; } }
  .tarjeta { background: #1a1d24; border-radius: 14px; padding: 16px 20px; }
  .titulo { color: #8a8f98; font-weight: bold; display: flex; justify-content: space-between; }
  .titulo span { font-weight: normal; }
  .valor { font-size: 64px; font-weight: bold; text-align: center; margin: 12px 0; }
  .barra { height: 12px; background: #2a2e37; }
  .barra div { height: 100%; background: #00c2ff; width: 0; }
  table { width: 100%; border-collapse: collapse; font-family: ui-monospace, monospace; font-size: 16px; }
  th { text-align: left; color: #8a8f98; font-family: system-ui, sans-serif; border-bottom: 1px solid #2a2e37; padding: 8px 4px; }
  td { padding: 8px 4px; }
  .resumen { color: #00c2ff; font-weight: bold; margin-bottom: 8px; }
  #demo { width: 100%; font-size: 26px; font-weight: bold; padding: 30px; border: 0; border-radius: 14px; cursor: pointer; }
  #demo.si { background: #2ecc71; color: #0f1115; }
  #demo.no { background: #2a2e37; color: #e6e6e6; }
  h3 { color: #00c2ff; margin: 0 0 8px; }
  p { margin: 6px 0; }
  .gris { color: #8a8f98; }
  #estado { font-size: 14px; }
</style>
</head>
<body>
<header>
  <h1>MocciaCAN</h1>
  <nav>
    <button class="activa" data-p="0">Tablero</button>
    <button data-p="1">CAN</button>
    <button data-p="2">Ajustes</button>
  </nav>
  <span id="estado" class="gris">Conectando...</span>
</header>
<main>
  <section class="pagina activa">
    <div class="rejilla">
      <div class="tarjeta"><div class="titulo">Velocidad <span>km/h</span></div><div class="valor" id="vel">-</div><div class="barra"><div id="bvel"></div></div></div>
      <div class="tarjeta"><div class="titulo">Revoluciones <span>rpm</span></div><div class="valor" id="rpm">-</div><div class="barra"><div id="brpm"></div></div></div>
      <div class="tarjeta"><div class="titulo">Temperatura <span>grados C</span></div><div class="valor" id="temp">-</div><div class="barra"><div id="btemp"></div></div></div>
      <div class="tarjeta"><div class="titulo">Bateria <span>voltios</span></div><div class="valor" id="bat">-</div><div class="barra"><div id="bbat"></div></div></div>
    </div>
  </section>

  <section class="pagina">
    <div class="tarjeta">
      <div class="resumen" id="resumen">Tramas por segundo: 0 &nbsp; Total: 0</div>
      <table>
        <thead><tr><th>ID</th><th>Bytes</th><th>Datos</th><th>Veces</th></tr></thead>
        <tbody id="tabla"></tbody>
      </table>
    </div>
  </section>

  <section class="pagina">
    <div class="rejilla">
      <div>
        <button id="demo" class="si">Modo demo: SI</button>
        <div class="tarjeta" style="margin-top:16px">
          <p class="gris">Toca el boton verde para activar o quitar los datos simulados.</p>
          <p class="gris">Quitalo para ver el CAN real.</p>
        </div>
      </div>
      <div class="tarjeta">
        <h3>App web</h3>
        <p>Red WiFi: <b id="wifi"></b></p>
        <p>Clave: <b id="clave"></b></p>
        <p>Abre: <b id="ip"></b></p>
        <h3 style="margin-top:20px">Sistema</h3>
        <p>CAN: 500 kbit/s, solo escucha</p>
        <p>Version: <span id="version"></span></p>
        <p>Encendido: <span id="encendido"></span></p>
      </div>
    </div>
  </section>
</main>

<script>
const $ = id => document.getElementById(id);
let demo = true;

// Pestanas
document.querySelectorAll('nav button').forEach(b => b.onclick = () => {
  const p = +b.dataset.p;
  document.querySelectorAll('nav button').forEach((x, i) => x.classList.toggle('activa', i === p));
  document.querySelectorAll('.pagina').forEach((x, i) => x.classList.toggle('activa', i === p));
});

function barra(id, f) { $(id).style.width = Math.max(0, Math.min(1, f)) * 100 + '%'; }

function mostrar(d) {
  $('vel').textContent = Math.round(d.vel);   barra('bvel', d.vel / 240);
  $('rpm').textContent = d.rpm;               barra('brpm', d.rpm / 8000);
  $('temp').textContent = d.temp;             barra('btemp', d.temp / 120);
  $('bat').textContent = d.bat.toFixed(1);    barra('bbat', (d.bat - 11) / 4);

  $('resumen').innerHTML = 'Tramas por segundo: ' + d.tps + ' &nbsp; Total: ' + d.total;
  $('tabla').innerHTML = d.tramas.length
    ? d.tramas.map(t => `<tr><td>${t.id}</td><td>${t.largo}</td><td>${t.datos}</td><td>${t.veces}</td></tr>`).join('')
    : '<tr><td colspan="4" class="gris">Esperando tramas CAN (500 kbit/s)...</td></tr>';

  demo = d.demo;
  $('demo').textContent = 'Modo demo: ' + (demo ? 'SI' : 'NO');
  $('demo').className = demo ? 'si' : 'no';
  $('wifi').textContent = d.wifi;
  $('clave').textContent = d.clave;
  $('ip').textContent = 'http://' + d.ip;
  $('version').textContent = d.version;
  $('encendido').textContent = d.encendido;
}

async function pedir(url) {
  try {
    const r = await fetch(url);
    mostrar(await r.json());
    $('estado').textContent = 'Conectado';
    $('estado').style.color = '#2ecc71';
  } catch (e) {
    $('estado').textContent = 'Sin conexion';
    $('estado').style.color = '#e74c3c';
  }
}

$('demo').onclick = () => pedir('/demo?on=' + (demo ? 0 : 1));
pedir('/datos');
setInterval(() => pedir('/datos'), 500);
</script>
</body>
</html>
)HTML";
