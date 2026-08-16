"use strict";

const crypto = require("crypto");

function send(ws, message) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(message));
  }
}

function newClientId(room) {
  let id;
  do {
    id = crypto.randomBytes(12).toString("hex");
  } while (roomHasId(room, id));
  return id;
}

function roomHasId(room, id) {
  return (room.master && room.master.id === id) || room.viewers.has(id);
}

class SignalingRooms {
  constructor({ maxViewers, log }) {
    this.maxViewers = maxViewers;
    this.log = log;
    /** @type {Map<string, { master?: object, viewers: Map<string, object> }>} */
    this.rooms = new Map();
  }

  roomCount() {
    return this.rooms.size;
  }

  ensure(name) {
    let room = this.rooms.get(name);
    if (!room) {
      room = { viewers: new Map() };
      this.rooms.set(name, room);
    }
    return room;
  }

  dropIfEmpty(name, room) {
    if (!room.master && room.viewers.size === 0) {
      this.rooms.delete(name);
    }
  }

  notifyViewers(room, message) {
    for (const viewer of room.viewers.values()) {
      send(viewer.ws, message);
    }
  }

  joinMaster(ws, roomName, requestedId) {
    const room = this.ensure(roomName);
    const id = requestedId || newClientId(room);
    if (room.master) {
      const old = room.master;
      room.master = undefined;
      send(old.ws, { type: "error", message: "master replaced by new device" });
      try {
        old.ws.close(4000, "master replaced");
      } catch (_) {
        /* ignore */
      }
      this.notifyViewers(room, {
        type: "peer-left",
        role: "master",
        clientId: old.id,
      });
      this.log("master replace", roomName, old.id, "->", id);
    }

    const peer = { id, role: "master", room: roomName, ws };
    room.master = peer;
    send(ws, {
      type: "joined",
      role: "master",
      room: roomName,
      clientId: id,
      viewers: [...room.viewers.keys()],
    });
    for (const viewer of room.viewers.values()) {
      send(viewer.ws, { type: "peer-joined", role: "master", clientId: id });
      send(ws, { type: "peer-joined", role: "viewer", clientId: viewer.id });
    }
    this.log("master joined", roomName, id);
    return { ok: true, peer };
  }

  joinViewer(ws, roomName, requestedId) {
    const room = this.ensure(roomName);
    if (requestedId && roomHasId(room, requestedId)) {
      return { ok: false, error: "client id already in use" };
    }
    if (room.viewers.size >= this.maxViewers) {
      this.log("viewer rejected (full)", roomName, "max=", this.maxViewers);
      return { ok: false, error: `viewer limit reached (max ${this.maxViewers})` };
    }

    const id = requestedId || newClientId(room);
    const peer = { id, role: "viewer", room: roomName, ws };
    room.viewers.set(id, peer);
    send(ws, {
      type: "joined",
      role: "viewer",
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
    this.log("viewer joined", roomName, id);
    return { ok: true, peer };
  }

  leave(peer) {
    if (!peer) {
      return;
    }
    const room = this.rooms.get(peer.room);
    if (!room) {
      return;
    }

    if (peer.role === "master" && room.master === peer) {
      room.master = undefined;
      this.notifyViewers(room, {
        type: "peer-left",
        role: "master",
        clientId: peer.id,
      });
      this.log("master left", peer.room, peer.id);
    } else if (peer.role === "viewer" && room.viewers.get(peer.id) === peer) {
      room.viewers.delete(peer.id);
      if (room.master) {
        send(room.master.ws, {
          type: "peer-left",
          role: "viewer",
          clientId: peer.id,
        });
      }
      this.log("viewer left", peer.room, peer.id);
    }

    this.dropIfEmpty(peer.room, room);
  }

  find(roomName, id) {
    const room = this.rooms.get(roomName);
    if (!room) {
      return undefined;
    }
    if (room.master && room.master.id === id) {
      return room.master;
    }
    return room.viewers.get(id);
  }

  route(fromPeer, toId, message) {
    const target = this.find(fromPeer.room, toId);
    if (!target) {
      return false;
    }
    send(target.ws, { ...message, from: fromPeer.id, to: toId });
    return true;
  }

  routeDefault(fromPeer, message) {
    const room = this.rooms.get(fromPeer.room);
    if (!room) {
      return false;
    }
    if (fromPeer.role === "viewer" && room.master) {
      send(room.master.ws, {
        ...message,
        from: fromPeer.id,
        to: room.master.id,
      });
      return true;
    }
    if (fromPeer.role === "master" && room.viewers.size === 1) {
      const viewer = room.viewers.values().next().value;
      send(viewer.ws, { ...message, from: fromPeer.id, to: viewer.id });
      return true;
    }
    return false;
  }
}

module.exports = { SignalingRooms, send };
