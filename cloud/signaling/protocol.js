"use strict";

const { send } = require("./rooms");

function validIdentifier(value, maximumLength = 128) {
  return typeof value === "string" &&
    value.length > 0 &&
    value.length <= maximumLength &&
    /^[A-Za-z0-9._:-]+$/.test(value);
}

function validSignalString(value, maximumLength) {
  return typeof value === "string" &&
    value.length > 0 &&
    value.length <= maximumLength;
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

function parseClientMessage(data, isBinary, maxBytes) {
  if (isBinary || data.length > maxBytes) {
    return { error: "message too large or binary", close: { code: 1009, reason: "message too large" } };
  }
  let msg;
  try {
    msg = JSON.parse(String(data));
  } catch {
    return { error: "invalid json" };
  }
  const invalid = validateSignalMessage(msg);
  if (invalid) {
    return { error: invalid };
  }
  return { msg };
}

/**
 * Wire types (do not change without updating device + viewer):
 *   in:  join, offer, answer, candidate, leave, ping
 *   out: joined, peer-joined, peer-left, left, pong, error
 */
function handleClientMessage({ ws, peer, rooms, msg }) {
  if (msg.type === "ping") {
    send(ws, { type: "pong", t: Date.now() });
    return { peer };
  }

  if (msg.type === "join") {
    if (peer) {
      rooms.leave(peer);
    }
    const roomName = msg.room;
    const requestedId = msg.clientId || "";
    const result = msg.role === "master"
      ? rooms.joinMaster(ws, roomName, requestedId)
      : rooms.joinViewer(ws, roomName, requestedId);
    if (!result.ok) {
      send(ws, { type: "error", message: result.error });
      return { peer: null };
    }
    return { peer: result.peer };
  }

  if (!peer) {
    send(ws, { type: "error", message: "join first" });
    return { peer };
  }

  if (msg.type === "leave") {
    rooms.leave(peer);
    send(ws, { type: "left" });
    return { peer: null };
  }

  if (msg.type === "offer" || msg.type === "answer" || msg.type === "candidate") {
    const to = String(msg.to || "");
    if (!to) {
      if (!rooms.routeDefault(peer, msg)) {
        send(ws, { type: "error", message: "missing to" });
      }
      return { peer };
    }
    if (!rooms.route(peer, to, msg)) {
      send(ws, { type: "error", message: "target offline" });
    }
    return { peer };
  }

  send(ws, { type: "error", message: `unknown type ${msg.type}` });
  return { peer };
}

module.exports = { parseClientMessage, handleClientMessage };
