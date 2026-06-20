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
  setConn("…", "saying hi…");
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
  seen.clear();                                // forget send-history of any departed makers
  renderTargets(); renderDevs(); routeEl.hidden = makers.length < 2;
}
function updateMaker(m) {
  const d = makers.find(x => x.id === m.id);
  if (!d) return;
  Object.assign(d, m); d.seen = performance.now();
  renderDevs();
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
function renderDevs() {
  devCount.textContent = makers.length;
  if (!makers.length) { devsEl.innerHTML = '<li class="empty">No makers yet — flash one &amp; power it on ☁</li>'; return; }
  devsEl.innerHTML = makers.map(d => {
    const lvl = clamp(+d.level || 0);
    const w = d.water === "ok" ? '<span class="badge ok">water ok</span>'
            : d.water === "low" ? '<span class="badge warn">water low</span>'
            : d.water === "?" ? "" : `<span class="badge bad">${esc(d.water)}</span>`;
    const sig = Number.isFinite(+d.rssi) && d.rssi ? `${(+d.rssi).toFixed(0)} dBm` : "";
    return `<li class="dev"><div class="dev-top"><span class="dev-name">${esc(d.name)}</span>
      <span class="dev-sig">📶 ${esc(sig)}</span></div>
      <div class="dev-bar"><i style="width:${lvl}%"></i></div>
      <div class="dev-meta"><span>${lvl}%</span><span>${Math.round(+d.current_ma || 0)} mA</span>${w}</div></li>`;
  }).join("");
}

// ───────────────────────── input sources ─────────────────────────
// Each source sets `energy` (0-100) and optionally `bands`. read() runs per frame.
const G = () => gain.value / 50;               // sensitivity 0.02..2, 1 = neutral
let inverted = false;

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
  name: "mic", hint: "Blow across the mic or make noise — louder = more mist.",
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
  name: "light", invert: true, invLabel: "cover = more",
  hint: "Point the camera at a lamp, or cover it with your hand.",
  async start() {
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" } });
    cam.srcObject = this.stream; await cam.play();
    this.cv = document.createElement("canvas"); this.cv.width = this.cv.height = 24;
    this.cx = this.cv.getContext("2d", { willReadFrequently: true });
  },
  read() {
    if (!this.cx || cam.readyState < 2) return;
    this.cx.drawImage(cam, 0, 0, 24, 24);
    const px = this.cx.getImageData(0, 0, 24, 24).data;
    let lum = 0; for (let i = 0; i < px.length; i += 4) lum += 0.299*px[i] + 0.587*px[i+1] + 0.114*px[i+2];
    let b = clamp((lum / (px.length / 4)) / 255 * 100 * G());
    energy = inverted ? 100 - b : b;
  },
  stop() { teardown({ stream: this.stream }); this.stream = null; },
};

const Motion = {
  name: "motion", invert: true, invLabel: "flip direction",
  hint: "Tilt the phone left/right. Flat = off, tilted = full.",
  async start() {
    if (typeof DeviceOrientationEvent !== "undefined" && DeviceOrientationEvent.requestPermission) {
      const r = await DeviceOrientationEvent.requestPermission();   // iOS: needs this tap
      if (r !== "granted") throw new Error("Motion access was denied.");
    }
    this.h = (e) => { this.g = e.gamma || 0; };                     // -90..90 left/right tilt
    window.addEventListener("deviceorientation", this.h);
  },
  read() {
    let v = clamp(Math.abs(this.g || 0) / 60 * 100 * G());
    energy = inverted ? 100 - v : v;
  },
  stop() { if (this.h) window.removeEventListener("deviceorientation", this.h); },
};

const Face = {
  name: "face", hint: "Open your mouth wide. (Tap ▸ for brows or blink.)",
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
    this.addExprPicker();
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
  addExprPicker() {
    if (this.picker) return;
    this.picker = document.createElement("div"); this.picker.className = "route"; this.picker.style.marginTop = "12px";
    [["mouth","😮 Mouth"],["brows","🤨 Brows"],["blink","😉 Blink"]].forEach(([k, label], i) => {
      const b = document.createElement("button"); b.className = "rbtn" + (i === 0 ? " on" : ""); b.textContent = label;
      b.onclick = () => { this.expr = k; [...this.picker.children].forEach(c => c.classList.remove("on")); b.classList.add("on"); };
      this.picker.appendChild(b);
    });
    sensorHint.before(this.picker);
  },
  stop() { teardown({ stream: this.stream }); this.fl?.close?.(); this.fl = null; this.picker?.remove(); this.picker = null; },
};

const Audio = {
  name: "audio", hint: "Play music near the phone — each maker becomes one frequency band.",
  async start() {
    this.a = await micAnalyser(256); this.buf = new Uint8Array(this.a.an.frequencyBinCount);
    bandsCv.hidden = false; this.ctx2d = bandsCv.getContext("2d");
    bandsCv.width = (bandsCv.clientWidth || 320) * devicePixelRatio; bandsCv.height = 70 * devicePixelRatio;
    if (makers.length >= 2) { this.prevRoute = route; setRoute("spectrum"); }
  },
  read() {
    if (!this.a) return;
    this.a.an.getByteFrequencyData(this.buf);
    const N = Math.max(1, Math.min(makers.length || 8, 16));
    const out = new Array(N).fill(0);
    const lo = 2, hi = this.buf.length;                  // skip DC bin
    for (let i = 0; i < N; i++) {
      const a = lo + Math.floor((hi - lo) * i / N), b = lo + Math.floor((hi - lo) * (i + 1) / N);
      let s = 0; for (let j = a; j < b; j++) s += this.buf[j];
      out[i] = clamp((s / Math.max(1, b - a)) / 255 * 140 * G());
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
    modePill.textContent = "slider";
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

// room dialog
$("roomBtn").onclick = () => { $("roomInput").value = room; $("roomDlg").showModal(); };
$("roomSave").onclick = (e) => {
  const v = $("roomInput").value.trim(); if (!v) return;
  localStorage.setItem("mm_room", v);
  const u = new URL(location); u.searchParams.set("room", v); history.replaceState(0, "", u);
  connect();                                          // connect() cleanly drops the old socket + timer
};

function setFill(el) { el.style.setProperty("--fill", ((el.value - el.min) / (el.max - el.min) * 100) + "%"); }

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
connect();
requestAnimationFrame(tick);
