/* Mist Control — phone web app.
   The phone does all the thinking: it reads its own sensors, turns each into a
   0-100 mist level, and sends compact commands to the mist makers through the
   Cloudflare relay. Makers just obey + report status back.

   Staying on Cloudflare's free tier: we cap sends at SEND_HZ and only transmit
   when a value actually changes (send-on-change). A steady hand sends nothing. */

// ───────────────────────── config ─────────────────────────
const SEND_HZ      = 8;     // max commands/sec to the relay (mist reacts slower than this anyway)
const DELTA        = 2;     // only send if a level moved at least this many %
const KEEPALIVE_MS = 2000;  // …but re-send unchanged values this often so a maker can't drift
const MIN_INTERVAL = 1000 / SEND_HZ;
const MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/1/face_landmarker.task";
const VISION_CDN = "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.20";

// ───────────────────────── state ─────────────────────────
let ws, room, running = false, reconnectTimer = 0, modeGen = 0, audioCtx = null;
let source = null;          // active input: null (manual) or a sensor object
let energy = 0;             // current 0-100 from the active source
let bands = null;           // audio mode: per-band 0-100 array
let route = "sync";         // sync | phase | spectrum
const makers = [];          // [{id,name,level,current_ma,water,rssi,seen}]
const seen = new Map();     // send-on-change memory: key -> last value sent
let lastSendAt = 0, lastKeepAlive = 0, waveT = 0;

// ───────────────────────── dom ─────────────────────────
const $ = (id) => document.getElementById(id);
const connEl=$("conn"), connTxt=$("connTxt"), roomName=$("roomName");
const levelBig=$("levelBig"), targetSel=$("target"), engage=$("engage");
const manual=$("manual"), manualVal=$("manualVal");
const modesEl=$("modes"), modePill=$("modePill"), sensorEl=$("sensor");
const meterFill=$("meterFill"), meterTxt=$("meterTxt"), cam=$("cam"), bandsCv=$("bands");
const gain=$("gain"), invertRow=$("invertRow"), invertBtn=$("invert"), invLbl=$("invLbl"), sensorHint=$("sensorHint");
const routeEl=$("route"), phaseCtl=$("phaseCtl"), spread=$("spread");
const devsEl=$("devs"), devCount=$("devCount"), puffs=$("puffs");

// ───────────────────────── connection ─────────────────────────
function connect() {
  clearTimeout(reconnectTimer);
  if (ws) { ws.onclose = ws.onerror = null; try { ws.close(); } catch {} }  // drop any old socket cleanly
  room = new URL(location).searchParams.get("room")
       || localStorage.getItem("mm_room") || "workshop";
  roomName.textContent = room;
  const url = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws?room=${encodeURIComponent(room)}&role=phone&name=Phone`;
  setConn("…", "connecting…");
  ws = new WebSocket(url);
  ws.onopen    = () => setConn("ok", "connected");
  ws.onclose   = () => { setConn("off", "reconnecting…"); reconnectTimer = setTimeout(connect, 2000); };
  ws.onerror   = () => { try { ws.close(); } catch {} };
  ws.onmessage = (e) => onMessage(e.data);
}

// One shared AudioContext for mic + music modes. iOS only unlocks audio inside a
// user gesture, so we create/resume it on the tap (and on any later gesture).
function getAudioCtx() {
  if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  if (audioCtx.state === "suspended") audioCtx.resume();
  return audioCtx;
}
["pointerdown", "touchend"].forEach(ev => addEventListener(ev,
  () => { if (audioCtx && audioCtx.state === "suspended") audioCtx.resume(); }, { passive: true }));
function setConn(cls, txt) { connEl.className = "conn " + cls; connTxt.textContent = txt; }

function onMessage(raw) {
  let m; try { m = JSON.parse(raw); } catch { return; }
  if (m.t === "roster")      setRoster(m.devices || []);
  else if (m.t === "status") updateMaker(m);
}

// ───────────────────────── send layer (throttle + on-change) ─────────────────────────
function wsSend(obj) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); }

// returns true and remembers it if `val` for `key` is worth sending
function isNew(key, val) {
  const p = seen.get(key);
  return p === undefined || Math.abs(p - val) >= DELTA;
}

function dispatch() {
  if (!ws || ws.readyState !== 1) return;             // don't book-keep sends that can't leave
  const now = performance.now();
  if (now - lastSendAt < MIN_INTERVAL) return;
  const force = now - lastKeepAlive > KEEPALIVE_MS;   // periodic anti-drift resend

  if (route === "sync" || makers.length < 2 || (route === "spectrum" && !bands)) {
    const lvl = Math.round(energy), tgt = targetSel.value;
    const key = "s:" + tgt;
    if (force || isNew(key, lvl)) { wsSend({ t: "set", target: tgt, level: lvl }); seen.set(key, lvl); lastSendAt = now; }
  } else {
    const levels = {};
    if (route === "spectrum") {   // bands present here (the null case fell into sync above)
      makers.forEach((d, i) => levels[d.id] = Math.round(bands[i % bands.length] || 0));
    } else { // phase: a traveling wave scaled by the input energy
      const sp = (spread.value / 100) * Math.PI;
      makers.forEach((d, i) => levels[d.id] = Math.round(energy * (0.5 + 0.5 * Math.sin(waveT - i * sp))));
    }
    let any = force;
    for (const id in levels) if (isNew("m:" + id, levels[id])) any = true;
    if (any) { wsSend({ t: "multi", levels }); for (const id in levels) seen.set("m:" + id, levels[id]); lastSendAt = now; }
  }
  if (force) lastKeepAlive = now;
}

function stopAll() { wsSend({ t: "set", target: "all", level: 0 }); seen.clear(); }

// ───────────────────────── makers (roster + live status) ─────────────────────────
function setRoster(list) {
  // keep known live status, add/remove to match the roster
  const ids = new Set(list.map(d => d.id));
  for (let i = makers.length - 1; i >= 0; i--) if (!ids.has(makers[i].id)) makers.splice(i, 1);
  for (const d of list) if (!makers.find(m => m.id === d.id)) makers.push({ ...d, level: 0, current_ma: 0, water: "?", rssi: 0 });
  applyOrder();                                // restore the saved wave/spectrum order (by id)
  seen.clear();                                // forget send-history of any departed makers
  renderTargets(); renderDevs(); routeEl.hidden = makers.length < 2;
}

// ── maker order (persisted by id) — this IS the wave/spectrum sequence ──
function applyOrder() {
  let order; try { order = JSON.parse(localStorage.getItem("mm_order") || "[]"); } catch { order = []; }
  const idx = (id) => { const k = order.indexOf(id); return k < 0 ? 1e9 : k; };
  makers.sort((a, b) => idx(a.id) - idx(b.id));  // stable: unsaved ids keep roster order, at the end
}
function moveMaker(id, dir) {                     // dir: -1 earlier, +1 later
  const i = makers.findIndex(m => m.id === id), j = i + dir;
  if (i < 0 || j < 0 || j >= makers.length) return;
  [makers[i], makers[j]] = [makers[j], makers[i]];
  localStorage.setItem("mm_order", JSON.stringify(makers.map(m => m.id)));
  seen.clear();                                  // order changed → resend wave/spectrum levels
  renderTargets(); renderDevs();
}
devsEl.onclick = (e) => { const b = e.target.closest(".mv"); if (b && !b.disabled) moveMaker(b.dataset.id, b.dataset.mv === "up" ? -1 : 1); };
function updateMaker(m) {
  const d = makers.find(x => x.id === m.id);
  if (!d) return;
  // full re-render only when something structural changes (name / water badge);
  // pure telemetry (level/current/rssi) patches in place so reorder taps and the
  // bar animation survive the 1 Hz updates, and the DOM doesn't churn.
  const structural = (m.name != null && m.name !== d.name) || (m.water != null && m.water !== d.water);
  Object.assign(d, m); d.seen = performance.now();
  if (structural) renderDevs(); else patchDev(d);
}
function patchDev(d) {
  const li = [...devsEl.children].find(el => el.dataset && el.dataset.id === d.id);
  if (!li) return renderDevs();                   // tile not built yet → full render
  const lvl = clamp(+d.level || 0);
  const bar = li.querySelector(".dev-bar i"); if (bar) bar.style.width = lvl + "%";
  const set = (sel, txt) => { const el = li.querySelector(sel); if (el) el.textContent = txt; };
  set(".m-lvl", lvl + "%");
  set(".m-ma", Math.round(+d.current_ma || 0) + " mA");
  if (Number.isFinite(+d.rssi) && d.rssi) set(".m-sig", `📶 ${(+d.rssi).toFixed(0)} dBm`);
}
// Devices announce their own name/water, and anyone can join the room as a
// "device" — so treat those strings as untrusted and escape before innerHTML.
const esc = (s) => String(s).replace(/[&<>"']/g, c => (
  { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

function renderTargets() {
  const cur = targetSel.value;
  targetSel.innerHTML = '<option value="all">every maker</option>'
    + makers.map(d => `<option value="${esc(d.id)}">${esc(d.name)}</option>`).join("");
  if ([...targetSel.options].some(o => o.value === cur)) targetSel.value = cur;
}
// water/disc status from the maker's own current-sense → a friendly chip + tile state
const WATER = {
  ok:           { cls: "ok",   label: "💧 ok" },
  low:          { cls: "warn", label: "💧 low" },
  no_disc:      { cls: "bad",  label: "⚠ no disc" },
  disconnected: { cls: "bad",  label: "⚠ disc loose" },
};
function renderDevs() {
  devCount.textContent = makers.length;
  if (!makers.length) { devsEl.innerHTML = `<li class="empty">No makers in &ldquo;${esc(room)}&rdquo; yet. Power one on, or tap <b>➕ Add a maker</b> below to bring one in by its two words ☁</li>`; renderAlert(0); return; }
  let attention = 0;
  const many = makers.length >= 2;                     // order/reorder only matters with 2+
  devsEl.innerHTML = makers.map((d, i) => {
    const lvl = clamp(+d.level || 0);
    const w = WATER[d.water];                          // undefined while unknown ("?")
    if (w && w.cls !== "ok") attention++;
    const ring = w && w.cls !== "ok" ? " " + w.cls : "";        // .dev.warn / .dev.bad highlight
    const badge = w ? `<span class="badge ${w.cls}">${w.label}</span>` : "";
    const sig = Number.isFinite(+d.rssi) && d.rssi ? `<span class="m-sig">📶 ${(+d.rssi).toFixed(0)} dBm</span>` : "";
    const pos = many ? `<span class="pos" title="Wave / spectrum order">${i + 1}</span>` : "";
    const mv = many ? `<span class="mv-grp">`
        + `<button class="mv" data-mv="up" data-id="${esc(d.id)}" aria-label="Move earlier"${i === 0 ? " disabled" : ""}>↑</button>`
        + `<button class="mv" data-mv="down" data-id="${esc(d.id)}" aria-label="Move later"${i === makers.length - 1 ? " disabled" : ""}>↓</button></span>` : "";
    return `<li class="dev${ring}" data-id="${esc(d.id)}"><div class="dev-top">${pos}<span class="dev-name">${esc(d.name)}</span>${mv}</div>
      <div class="dev-bar"><i style="width:${lvl}%"></i></div>
      <div class="dev-meta"><span class="m-lvl">${lvl}%</span><span class="m-ma">${Math.round(+d.current_ma || 0)} mA</span>${sig}${badge}</div></li>`;
  }).join("");
  renderAlert(attention);
}
function renderAlert(n) {                              // one-line summary so a dry maker stands out
  const al = $("makerAlert"); if (!al) return;
  if (n > 0) { al.textContent = `⚠ ${n} maker${n > 1 ? "s" : ""} need${n > 1 ? "" : "s"} attention — check water or disc.`; al.hidden = false; }
  else al.hidden = true;
}

// ───────────────────────── input sources ─────────────────────────
// Each source sets `energy` (0-100) and optionally `bands`. read() runs per frame.
const G = () => gain.value / 50;               // sensitivity 0.02..2, 1 = neutral
let inverted = false;

// a little segmented control (reuses the .route/.rbtn look) shown under a mode —
// used by Light (camera), Move (lean/shake) and Face (expression)
function segPicker(items, current, onPick) {
  const wrap = document.createElement("div");
  wrap.className = "route"; wrap.style.marginTop = "12px";
  wrap.style.gridTemplateColumns = `repeat(${items.length}, 1fr)`;
  items.forEach(([key, label]) => {
    const b = document.createElement("button");
    b.className = "rbtn" + (key === current ? " on" : ""); b.textContent = label;
    b.onclick = () => { onPick(key); [...wrap.children].forEach(c => c.classList.remove("on")); b.classList.add("on"); };
    wrap.appendChild(b);
  });
  sensorHint.before(wrap);
  return wrap;
}

const Manual = {
  name: "slider",
  start() { energy = +manual.value; },
  read() {},
  stop() {},
};

function micAnalyser(fftSize) {
  return navigator.mediaDevices.getUserMedia({ audio: true }).then(stream => {
    const ctx = getAudioCtx();                 // shared, unlocked on the tap gesture (iOS)
    const an = ctx.createAnalyser(); an.fftSize = fftSize;
    ctx.createMediaStreamSource(stream).connect(an);
    return { an, stream };                      // no ctx → teardown keeps the shared ctx alive
  });
}

const Mic = {
  name: "mic", hint: "Blow or make noise — louder = more mist.",
  async start() { this.a = await micAnalyser(1024); this.buf = new Uint8Array(this.a.an.fftSize); },
  read() {
    if (!this.a) return;
    this.a.an.getByteTimeDomainData(this.buf);
    let s = 0; for (const v of this.buf) { const x = (v - 128) / 128; s += x * x; }
    const rms = Math.sqrt(s / this.buf.length);          // 0..~1
    energy = clamp(rms * 320 * G());
  },
  stop() { teardown(this.a); this.a = null; },
};

const Light = {
  name: "light", invert: true, invLabel: "cover = more", facing: "environment",
  hint: "Point at a light — brighter = more mist.",
  async start() {
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: this.facing } });
    cam.srcObject = this.stream; await cam.play();
    this.cv = document.createElement("canvas"); this.cv.width = this.cv.height = 24;
    this.cx = this.cv.getContext("2d", { willReadFrequently: true });
    this.picker = segPicker([["environment", "📷 Back"], ["user", "🤳 Front"]], this.facing, k => this.setFacing(k));
  },
  async setFacing(f) {                              // swap front/back camera live (atomic)
    if (f === this.facing && this.stream) return;
    let next;
    try {
      next = await navigator.mediaDevices.getUserMedia({ video: { facingMode: f } });
      cam.srcObject = next; await cam.play();
    } catch {
      if (next) next.getTracks().forEach(t => t.stop());   // discard the new one…
      if (this.stream) cam.srcObject = this.stream;        // …and keep showing the working camera
      sensorHint.textContent = "⚠ Couldn't switch camera."; return;
    }
    if (this.stream) this.stream.getTracks().forEach(t => t.stop());   // retire old only on success
    this.stream = next; this.facing = f;
  },
  read() {
    if (!this.cx || cam.readyState < 2) return;
    this.cx.drawImage(cam, 0, 0, 24, 24);
    const px = this.cx.getImageData(0, 0, 24, 24).data;
    let lum = 0; for (let i = 0; i < px.length; i += 4) lum += 0.299*px[i] + 0.587*px[i+1] + 0.114*px[i+2];
    let b = clamp((lum / (px.length / 4)) / 255 * 100 * G());
    energy = inverted ? 100 - b : b;
  },
  stop() { teardown({ stream: this.stream }); this.stream = null; this.picker?.remove(); this.picker = null; },
};

const Motion = {
  name: "motion", invert: true, invLabel: "flip it", swing: "lean",
  hint: "Tilt left or right — more tilt, more mist.",
  async start() {
    // one tap grants iOS motion (covers both orientation + acceleration)
    const askM = typeof DeviceMotionEvent !== "undefined" && DeviceMotionEvent.requestPermission;
    const askO = typeof DeviceOrientationEvent !== "undefined" && DeviceOrientationEvent.requestPermission;
    if (askM) { if (await DeviceMotionEvent.requestPermission() !== "granted") throw new Error("Motion access was denied."); }
    else if (askO) { if (await DeviceOrientationEvent.requestPermission() !== "granted") throw new Error("Motion access was denied."); }
    this.g = 0; this.shake = 0; this.lastMag = null;
    this.ho = (e) => { this.g = e.gamma || 0; };                    // -90..90 lean (left/right)
    this.hm = (e) => {                                              // shake = frame-to-frame jerk
      const a = e.accelerationIncludingGravity || e.acceleration || {};
      const mag = Math.hypot(a.x || 0, a.y || 0, a.z || 0);
      if (this.lastMag !== null) this.shake = Math.min(120, this.shake + Math.abs(mag - this.lastMag) * 5);
      this.lastMag = mag;
    };
    window.addEventListener("deviceorientation", this.ho);
    window.addEventListener("devicemotion", this.hm);
    this.picker = segPicker([["lean", "↔ Lean"], ["shake", "🤳 Shake"]], this.swing, k => {
      this.swing = k; invertRow.hidden = (k === "shake");          // "flip it" is lean-only
      sensorHint.textContent = k === "shake" ? "Shake your phone — harder = more mist." : this.hint;
    });
  },
  read() {
    if (this.swing === "shake") { energy = clamp(this.shake * G()); this.shake *= 0.88; } // decays when you stop
    else { let v = clamp(Math.abs(this.g || 0) / 60 * 100 * G()); energy = inverted ? 100 - v : v; }
  },
  stop() {
    if (this.ho) window.removeEventListener("deviceorientation", this.ho);
    if (this.hm) window.removeEventListener("devicemotion", this.hm);
    this.picker?.remove(); this.picker = null;
  },
};

const Face = {
  name: "face", hint: "Open your mouth — wider = more mist.",
  expr: "mouth",
  async start() {
    sensorHint.textContent = "loading face tracking…";
    const vision = await import(VISION_CDN);
    const fileset = await vision.FilesetResolver.forVisionTasks(VISION_CDN + "/wasm");
    this.fl = await vision.FaceLandmarker.createFromOptions(fileset, {
      baseOptions: { modelAssetPath: MODEL_URL },
      outputFaceBlendshapes: true, runningMode: "VIDEO", numFaces: 1,
    });
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "user" } });
    cam.srcObject = this.stream; await cam.play();
    this.picker = segPicker([["mouth", "😮 Mouth"], ["brows", "🤨 Brows"], ["blink", "😉 Blink"]], this.expr, k => this.expr = k);
    sensorHint.textContent = this.hint;
  },
  read() {
    if (!this.fl || cam.readyState < 2) return;
    const res = this.fl.detectForVideo(cam, performance.now());
    const cat = res.faceBlendshapes?.[0]?.categories;
    if (!cat) { energy = 0; return; }
    const get = (n) => cat.find(c => c.categoryName === n)?.score || 0;
    let v = this.expr === "mouth" ? get("jawOpen")
          : this.expr === "brows" ? (get("browOuterUpLeft") + get("browOuterUpRight") + get("browInnerUp")) / 3
          : (get("eyeBlinkLeft") + get("eyeBlinkRight")) / 2;       // blink
    energy = clamp(v * 130 * G());
  },
  stop() { teardown({ stream: this.stream }); this.fl?.close?.(); this.fl = null; this.picker?.remove(); this.picker = null; },
};

const Audio = {
  name: "audio", hint: "Play a song — bass, vocals & highs each drive a different maker.",
  async start() {
    this.a = await micAnalyser(2048);                   // finer low-end so bass/beat resolves
    this.a.an.smoothingTimeConstant = 0.6;              // snappier than the 0.8 default (more beat)
    this.buf = new Uint8Array(this.a.an.frequencyBinCount);
    this.peak = null;                                   // per-band decaying peak (built on first read)
    bandsCv.hidden = false; this.ctx2d = bandsCv.getContext("2d");
    bandsCv.width = (bandsCv.clientWidth || 320) * devicePixelRatio; bandsCv.height = 70 * devicePixelRatio;
    if (makers.length >= 2) { this.prevRoute = route; setRoute("spectrum"); }
  },
  read() {
    if (!this.a) return;
    const an = this.a.an;
    an.getByteFrequencyData(this.buf);
    const N = Math.max(1, Math.min(makers.length || 3, 16));
    const binHz = (audioCtx ? audioCtx.sampleRate : 44100) / an.fftSize;
    const fLo = 40, fHi = Math.min(16000, binHz * (this.buf.length - 1));    // musical range
    if (!this.peak || this.peak.length !== N) this.peak = new Array(N).fill(200);  // high = fade in, no blast
    const out = new Array(N);
    let a = Math.max(1, Math.round(fLo / binHz));        // first band's start bin
    for (let i = 0; i < N; i++) {
      // LOG-spaced bands (music + hearing are logarithmic): maker 0 = bass/beat,
      // middle = vocals, last = highs — scales to any N. Each band starts where
      // the previous ended, so adjacent bands never double-count a bin.
      const f1 = fLo * Math.pow(fHi / fLo, (i + 1) / N);
      const b = Math.min(this.buf.length, Math.max(a + 1, Math.round(f1 / binHz)));
      let raw = 0;
      if (a < b) { let s = 0; for (let j = a; j < b; j++) s += this.buf[j]; raw = s / (b - a); }
      // per-band auto-gain: scale each band to its OWN recent peak, so a quiet
      // treble band still drives its maker fully (fixes "the last one never mists").
      this.peak[i] = Math.max(raw, this.peak[i] * 0.992);
      const ref = Math.max(this.peak[i], 14);            // floor so silence/noise isn't amplified
      out[i] = clamp(raw < 6 ? 0 : (raw / ref) * 100 * G());   // gate near-silence
      a = b;                                             // next band continues from here
    }
    bands = out;
    energy = out.reduce((p, c) => p + c, 0) / N;
    drawBands(this.ctx2d, out);
  },
  stop() { teardown(this.a); this.a = null; bands = null; bandsCv.hidden = true;
           if (this.prevRoute) { setRoute(this.prevRoute); this.prevRoute = null; } },
};

const SOURCES = { mic: Mic, light: Light, motion: Motion, face: Face, audio: Audio };

function teardown(a) { try { a?.stream?.getTracks().forEach(t => t.stop()); a?.ctx?.close?.(); } catch {} }
function clamp(v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

function drawBands(cx, arr) {
  const W = bandsCv.width, H = bandsCv.height, n = arr.length, bw = W / n;
  cx.clearRect(0, 0, W, H);
  arr.forEach((v, i) => {
    const h = (v / 100) * H;
    cx.fillStyle = i % 2 ? "#2596be" : "#ffc831";
    cx.fillRect(i * bw + 2, H - h, bw - 4, h);
  });
}

// ───────────────────────── mode switching ─────────────────────────
async function selectMode(key) {
  const tile = [...modesEl.children].find(b => b.dataset.mode === key);
  if (tile && tile.classList.contains("dim")) {        // capability missing — explain, don't fail blank
    ++modeGen; setManual();                            // cancel any in-flight start, drop to the slider…
    sensorEl.hidden = false;                           // …but keep the panel up to show why
    sensorHint.textContent = "⚠ " + (tile.dataset.why || "Not available here.");
    return;
  }
  const next = SOURCES[key];
  const myGen = ++modeGen;                            // cancel-token first: also cancels an in-flight
  if (source === next) return setManual();           // start() when toggling the active mode back off
  if (key === "mic" || key === "audio") getAudioCtx(); // unlock audio inside this tap (iOS)
  if (source) source.stop();
  source = next;                                      // tick reads it; each read() self-guards until ready
  // reset per-mode UI
  invertRow.hidden = true; bandsCv.hidden = true; sensorHint.textContent = "";
  inverted = false; invertBtn.setAttribute("aria-checked", "false");
  sensorEl.hidden = false;
  [...modesEl.children].forEach(b => b.classList.toggle("on", b.dataset.mode === key));
  modePill.textContent = next.name;
  if (next.invert) { invertRow.hidden = false; invLbl.textContent = next.invLabel || "invert"; }
  try {
    await next.start();
    if (myGen !== modeGen) { next.stop(); return; }   // a newer mode took over mid-start → clean up
    sensorHint.textContent = next.hint || "";
    running = true; setEngage();                      // auto-start so it "just works"
  } catch (err) {
    if (myGen !== modeGen) return;                     // superseded; the newer call owns the UI
    source = null; energy = +manual.value;             // fall back to the slider, but…
    [...modesEl.children].forEach(b => b.classList.remove("on"));
    modePill.textContent = "slider"; invertRow.hidden = true;   // clear any half-shown mode UI
    sensorEl.hidden = false;                            // …keep the panel up to SHOW why it failed
    sensorHint.textContent = "⚠ " + (err.message || "Couldn't start that sensor — check the permission prompt.");
  }
}
function setManual() {
  if (source) source.stop();
  source = null; sensorEl.hidden = true;
  [...modesEl.children].forEach(b => b.classList.remove("on"));
  modePill.textContent = "slider"; energy = +manual.value;
}

// ───────────────────────── ui wiring ─────────────────────────
manual.oninput = () => { manualVal.textContent = manual.value + "%"; setFill(manual); if (!source) energy = +manual.value; };
gain.oninput   = () => setFill(gain);
spread.oninput = () => setFill(spread);
invertBtn.onclick = () => { inverted = !inverted; invertBtn.setAttribute("aria-checked", inverted); };
modesEl.onclick = (e) => { const b = e.target.closest(".mode"); if (b) selectMode(b.dataset.mode); };

engage.onclick = () => { running = !running; if (!running) stopAll(); setEngage(); };
function setEngage() {
  engage.textContent = running ? "Stop misting" : "Start misting";
  engage.classList.toggle("on", running);
}

routeEl.onclick = (e) => { const b = e.target.closest(".rbtn"); if (b) setRoute(b.dataset.route); };
function setRoute(r) {
  route = r;
  [...routeEl.children].forEach(b => b.classList.toggle("on", b.dataset.route === r));
  phaseCtl.hidden = r !== "phase"; seen.clear();
}

// room dialog — with a prompt() fallback for Safari < 15.4 (no <dialog>/showModal)
function changeRoom(v) {
  v = (v || "").trim(); if (!v || v === room) return;
  localStorage.setItem("mm_room", v);
  const u = new URL(location); u.searchParams.set("room", v); history.replaceState(0, "", u);
  connect();                                          // connect() cleanly drops the old socket + timer
}
const roomDlg = $("roomDlg");
$("roomBtn").onclick = () => {
  if (roomDlg && typeof roomDlg.showModal === "function") { $("roomInput").value = room; roomDlg.showModal(); }
  else changeRoom(prompt("Room name. Same name = shared control. Type your maker's two words to control just it.", room));
};
$("roomSave").onclick = () => changeRoom($("roomInput").value);

// ── add / claim a maker into THIS room by its printed two words ──
// We briefly join the board's private room (its words), then send a keyed
// {t:"room"} command telling it to hop into our current room. No reset/cable.
const addDlg = $("addDlg"), addInput = $("addInput"), addStatus = $("addStatus");
const normalizeRoom = (s) => (s || "").trim().toLowerCase().replace(/\s+/g, "-");
function setAddStatus(msg, ok) {        // ok: true | false | null (in progress)
  addStatus.innerHTML = (ok === false ? "⚠ " : ok === true ? "✓ " : "") + msg;
  addStatus.style.color = ok === false ? "#a23b3b" : ok === true ? "#1f7a4d" : "var(--ink-soft)";
}
function openAdd() {
  if (addDlg && typeof addDlg.showModal === "function") {
    addInput.value = ""; setAddStatus("", null); addDlg.showModal(); setTimeout(() => addInput.focus(), 50);
  } else {
    const w = prompt("Type the two words printed on the maker (e.g. fluffy-otter) to bring it into this room:");
    if (w) addMaker(w);
  }
}
let addAbort = null;                                   // closes an in-flight Add probe (Cancel / retry)
function addMaker(words) {
  if (addAbort) addAbort();                            // abort any previous in-flight attempt
  const board = normalizeRoom(words);
  if (!board) return;
  const dest = room;                                   // bring it into the room I'm in right now
  if (board === dest) return setAddStatus("That's already this room — type a maker's own two words.", false);
  setAddStatus(`Looking for &ldquo;${esc(board)}&rdquo;…`, null);
  let probe, done = false, sent = 0;
  const url = `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws?room=${encodeURIComponent(board)}&role=phone&name=Adder`;
  try { probe = new WebSocket(url); } catch { return setAddStatus("Couldn't reach the relay. Try again.", false); }
  const finish = (msg, ok) => {
    if (done) return; done = true; clearTimeout(to); addAbort = null; try { probe.close(); } catch {}
    setAddStatus(msg, ok);
    if (ok) setTimeout(() => { if (addDlg && addDlg.open) addDlg.close(); }, 1600);
  };
  const to = setTimeout(() => finish(`No maker &ldquo;${esc(board)}&rdquo; answered. Check the two words (printed on it) and that it's powered on.`, false), 7000);
  addAbort = () => { if (done) return; done = true; clearTimeout(to); addAbort = null; try { probe.close(); } catch {} };  // silent cancel
  probe.onmessage = (e) => {
    let m; try { m = JSON.parse(e.data); } catch { return; }
    if (m.t === "roster") {
      if (sent) return;                                 // send the move once
      const devs = (m.devices || []).filter((d, i, a) => a.findIndex(x => x.id === d.id) === i);
      if (!devs.length) return;                         // board not joined yet — wait until timeout
      devs.forEach(d => probe.send(JSON.stringify({ t: "room", id: d.id, key: board, room: dest })));
      sent = devs.length;
      setTimeout(() => finish(`Moved ${sent} maker${sent > 1 ? "s" : ""} into &ldquo;${esc(dest)}&rdquo; — appearing below ☁`, true), 800);
    }
  };
  probe.onerror = () => finish("Couldn't reach the relay. Try again.", false);
}
$("addBtn").onclick = openAdd;
$("addGo").onclick = () => addMaker(addInput.value);
$("addCancel").onclick = () => { if (addAbort) addAbort(); if (addDlg && addDlg.open) addDlg.close(); };
addInput.onkeydown = (e) => { if (e.key === "Enter") { e.preventDefault(); addMaker(addInput.value); } };

function setFill(el) { el.style.setProperty("--fill", ((el.value - el.min) / (el.max - el.min) * 100) + "%"); }

// ───────────────────────── browser capability check ─────────────────────────
// Sensor modes need a secure context + getUserMedia (camera/mic), and Tilt needs
// DeviceOrientation. Older iOS Safari (< 15.4) and in-app browsers miss some — so
// grey out what won't work and say why, instead of failing on a blank screen.
function iosVersion() {                                   // {major,minor} or null — compared as ints
  const m = navigator.userAgent.match(/OS (\d+)_(\d+)/i);
  return /iP(hone|ad|od)/.test(navigator.userAgent) && m ? { major: +m[1], minor: +m[2] } : null;
}
function checkCompat() {
  const hasCam = !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia);
  const hasMotion = typeof DeviceOrientationEvent !== "undefined";
  [...modesEl.children].forEach(b => {
    const mode = b.dataset.mode;                          // sensor inputs gate on getUserMedia / motion
    if (mode === "motion" ? !hasMotion : !hasCam) {
      b.classList.add("dim");
      b.dataset.why =
        mode === "motion" ? "Motion sensing isn't available on this device or browser."
        : (mode === "mic" || mode === "audio") ? "Mic access isn't available here — open this page in Safari or Chrome, not inside another app."
        : "Camera access isn't available here — open this page in Safari or Chrome, not inside another app.";
    }
  });
  const b = $("compat"); if (!b) return;
  const ios = iosVersion();
  let msg = "";
  if (!window.isSecureContext) msg = "⚠ Not a secure (https) connection — sensors are blocked here.";
  else if (!hasCam) msg = "📷 <b>Sensor modes need Safari or Chrome.</b> Opened from another app (Messages, Instagram…)? Tap ••• → <b>Open in Browser</b>. The slider below always works.";
  else if (ios && (ios.major < 15 || (ios.major === 15 && ios.minor < 4)))
    msg = "ℹ️ You're on iOS " + ios.major + "." + ios.minor + " — for the full experience, update to <b>iOS 15.4+</b> (Settings → General → Software Update). The controls still work.";
  if (msg) { b.innerHTML = msg; b.hidden = false; }
}

// ───────────────────────── main loop ─────────────────────────
function tick(now) {
  if (source && source.read) source.read();
  waveT += 0.06;
  const lvl = Math.round(energy);
  levelBig.textContent = lvl;
  meterFill.style.setProperty("--fill", lvl + "%"); meterTxt.textContent = lvl + "%";
  document.documentElement.style.setProperty("--live", (lvl / 100).toFixed(2));
  [...puffs.children].forEach((p, i) => {
    const on = lvl > i * 20;
    p.style.opacity = on ? 0.85 : 0.2;
    p.style.transform = on ? "translateY(-4px) scale(1.15)" : "none";
  });
  if (running) dispatch();
  requestAnimationFrame(tick);
}

// ───────────────────────── boot ─────────────────────────
setFill(manual); setFill(gain); setFill(spread);
manualVal.textContent = manual.value + "%";
energy = +manual.value;          // start matching the slider (default 30%)
checkCompat();                   // grey out unsupported sensors + show a friendly note
connect();
requestAnimationFrame(tick);
