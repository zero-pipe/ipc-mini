#!/usr/bin/env node
"use strict";

/**
 * ipc_mini signaling
 *
 *   rooms.js     room table (1 master + N viewers)
 *   protocol.js  join / offer / answer / candidate / leave / ping
 *   http.js      /healthz + viewer page
 *   record_http.js  PUT/GET /record/* (CMAF + HLS)
 *
 * Wire schema is stable. Device and viewer.html depend on it.
 */

const path = require("path");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");
const { SignalingRooms, send } = require("./rooms");
const { parseClientMessage, handleClientMessage } = require("./protocol");
const { createHttpServer } = require("./http");

function parsePositiveInteger(value, fallback, maximum = Number.MAX_SAFE_INTEGER) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1 || parsed > maximum) {
    return fallback;
  }
  return parsed;
}

function loadConfig() {
  const token = process.env.SIGNALING_TOKEN || "";
  const environment = process.env.SIGNALING_ENV || "demo";
  if (environment === "production" && !token) {
    throw new Error("SIGNALING_TOKEN is required in production");
  }
  return {
    port: parsePositiveInteger(process.env.PORT, 8089, 65535),
    wsPath: process.env.SIGNALING_PATH || "/ws",
    maxViewers: parsePositiveInteger(process.env.MAX_VIEWERS, 3, 3),
    maxMessageBytes: parsePositiveInteger(
      process.env.MAX_SIGNAL_MESSAGE_BYTES,
      256 * 1024,
      4 * 1024 * 1024,
    ),
    token,
    environment,
    publicDir: path.join(__dirname, "public"),
    recordDir: process.env.RECORD_DIR || path.join(__dirname, "record"),
  };
}

function safeTokenEqual(actual, expected) {
  if (typeof actual !== "string" || typeof expected !== "string") {
    return false;
  }
  const actualBuffer = Buffer.from(actual);
  const expectedBuffer = Buffer.from(expected);
  return actualBuffer.length === expectedBuffer.length &&
    crypto.timingSafeEqual(actualBuffer, expectedBuffer);
}

function authOk(req, token) {
  if (!token) {
    return true;
  }
  const url = new URL(req.url || "/", "http://localhost");
  return safeTokenEqual(url.searchParams.get("token"), token) ||
    safeTokenEqual(req.headers["x-signaling-token"], token);
}

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

const config = loadConfig();
const rooms = new SignalingRooms({ maxViewers: config.maxViewers, log });
const server = createHttpServer({
  publicDir: config.publicDir,
  rooms,
  recordDir: config.recordDir,
  recordToken: config.token,
});
const wss = new WebSocketServer({
  server,
  path: config.wsPath,
  maxPayload: config.maxMessageBytes,
});

wss.on("connection", (ws, req) => {
  if (!authOk(req, config.token)) {
    send(ws, { type: "error", message: "unauthorized" });
    ws.close(4401, "unauthorized");
    return;
  }

  let peer = null;
  ws.isAlive = true;
  ws.on("pong", () => {
    ws.isAlive = true;
  });

  ws.on("message", (data, isBinary) => {
    const parsed = parseClientMessage(data, isBinary, config.maxMessageBytes);
    if (parsed.error) {
      send(ws, { type: "error", message: parsed.error });
      if (parsed.close) {
        ws.close(parsed.close.code, parsed.close.reason);
      }
      return;
    }
    const next = handleClientMessage({ ws, peer, rooms, msg: parsed.msg });
    peer = next.peer;
  });

  ws.on("close", () => {
    rooms.leave(peer);
    peer = null;
  });
});

function shutdown(signal) {
  log("shutdown", signal);
  clearInterval(heartbeat);
  for (const socket of wss.clients) {
    try {
      socket.close(1001, "server shutdown");
    } catch (_) {
      /* ignore */
    }
  }
  wss.close(() => server.close(() => process.exit(0)));
  setTimeout(() => process.exit(1), 5000).unref();
}

const heartbeat = setInterval(() => {
  for (const socket of wss.clients) {
    if (socket.isAlive === false) {
      socket.terminate();
      continue;
    }
    socket.isAlive = false;
    socket.ping();
  }
}, 15000);
heartbeat.unref();
wss.on("close", () => clearInterval(heartbeat));
process.once("SIGTERM", () => shutdown("SIGTERM"));
process.once("SIGINT", () => shutdown("SIGINT"));

server.listen(config.port, () => {
  log(
    `signaling listen :${config.port}${config.wsPath}`,
    `maxViewers=${config.maxViewers}`,
    `token=${config.token ? "on" : "off"}`,
    `env=${config.environment}`,
    `record=${config.recordDir}`,
  );
});
