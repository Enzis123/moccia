/* MocciaCAN - app web (vanilla JS, sin dependencias). Protocolo: docs/SPEC.md */
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var qa = function (sel, el) { return Array.prototype.slice.call((el || document).querySelectorAll(sel)); };
  var SVGNS = 'http://www.w3.org/2000/svg';
  var KMH2MPH = 0.621371;
  var PAGES = ['tablero', 'can', 'graficas', 'ajustes'];

  var S = {
    st: null,                                    // último state.d
    set: { bitrate: 0, demo: false, backlight: false, units: 'kmh' },
    sys: null,
    hist: { speed: [], rpm: [], temp: [] },
    link: 'off',                                 // ok | poll | off
    page: 'tablero',
    k: 1                                         // escala del marco 800x480
  };

  /* ---------------- utilidades ---------------- */
  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; });
  }
  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function fmtUp(s) {
    s = Math.max(0, s | 0);
    var d = Math.floor(s / 86400), h = Math.floor(s / 3600) % 24, m = Math.floor(s / 60) % 60;
    return (d ? d + 'd ' : '') + pad(h) + ':' + pad(m) + ':' + pad(s % 60);
  }
  function mph() { return S.set.units === 'mph'; }
  function spd(kmh) { return mph() ? kmh * KMH2MPH : kmh; }
  function spdUnit() { return mph() ? 'mph' : 'km/h'; }
  function fmtBitrate(b) { return !b ? '--' : b >= 1e6 ? (b / 1e6) + ' Mbit/s' : (b / 1e3) + ' kbit/s'; }
  function clamp(v, a, b) { return v < a ? a : v > b ? b : v; }
  function el(tag, attrs, parent) {
    var e = document.createElementNS(SVGNS, tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }

  /* ---------------- marco 800x480 escalado ---------------- */
  var app = $('app'), root = document.documentElement;
  function fit() {
    var W = window.innerWidth, H = window.innerHeight;
    var fixed = W >= 800 && H >= 480;
    root.classList.toggle('fixed', fixed);
    if (fixed) {
      S.k = Math.min(W / 800, H / 480);
      app.style.transform = 'scale(' + S.k + ')';
      app.style.left = ((W - 800 * S.k) / 2) + 'px';
      app.style.top = ((H - 480 * S.k) / 2) + 'px';
    } else {
      S.k = 1;
      app.style.transform = app.style.left = app.style.top = '';
    }
    drawCharts();
  }

  /* ---------------- indicadores (gauges) ---------------- */
  function ang(v, max) { return 150 + 240 * clamp(v, 0, max) / max; }
  function pt(r, a) { a = a * Math.PI / 180; return [100 + r * Math.cos(a), 100 + r * Math.sin(a)]; }
  function arc(r, a0, a1) {
    var p0 = pt(r, a0), p1 = pt(r, a1);
    return 'M' + p0[0].toFixed(2) + ' ' + p0[1].toFixed(2) + 'A' + r + ' ' + r + ' 0 ' + (a1 - a0 > 180 ? 1 : 0) + ' 1 ' + p1[0].toFixed(2) + ' ' + p1[1].toFixed(2);
  }
  function Gauge(svg, o) {
    this.svg = svg; this.o = o; this.build();
  }
  Gauge.prototype.build = function () {
    var o = this.o, svg = this.svg, i;
    svg.textContent = '';
    el('path', { d: arc(86, 150, 390), 'class': 'g-track' }, svg);
    this.val = el('path', { d: arc(86, 150, 390), 'class': 'g-val', pathLength: 100, 'stroke-dasharray': '0 100' }, svg);
    if (o.red) el('path', { d: arc(75, ang(o.red, o.max), 390), 'class': 'g-red' }, svg);
    var n = o.max / o.minor;
    for (i = 0; i <= n; i++) {
      var v = i * o.minor, a = ang(v, o.max), M = Math.abs(v / o.major - Math.round(v / o.major)) < 1e-6;
      var p0 = pt(78, a), p1 = pt(M ? 69 : 73, a);
      el('line', { x1: p0[0], y1: p0[1], x2: p1[0], y2: p1[1], 'class': 'g-tick' + (M ? ' M' : '') }, svg);
      if (M) {
        var pl = pt(58, a);
        el('text', { x: pl[0].toFixed(1), y: pl[1].toFixed(1), 'class': 'g-num' }, svg).textContent = o.label(v);
      }
    }
    this.needle = el('g', { 'class': 'g-needle' }, svg);
    el('path', { d: 'M100 96.5L172 100L100 103.5Z' }, this.needle);
    el('circle', { cx: 100, cy: 100, r: 7, 'class': 'g-hub' }, svg);
    this.txt = el('text', { x: 100, y: 152, 'class': 'g-v' }, svg);
    this.txt.textContent = '--';
    el('text', { x: 100, y: 170, 'class': 'g-u' }, svg).textContent = o.unit;
    this.v = null;
  };
  Gauge.prototype.set = function (v) {
    var o = this.o;
    if (v == null || isNaN(v)) { this.txt.textContent = '--'; return; }
    var p = clamp(v / o.max, 0, 1) * 100;
    this.val.setAttribute('stroke-dasharray', p.toFixed(2) + ' 100');
    this.val.style.visibility = p < 0.3 ? 'hidden' : '';   // evita puntos del linecap a 0
    this.val.style.stroke = o.red && v >= o.red ? 'var(--err)' : '';
    this.needle.style.transform = 'rotate(' + ang(v, o.max).toFixed(2) + 'deg)';
    this.txt.textContent = Math.round(v);
  };
  var gSpeed, gRpm = new Gauge($('gRpm'), {
    max: 8000, minor: 250, major: 1000, red: 6500, unit: 'rpm', label: function (v) { return v / 1000; }
  });
  function buildSpeed() {
    var m = mph();
    gSpeed = new Gauge($('gSpeed'), {
      max: m ? 160 : 240, minor: m ? 5 : 10, major: m ? 20 : 20, unit: spdUnit(),
      label: function (v) { return (m || v % 40 === 0) ? v : ''; }
    });
  }
  buildSpeed();

  /* ---------------- render: state ---------------- */
  var lightEls = qa('#lights .li');
  function setBar(id, v, pct, lvl, txt) {
    var c = $(id);
    c.querySelector('b').textContent = v == null ? '--' : txt;
    c.querySelector('i').style.width = (v == null ? 0 : clamp(pct, 0, 1) * 100) + '%';
    c.className = 'card bar' + (lvl ? ' ' + lvl : '');
  }
  var BUS = { OK: ['OK', 'ok'], BUSOFF: ['BUS-OFF', 'err'], IDLE: ['SIN TRÁFICO', 'idle'] };

  function renderState() {
    var d = S.st;
    if (!d) return;
    $('uptime').textContent = fmtUp(d.uptime);
    $('iUp').textContent = fmtUp(d.uptime);
    gSpeed.set(spd(+d.speed));
    gRpm.set(+d.rpm);
    var g = $('gear');
    g.textContent = d.gear || '-';
    g.style.color = d.gear === 'R' ? 'var(--warn)' : (d.gear === 'P' || d.gear === 'N') ? 'var(--tx)' : '';
    var lights = d.lights | 0;
    lightEls.forEach(function (e) { e.classList.toggle('on', !!(lights & (1 << +e.getAttribute('data-b')))); });
    var t = +d.temp, f = +d.fuel, b = +d.batt;
    setBar('bTemp', d.temp, (t - 40) / 90, t >= 115 ? 'err' : t >= 105 ? 'warn' : '', Math.round(t));
    setBar('bFuel', d.fuel, f / 100, f < 10 ? 'err' : f < 20 ? 'warn' : '', Math.round(f));
    setBar('bBatt', d.batt, (b - 10) / 5, (b < 11.5 || b > 15.2) ? 'err' : (b < 12 || b > 14.8) ? 'warn' : '', b.toFixed(1));
    // CAN
    $('cFps').textContent = d.fps;
    var bs = BUS[d.bus] || [d.bus || '--', ''];
    $('cBus').textContent = bs[0];
    $('cBus').className = bs[1];
    $('icCan').className = 'canbadge ' + (S.link === 'off' ? '' : bs[1]);
    $('icCan').title = 'Bus CAN: ' + bs[0];
    var bp = $('btnPause');
    bp.textContent = d.paused ? 'Reanudar' : 'Pausar';
    bp.classList.toggle('resume', !!d.paused);
    $('p-can').classList.toggle('paused', !!d.paused);
  }

  /* ---------------- render: tramas ---------------- */
  function renderFrames(list) {
    var h = '';
    (list || []).forEach(function (f) {
      h += '<tr><td>' + esc(f.id) + '</td><td>' + (f.dlc | 0) + '</td><td class="d">' + esc(f.data) +
        '</td><td class="r">' + (f.count | 0) + '</td><td class="r">' + ((f.count | 0) > 1 ? (f.period | 0) + '<span class="lg"> ms</span>' : '-') + '</td></tr>';
    });
    $('fBody').innerHTML = h;
    $('fEmpty').hidden = !!h;
  }

  /* ---------------- render: ajustes / sistema ---------------- */
  function renderSettings() {
    var s = S.set;
    qa('#sBitrate button').forEach(function (b) { b.classList.toggle('on', +b.getAttribute('data-v') === +s.bitrate); });
    qa('#sUnits button').forEach(function (b) { b.classList.toggle('on', b.getAttribute('data-v') === s.units); });
    qa('.sw').forEach(function (b) { b.setAttribute('aria-checked', !!s[b.getAttribute('data-k')]); });
    $('cBit').textContent = fmtBitrate(s.bitrate);
    if (gSpeed.o.unit !== spdUnit()) {
      buildSpeed();
      renderState();
      drawCharts();
    }
  }
  function renderSys() {
    var y = S.sys;
    if (!y) return;
    $('iIp').textContent = y.ip || '--';
    $('iSsid').textContent = (y.ssid || '--') + (y.mode ? ' (' + y.mode + ')' : '');
    $('iRssi').textContent = y.mode === 'AP' ? 'Punto de acceso' : y.rssi + ' dBm';
    $('iHeap').textContent = Math.round(y.heap / 1024) + ' KB';
    $('iPsram').textContent = y.psram >= 1048576 ? (y.psram / 1048576).toFixed(2) + ' MB' : Math.round(y.psram / 1024) + ' KB';
    $('iVer').textContent = y.ver || '--';
    renderWifi();
  }
  function renderWifi() {
    var w = $('icWifi'), c = 'ic wifi', y = S.sys;
    if (S.link !== 'off' && y) {
      var lv = y.mode === 'AP' || y.rssi >= -60 ? 3 : y.rssi >= -70 ? 2 : 1;
      c += ' on l' + lv;
      w.querySelector('title').textContent = y.mode === 'AP' ? 'WiFi: punto de acceso' : 'WiFi: ' + y.rssi + ' dBm';
    }
    w.setAttribute('class', c);
  }
  function renderLink() {
    var l = S.link;
    $('link').className = 'link ' + l;
    $('link').title = l === 'ok' ? 'Conectado (tiempo real)' : l === 'poll' ? 'Solo HTTP, reintentando WebSocket' : 'Sin conexión';
    $('iLink').textContent = l === 'ok' ? 'Conectada (WebSocket)' : l === 'poll' ? 'Solo HTTP (reintentando)' : 'Sin conexión';
    var b = $('banner');
    b.hidden = l === 'ok';
    b.textContent = l === 'poll' ? 'Tiempo real no disponible · datos por HTTP · reintentando…'
      : 'Sin conexión con el dispositivo · reintentando…';
    document.body.classList.toggle('offline', l === 'off');
    qa('[data-net]').forEach(function (e) {
      var isCmd = e.id === 'btnPause' || e.id === 'btnClear';
      e.disabled = isCmd ? l !== 'ok' : l === 'off';
    });
    renderWifi();
    if (S.st) renderState();
  }

  /* ---------------- gráficas ---------------- */
  var CH = {
    speed: { col: '--c-speed', min: 60 },
    rpm: { col: '--c-rpm', min: 2000 },
    temp: { col: '--c-temp', min: 0 }
  };
  function niceStep(range) {
    var raw = range / 4, mag = Math.pow(10, Math.floor(Math.log10(raw))), n = raw / mag;
    return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag;
  }
  function drawCharts() {
    if (S.page !== 'graficas') return;
    var cs = getComputedStyle(root);
    qa('.chart').forEach(function (card) {
      var key = card.getAttribute('data-k'), cfg = CH[key], cv = card.querySelector('canvas');
      var data = S.hist[key].slice(-120);
      if (key === 'speed') data = data.map(spd);
      var last = data.length ? data[data.length - 1] : null;
      card.querySelector('.cv').textContent = last == null ? '--' :
        Math.round(last) + (key === 'speed' ? ' ' + spdUnit() : key === 'rpm' ? ' rpm' : ' °C');
      var w = cv.clientWidth, h = cv.clientHeight;
      if (!w || !h) return;
      var r = (window.devicePixelRatio || 1) * S.k;
      if (cv.width !== Math.round(w * r) || cv.height !== Math.round(h * r)) {
        cv.width = Math.round(w * r); cv.height = Math.round(h * r);
      }
      var c = cv.getContext('2d');
      c.setTransform(r, 0, 0, r, 0, 0);
      c.clearRect(0, 0, w, h);
      var L = 38, R = 6, T = 6, B = 16, pw = w - L - R, ph = h - T - B;
      var lo, hi, mx = cfg.min, mn = Infinity, i;
      for (i = 0; i < data.length; i++) { mx = Math.max(mx, data[i]); mn = Math.min(mn, data[i]); }
      if (key === 'temp') {
        if (!isFinite(mn)) mn = 20;
        lo = Math.floor((Math.min(mn, mx) - 5) / 10) * 10;
        hi = Math.max(lo + 40, Math.ceil((mx + 5) / 10) * 10);
      } else { lo = 0; hi = mx; }
      var step = niceStep(hi - lo);
      hi = lo + Math.ceil((hi - lo) / step - 1e-9) * step;
      var Y = function (v) { return T + ph - (v - lo) / (hi - lo) * ph; };
      var X = function (idx) { return L + pw * (idx + (120 - data.length)) / 119; };
      // rejilla
      c.font = '10px system-ui,sans-serif';
      c.lineWidth = 1;
      c.textBaseline = 'middle'; c.textAlign = 'right';
      for (var v = lo; v <= hi + 1e-9; v += step) {
        var y = Math.round(Y(v)) + 0.5;
        c.strokeStyle = '#262a33'; c.beginPath(); c.moveTo(L, y); c.lineTo(L + pw, y); c.stroke();
        c.fillStyle = '#8a8f98';
        c.fillText(key === 'rpm' && v >= 1000 ? (v / 1000) + 'k' : String(+v.toFixed(1)), L - 5, y);
      }
      c.textBaseline = 'alphabetic'; c.textAlign = 'center';
      [-120, -90, -60, -30, 0].forEach(function (s) {
        var x = L + pw * (s + 120) / 120;
        c.textAlign = s === -120 ? 'left' : s === 0 ? 'right' : 'center';
        c.fillText(s === 0 ? 'ahora' : s + ' s', x, h - 3);
      });
      if (data.length < 2) return;
      var col = cs.getPropertyValue(cfg.col).trim() || '#00c2ff';
      c.beginPath();
      for (i = 0; i < data.length; i++) {
        var px = X(i), py = Y(clamp(data[i], lo, hi));
        if (i) c.lineTo(px, py); else c.moveTo(px, py);
      }
      c.strokeStyle = col; c.lineWidth = 2; c.lineJoin = 'round'; c.stroke();
      c.lineTo(X(data.length - 1), T + ph); c.lineTo(X(0), T + ph); c.closePath();
      var gr = c.createLinearGradient(0, T, 0, T + ph);
      gr.addColorStop(0, col + '40'); gr.addColorStop(1, col + '00');
      c.fillStyle = gr; c.fill();
    });
  }

  /* ---------------- navegación ---------------- */
  function show(p) {
    if (PAGES.indexOf(p) < 0) p = 'tablero';
    S.page = p;
    qa('.tabs button').forEach(function (b) { b.setAttribute('aria-selected', b.getAttribute('data-p') === p); });
    PAGES.forEach(function (q) { $('p-' + q).hidden = q !== p; });
    if (location.hash.slice(1) !== p) history.replaceState(null, '', '#' + p);
    drawCharts();
  }
  qa('.tabs button').forEach(function (b) {
    b.addEventListener('click', function () { show(b.getAttribute('data-p')); });
  });
  window.addEventListener('hashchange', function () { show(location.hash.slice(1)); });

  /* ---------------- conexión ---------------- */
  var ws = null, retry = 1000, lastMsg = 0, pollTimer = null;
  function setLink(l) { if (S.link !== l) { S.link = l; renderLink(); } }

  function onMsg(m) {
    var d = m.d;
    switch (m.t) {
      case 'state': S.st = d; renderState(); break;
      case 'frames': renderFrames(d); break;
      case 'settings': S.set = d; renderSettings(); break;
      case 'sys': S.sys = d; renderSys(); break;
      case 'history':
        S.hist = { speed: (d.speed || []).slice(), rpm: (d.rpm || []).slice(), temp: (d.temp || []).slice() };
        drawCharts(); break;
      case 'sample':
        ['speed', 'rpm', 'temp'].forEach(function (k) {
          var a = S.hist[k]; a.push(+d[k] || 0); if (a.length > 120) a.splice(0, a.length - 120);
        });
        drawCharts(); break;
    }
  }

  function connect() {
    var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
    try { ws = new WebSocket(proto + location.host + '/ws'); } catch (e) { return schedule(); }
    ws.onopen = function () { retry = 1000; lastMsg = Date.now(); stopPoll(); setLink('ok'); };
    ws.onmessage = function (ev) {
      lastMsg = Date.now();
      var m; try { m = JSON.parse(ev.data); } catch (e) { return; }
      if (m && m.t) onMsg(m);
    };
    ws.onclose = function () { ws = null; startPoll(); schedule(); };
    ws.onerror = function () { try { ws.close(); } catch (e) { /* nada */ } };
  }
  function schedule() {
    setTimeout(connect, retry);
    retry = Math.min(retry * 1.5, 5000);
  }
  // Vigilancia: el servidor envía state a 10 Hz; si callamos 4 s, la conexión está muerta.
  setInterval(function () {
    if (ws && ws.readyState === 1 && Date.now() - lastMsg > 4000) ws.close();
  }, 1000);

  function getJSON(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) { if (!r.ok) throw r; return r.json(); });
  }
  function poll() {
    getJSON('/api/state').then(function (d) {
      if (S.link === 'ok') return;
      S.st = d; setLink('poll'); renderState();
      return getJSON('/api/settings').then(function (s) { S.set = s; renderSettings(); });
    }).catch(function () { if (S.link !== 'ok') setLink('off'); });
  }
  function startPoll() {
    if (S.link === 'ok') setLink('off');
    if (!pollTimer) { poll(); pollTimer = setInterval(poll, 2000); }
  }
  function stopPoll() { clearInterval(pollTimer); pollTimer = null; }

  function send(o) {
    if (ws && ws.readyState === 1) { ws.send(JSON.stringify(o)); return; }
    if (o.t === 'set') {   // sin WebSocket: usar la API HTTP
      var body = {}; body[o.k] = o.v;
      fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
        .then(function (r) { return r.json(); })
        .then(function (s) { if (s && s.units) { S.set = s; renderSettings(); } })
        .catch(function () { /* se reintentará al reconectar */ });
    }
  }

  /* ---------------- controles ---------------- */
  $('btnPause').addEventListener('click', function () {
    send({ t: 'cmd', c: S.st && S.st.paused ? 'resume' : 'pause' });
  });
  $('btnClear').addEventListener('click', function () { send({ t: 'cmd', c: 'clear' }); });
  qa('.seg').forEach(function (seg) {
    var k = seg.getAttribute('data-k');
    qa('button', seg).forEach(function (b) {
      b.addEventListener('click', function () {
        var v = b.getAttribute('data-v');
        if (k === 'bitrate') v = +v;
        if (S.set[k] === v) return;
        S.set[k] = v; renderSettings();             // respuesta inmediata; el servidor confirma
        send({ t: 'set', k: k, v: v });
      });
    });
  });
  qa('.sw').forEach(function (b) {
    b.addEventListener('click', function () {
      var k = b.getAttribute('data-k'), v = !S.set[k];
      S.set[k] = v; renderSettings();
      send({ t: 'set', k: k, v: v });
    });
  });

  /* ---------------- arranque ---------------- */
  window.addEventListener('resize', fit);
  show(location.hash.slice(1) || 'tablero');
  fit();
  renderLink();
  connect();
})();
