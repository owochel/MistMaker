# Mist Control — phone-sensor web app

Drive the mist makers from a phone's **mic, light, motion, face, or music**, and
choreograph several at once — all from one cute web page. Built for workshops.

```
   📱 phone (this web app, on HTTPS)              ☁ Cloudflare              🌫 mist makers
   ┌───────────────────────────────┐        ┌──────────────────┐        ┌────────────────┐
   │ reads its OWN sensors,         │  wss   │  Worker + Durable │  wss   │ XIAO ESP32-C6  │
   │ turns them into a 0-100 level  │ ─────► │  Object = "room"  │ ─────► │ PhoneDemo      │
   │ (mic/light/face/FFT/...)       │        │  passes messages  │        │ .ino (thin)    │
   │ + shows every maker's status   │ ◄───── │  both directions  │ ◄───── │ reports status │
   └───────────────────────────────┘        └──────────────────┘        └────────────────┘
        does ALL the thinking                 free, no broker            just obeys + reports
```

**Why a cloud relay and not the board's own WiFi page?** Phone sensors (mic,
camera, motion) only work on a *secure* (HTTPS) page. A board on its own WiFi
serves plain HTTP, so iPhones flatly refuse sensor access there. Hosting this
page on Cloudflare gives every phone secure sensor access with **no certificate
warnings**. Mist reacts slowly (hundreds of ms), so cloud lag is invisible — and
every maker just joins the same "room," so the phone keeps them in sync with no
ESP-NOW channel fiddling.

---

## How the "room" works (it's just like MQTT)

If you've used MQTT: a **broker** sits in the middle, phones **publish** messages,
devices **subscribe**. This is the same idea, but the broker is a tiny
**Cloudflare Worker** (a Durable Object) instead of a separate service you have to
sign up for. Phones and makers open a WebSocket to the same room; the room
forwards every phone message to the makers and every maker's status back to the
phones. That's the whole thing — about 70 lines in `src/worker.js`.

One Worker does **both** jobs: serves this web app **and** runs the room. So you
deploy once and get one URL.

> **Access:** the room is open — anyone with your URL **and** the room name can
> join and drive the makers. That's fine for a public workshop; for a private
> session, change the room name (top-right ☁ chip) to something non-obvious. No
> data is stored and nothing is logged — the Worker only passes messages through.

---

## Part A — deploy the relay + app to Cloudflare (one time, ~10 min)

You need a free [Cloudflare account](https://dash.cloudflare.com/sign-up) and
[Node.js](https://nodejs.org) installed.

1. **Open a terminal in this folder** (`extras/phone-app`).

2. **Log in to Cloudflare** (opens your browser once):
   ```bash
   npx wrangler login
   ```

3. **Deploy:**
   ```bash
   npx wrangler deploy
   ```
   Wrangler prints a URL like `https://mistmaker-relay.YOURNAME.workers.dev`.
   **That URL is your app** — open it on your phone. Copy the host part
   (`mistmaker-relay.YOURNAME.workers.dev`) for the firmware in Part B.

   > Want a custom name? Edit `name = "..."` at the top of `wrangler.toml`.

That's it — no database, no broker, no secrets. Updating the app later is just
`npx wrangler deploy` again.

### Will this stay free? Yes — comfortably.

Cloudflare's free plan gives Durable Objects their **own 100,000 requests/day**,
and incoming WebSocket messages are billed at **20:1** (20 messages = 1 request),
so the real ceiling is **~2,000,000 messages/day**. Outgoing messages and serving
this web page are **free and unlimited**. To stay frugal the app:

- sends at most **8 messages/sec** per phone (mist can't react faster anyway), and
- uses **send-on-change** — a steady level sends nothing.

A 3-hour workshop with ~10 phones + ~6 makers uses a few percent of one day's
free budget. And on the free plan you **cannot be charged** — at the (very
distant) cap, the app just pauses until the daily reset (midnight UTC). The send
rate lives in `SEND_HZ` at the top of `public/app.js` if you ever want to tune it.

---

## Part B — flash the makers (`examples/PhoneDemo`)

Each mist maker runs the **same** sketch — no per-board code edits. Flash it once
per board, then set WiFi + room **on the device** (no hardcoded passwords).

1. In Arduino IDE, install the libraries **"WebSockets" by Markus Sattler** and
   **"ArduinoJson" by Benoit Blanchon** (Library Manager) — plus MistMaker, as usual.
2. Open `examples/PhoneDemo/PhoneDemo.ino`. Only edit the top if you deployed
   your own relay: set `RELAY_HOST` to your host (defaults to the hosted one).
3. Select **XIAO_ESP32C6** and upload. Repeat for each board — that's it.
4. **First-boot WiFi setup** (per board, once): the board makes its own WiFi
   `MistMaker-Setup-XXXX`. Join it — the setup page pops up (else open
   `http://192.168.4.1`). There you can tap **Test mist (30%)** to check the
   hardware, name the maker, set the **Room** (below), then pick the venue WiFi +
   password and **Save**. The board reboots and connects. To redo setup later,
   hold the button while powering on.

> **Rooms / privacy:** every board defaults to its **own unique two-word room**
> (e.g. `fluffy-otter`, shown on the setup page and on Serial), so a fresh board
> is private. For a **group show**, set every board's Room to one shared name like
> `workshop` and set the same room in the app's ☁ chip — or pull a private board
> into your room with the app's **➕ Add a maker by its words**.

> **Button:** press the board button any time — setup or connected, online or
> offline — to toggle a local 30% mist. Hold it at power-on to re-run WiFi setup.

> **Flash tip:** this sketch uses ~93% of the default flash partition. If you add
> to it and it no longer fits, set **Tools → Partition Scheme → "Huge APP (3MB)"**.

**Debugging a board:** open Serial Monitor at **115200**. Each board prints its
name, WiFi + relay status, every mist level change, and a status line each second:
```
==== Fluffy Otter  (id: A4F1, room: workshop, relay: mistcontrol.byproductlab.com) ====
[ID] this board is private room "fluffy-otter" — set room to a shared name like "workshop" for a group
[WiFi] joining "venue-wifi"... ok — ip 192.168.1.42, rssi -54 dBm
[WS] connected — control me from the app
[CMD] mist level -> 30%
[STAT] level=30%  current=148 mA  water=ok  wifi=relay-ok(-54 dBm)
```
The **same** status (level, current, water, signal) shows live in the app's
**Your makers** panel — so you can watch a board from USB or from your phone.

---

## Using it at the workshop

Open your Worker URL on a phone, then:

- **Slide it** — the always-there manual slider (starts at a gentle 30%).
- **Start misting** — master on/off. Pick *every maker* or one specific maker.
- **Play with it** — tap a sense mode; it asks permission, then just works:
  - **Mic** 🎤 — blow across the mic / make noise → more mist.
  - **Light** 💡 — point the camera at a lamp, or cover it (toggle "cover = more").
  - **Tilt** 📱 — lean the phone left/right.
  - **Face** 😮 — open your mouth (or switch to brows / blink).
  - **Music** 🎵 — play a song; the makers become a **spectrum** — one frequency
    band each (log-spaced 60 Hz–8 kHz, so bass gets real resolution). Bands
    auto-gain against their own rolling peaks, ride a fast-attack/slow-release
    envelope, and a bass beat detector adds a decaying "punch" on each kick — the
    mist pulses with the rhythm instead of tracking raw loudness. Band order
    follows device id, so each maker keeps its band across reconnects. Line them
    up for a vapor equalizer.
- **Your makers** — with 2+ makers you get routing:
  - **Together** — all in sync.
  - **Wave** — a ripple/delay across them (use *spread*) — an animation.
  - **Spectrum** — one audio band per maker (great with Music mode).
- **➕ Add a maker by its words** — type the two words printed on a maker
  (its private room, e.g. `fluffy-otter`) and it hops into your current room
  over the air — no reset, no cable.

### Sensor permission notes
- **iPhone**: the first tap on Mic / Light / Face / Tilt shows a permission prompt
  (camera, mic, or motion) — that's expected; tap allow. Motion needs that tap by
  Apple's rules, which is why modes activate on tap.
- Everything runs **on the phone** — no sensor data leaves the browser; only the
  final mist level (a single number) goes to the makers.

---

## Files

| File | What it is |
|---|---|
| `wrangler.toml` | Cloudflare config (the app + the room) |
| `src/worker.js` | The Worker + Durable Object "room" (the relay) |
| `public/index.html` · `app.css` · `app.js` | The phone web app (cute, vanilla, no build step) |

Pairs with `examples/PhoneDemo/PhoneDemo.ino` (the firmware every maker runs).
