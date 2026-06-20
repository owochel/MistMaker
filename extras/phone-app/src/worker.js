// Mist Maker relay — a Cloudflare Worker + Durable Object.
//
// Think of the Durable Object "Room" as a chat room that two kinds of clients
// join over a WebSocket:
//   * phones  — send commands  ({t:"set", target, level} / {t:"multi", levels})
//   * devices — the mist makers, which receive commands and report status
//
// Rule of the room: a phone's message is forwarded to every device; a device's
// message is forwarded to every phone. That's the whole relay. The phone does
// all the thinking (sensors, FFT, choreography); devices just obey + report.

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // /ws → join the WebSocket room. Everything else → the web app (static).
    if (url.pathname === "/ws") {
      if (request.headers.get("Upgrade") !== "websocket") {
        return new Response("expected websocket", { status: 426 });
      }
      const room = url.searchParams.get("room") || "workshop";
      const stub = env.ROOM.get(env.ROOM.idFromName(room));
      return stub.fetch(request);
    }
    return env.ASSETS.fetch(request);
  },
};

export class Room {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  // A client joins. We tag its socket with role/id/name and accept it with the
  // hibernation API so the room costs nothing while idle.
  async fetch(request) {
    const url = new URL(request.url);
    const role = url.searchParams.get("role") === "device" ? "device" : "phone";
    const id = (url.searchParams.get("id") || crypto.randomUUID().slice(0, 4)).slice(0, 12);
    const name = (url.searchParams.get("name") || (role === "device" ? "Mist" : "Phone")).slice(0, 32);

    const [client, server] = Object.values(new WebSocketPair());
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ role, id, name });

    this.broadcastRoster(); // let phones see the updated device list
    return new Response(null, { status: 101, webSocket: client });
  }

  meta(ws) {
    return ws.deserializeAttachment() || {};
  }

  peers(role) {
    return this.state.getWebSockets().filter((ws) => this.meta(ws).role === role);
  }

  // Phones need the live list of mist makers to map sensors → devices.
  broadcastRoster() {
    const devices = this.peers("device").map((ws) => {
      const m = this.meta(ws);
      return { id: m.id, name: m.name };
    });
    const msg = JSON.stringify({ t: "roster", devices });
    for (const ws of this.peers("phone")) this.t(ws, msg);
  }

  async webSocketMessage(ws, raw) {
    const from = this.meta(ws);
    let data;
    try { data = JSON.parse(raw); } catch { return; }

    if (from.role === "phone") {
      // command → every device (each device decides if it's the target)
      const out = JSON.stringify(data);
      for (const dev of this.peers("device")) this.t(dev, out);
    } else {
      // status → every phone, stamped with which maker it came from
      data.id = from.id;
      data.name = from.name;
      const out = JSON.stringify(data);
      for (const ph of this.peers("phone")) this.t(ph, out);
    }
  }

  async webSocketClose(ws) { this.broadcastRoster(); }
  async webSocketError(ws) { this.broadcastRoster(); }

  // send, ignoring sockets that have already gone away
  t(ws, msg) {
    try { ws.send(msg); } catch {}
  }
}
