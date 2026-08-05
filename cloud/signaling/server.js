#!/usr/bin/env node
/**
 * zero-mini signaling server
 *
 * Roles:
 *   master  = device (HiSilicon)
 *   viewer  = phone / browser
 *
 * Message schema (JSON text frames):
 *   { "type":"join", "room":"door-1", "role":"master"|"viewer", "clientId":"optional" }
 *   { "type":"offer"|"answer", "sdp":"...", "from":"...", "to":"..." }
 *   { "type":"candidate", "candidate":"...", "sdpMid":"0", "sdpMLineIndex":0, "from":"...", "to":"..." }
 *   { "type":"leave" }
 *   { "type":"ping" } / { "type":"pong" }
 *
 * Room policy: 1 master + N viewers (default max viewers = 3 for family).
 */

const http = require("http");
const fs = require("fs");
const path = require("path");
const { WebSocketServer } = require("ws");
const crypto = require("crypto");

const PUBLIC_DIR = path.join(__dirname, "public");
const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".ico": "image/x-icon",
};

function parsePositiveInteger(value, fallback, maximum = Number.MAX_SAFE_INTEGER) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > maximum) {
    return fallback;
  }
  return parsed;
}

const PORT = parsePositiveInteger(process.env.PORT, 8089, 65535);
const SIGNALING_WS_PATH = process.env.SIGNALING_PATH || "/ws";
const MAX_VIEWERS = parsePositiveInteger(process.env.MAX_VIEWERS, 3, 3);
const MAX_SIGNAL_MESSAGE_BYTES = parsePositiveInteger(
  process.env.MAX_SIGNAL_MESSAGE_BYTES,
  256 * 1024,
  4 * 1024 * 1024,
);
const TOKEN = process.env.SIGNALING_TOKEN || "";
const ENVIRONMENT = process.env.SIGNALING_ENV || "demo";
if (ENVIRONMENT === "production" && !TOKEN) {
  throw new Error("SIGNALING_TOKEN is required in production");
}

/** @typedef {{ id:string, role:string, room:string, ws:import('ws').WebSocket }} Peer */

/** @type {Map<string, { master?: Peer, viewers: Map<string, Peer> }>} */
const rooms = new Map();

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(obj));
  }
}

function safeTokenEqual(actual, expected) {
  if (typeof actual !== "string" || typeof expected !== "string") return false;
  const actualBuffer = Buffer.from(actual);
  const expectedBuffer = Buffer.from(expected);
  return actualBuffer.length === expectedBuffer.length &&
    crypto.timingSafeEqual(actualBuffer, expectedBuffer);
}

function authOk(req) {
  if (!TOKEN) return true;
  const url = new URL(req.url || "/", "http://localhost");
  const q = url.searchParams.get("token");
  const h = req.headers["x-signaling-token"];
  return safeTokenEqual(q, TOKEN) || safeTokenEqual(h, TOKEN);
}

function getRoom(name) {
  let room = rooms.get(name);
  if (!room) {
    room = { viewers: new Map() };
    rooms.set(name, room);
  }
  return room;
}

function purgePeer(peer) {
  if (!peer) return;
  const room = rooms.get(peer.room);
  if (!room) return;

  if (peer.role === "master" && room.master === peer) {
    room.master = undefined;
    for (const viewer of room.viewers.values()) {
      send(viewer.ws, { type: "peer-left", role: "master", clientId: peer.id });
    }
    log("master left", peer.room, peer.id);
  } else if (peer.role === "viewer" && room.viewers.get(peer.id) === peer) {
    room.viewers.delete(peer.id);
    if (room.master) {
      send(room.master.ws, {
        type: "peer-left",
        role: "viewer",
        clientId: peer.id,
      });
    }
    log("viewer left", peer.room, peer.id);
  }

  if (!room.master && room.viewers.size === 0) {
    rooms.delete(peer.room);
  }
}

function validIdentifier(value, maximumLength = 128) {
  return typeof value === "string" &&
    value.length > 0 && value.length <= maximumLength &&
    /^[A-Za-z0-9._:-]+$/.test(value);
}

function validSignalString(value, maximumLength) {
  return typeof value === "string" &&
    value.length > 0 && value.length <= maximumLength;
}

function validateSignalMessage(msg) {
  if (!msg || typeof msg !== "object" || Array.isArray(msg) ||
      typeof msg.type !== "string") {
    return "invalid message";
  }
  if (msg.type === "join") {
    if (!validIdentifier(msg.room, 128) ||
        (msg.role !== "master" && msg.role !== "viewer") ||
        (msg.clientId !== undefined && !validIdentifier(msg.clientId, 128))) {
      return "invalid join";
    }
    return null;
  }
  if (msg.type === "offer" || msg.type === "answer") {
    if (!validSignalString(msg.sdp, 256 * 1024) ||
        (msg.to !== undefined && !validIdentifier(msg.to))) {
      return `invalid ${msg.type}`;
    }
    return null;
  }
  if (msg.type === "candidate") {
    if (!validSignalString(msg.candidate, 16 * 1024) ||
        (msg.sdpMid !== undefined && msg.sdpMid !== null &&
         !validIdentifier(msg.sdpMid, 64)) ||
        (msg.sdpMLineIndex !== undefined &&
         (!Number.isInteger(msg.sdpMLineIndex) || msg.sdpMLineIndex < 0 ||
          msg.sdpMLineIndex > 64)) ||
        (msg.to !== undefined && !validIdentifier(msg.to))) {
      return "invalid candidate";
    }
    return null;
  }
  if (msg.type === "leave" || msg.type === "ping") {
    return null;
  }
  return `unknown type ${msg.type}`;
}

function routeTo(roomName, toId, fromPeer, msg) {
  const room = rooms.get(roomName);
  if (!room) return false;
  let target;
  if (room.master && room.master.id === toId) target = room.master;
  else target = room.viewers.get(toId);
  if (!target) return false;
  send(target.ws, { ...msg, from: fromPeer.id, to: toId });
  return true;
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url || "/", "http://localhost");
  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405, { "allow": "GET, HEAD" });
    res.end("method not allowed");
    return;
  }
  if (url.pathname === "/healthz") {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({ ok: true, rooms: rooms.size }));
    return;
  }

  // Phone / PC viewer: http://HOST:8089/  or /viewer.html
  let rel = url.pathname === "/" ? "/viewer.html" : url.pathname;
  rel = path.normalize(rel).replace(/^(\.\.[/\\])+/, "");
  const filePath = path.resolve(PUBLIC_DIR, `.${rel}`);
  const relativePath = path.relative(PUBLIC_DIR, filePath);
  if (relativePath.startsWith("..") || path.isAbsolute(relativePath)) {
    res.writeHead(403);
    res.end("forbidden");
    return;
  }
  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
      res.end("zero-mini signaling — try /viewer.html\n");
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { "content-type": MIME[ext] || "application/octet-stream" });
    res.end(data);
  });
});

const wss = new WebSocketServer({
  server,
  path: SIGNALING_WS_PATH,
  maxPayload: MAX_SIGNAL_MESSAGE_BYTES,
});

wss.on("connection", (ws, req) => {
  if (!authOk(req)) {
    send(ws, { type: "error", message: "unauthorized" });
    ws.close(4401, "unauthorized");
    return;
  }

  /** @type {Peer|null} */
  let peer = null;
  ws.isAlive = true;
  ws.on("pong", () => {
    ws.isAlive = true;
  });

  ws.on("message", (data, isBinary) => {
    if (isBinary || data.length > MAX_SIGNAL_MESSAGE_BYTES) {
      send(ws, { type: "error", message: "message too large or binary" });
      ws.close(1009, "message too large");
      return;
    }

    let msg;
    try {
      msg = JSON.parse(String(data));
    } catch {
      send(ws, { type: "error", message: "invalid json" });
      return;
    }

    const validationError = validateSignalMessage(msg);
    if (validationError) {
      send(ws, { type: "error", message: validationError });
      return;
    }

    const type = msg.type;
    if (type === "ping") {
      send(ws, { type: "pong", t: Date.now() });
      return;
    }

    if (type === "join") {
      const roomName = String(msg.room || "").trim();
      const role = String(msg.role || "").trim();
      if (!roomName || (role !== "master" && role !== "viewer")) {
        send(ws, { type: "error", message: "join requires room + role" });
        return;
      }
      if (peer) purgePeer(peer);

      const room = getRoom(roomName);
      let id = String(msg.clientId || "");
      if (!id) {
        do {
          id = crypto.randomBytes(12).toString("hex");
        } while ((room.master && room.master.id === id) ||
                 room.viewers.has(id));
      }

      if (role === "master") {
        // Device reconnect / crash-restart: replace any existing master.
        if (room.master) {
          const old = room.master;
          log("master replace", roomName, old.id, "->", id);
          try {
            send(old.ws, {
              type: "error",
              message: "master replaced by new device",
            });
            old.ws.close(4000, "master replaced");
          } catch (_) {
            /* ignore */
          }
          room.master = undefined;
          for (const viewer of room.viewers.values()) {
            send(viewer.ws, {
              type: "peer-left",
              role: "master",
              clientId: old.id,
            });
          }
        }
        peer = { id, role, room: roomName, ws };
        room.master = peer;
        send(ws, {
          type: "joined",
          role,
          room: roomName,
          clientId: id,
          viewers: [...room.viewers.keys()],
        });
        for (const viewer of room.viewers.values()) {
          send(viewer.ws, { type: "peer-joined", role: "master", clientId: id });
          send(ws, { type: "peer-joined", role: "viewer", clientId: viewer.id });
        }
        log("master joined", roomName, id);
        return;
      }

      if ((room.master && room.master.id === id) || room.viewers.has(id)) {
        send(ws, { type: "error", message: "client id already in use" });
        return;
      }
      if (room.viewers.size >= MAX_VIEWERS) {
        send(ws, {
          type: "error",
          message: `viewer limit reached (max ${MAX_VIEWERS})`,
        });
        log("viewer rejected (full)", roomName, "max=", MAX_VIEWERS);
        return;
      }
      peer = { id, role, room: roomName, ws };
      room.viewers.set(id, peer);
      send(ws, {
        type: "joined",
        role,
        room: roomName,
        clientId: id,
        masterId: room.master ? room.master.id : null,
      });
      if (room.master) {
        send(room.master.ws, { type: "peer-joined", role: "viewer", clientId: id });
        send(ws, {
          type: "peer-joined",
          role: "master",
          clientId: room.master.id,
        });
      }
      log("viewer joined", roomName, id);
      return;
    }

    if (!peer) {
      send(ws, { type: "error", message: "join first" });
      return;
    }

    if (type === "leave") {
      purgePeer(peer);
      peer = null;
      send(ws, { type: "left" });
      return;
    }

    if (type === "offer" || type === "answer" || type === "candidate") {
      const to = String(msg.to || "");
      if (!to) {
        // default route: viewer -> master, master -> single viewer
        const room = rooms.get(peer.room);
        if (!room) return;
        if (peer.role === "viewer" && room.master) {
          send(room.master.ws, { ...msg, from: peer.id, to: room.master.id });
          return;
        }
        if (peer.role === "master" && room.viewers.size === 1) {
          const viewer = room.viewers.values().next().value;
          send(viewer.ws, { ...msg, from: peer.id, to: viewer.id });
          return;
        }
        send(ws, { type: "error", message: "missing to" });
        return;
      }
      if (!routeTo(peer.room, to, peer, msg)) {
        send(ws, { type: "error", message: "target offline" });
      }
      return;
    }

    send(ws, { type: "error", message: `unknown type ${type}` });
  });

  ws.on("close", () => {
    purgePeer(peer);
    peer = null;
  });
});

function shutdown(signal) {
  log("shutdown", signal);
  clearInterval(heartbeat);
  for (const ws of wss.clients) {
    try { ws.close(1001, "server shutdown"); } catch (_) { /* ignore */ }
  }
  wss.close(() => server.close(() => process.exit(0)));
  setTimeout(() => process.exit(1), 5000).unref();
}

const heartbeat = setInterval(() => {
  for (const ws of wss.clients) {
    if (ws.isAlive === false) {
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.ping();
  }
}, 15000);
heartbeat.unref();
wss.on("close", () => clearInterval(heartbeat));
process.once("SIGTERM", () => shutdown("SIGTERM"));
process.once("SIGINT", () => shutdown("SIGINT"));

server.listen(PORT, () => {
  log(`signaling listen :${PORT}${SIGNALING_WS_PATH} maxViewers=${MAX_VIEWERS} token=${TOKEN ? "on" : "off"} env=${ENVIRONMENT}`);
});
