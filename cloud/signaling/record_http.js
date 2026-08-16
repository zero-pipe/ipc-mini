"use strict";

const fs = require("fs");
const path = require("path");
const { pipeline } = require("stream");

const MIME = {
  ".m3u8": "application/vnd.apple.mpegurl",
  ".m4s": "video/iso.segment",
  ".mp4": "video/mp4",
};

const MAX_PUT_BYTES = 64 * 1024 * 1024;

function cors(res) {
  res.setHeader("access-control-allow-origin", "*");
  res.setHeader("access-control-allow-methods", "GET, HEAD, PUT, OPTIONS");
  res.setHeader("access-control-allow-headers", "content-type, x-record-token");
  res.setHeader("access-control-expose-headers", "content-length, content-range, accept-ranges");
}

function recordRel(urlPath) {
  const raw = decodeURIComponent(urlPath || "");
  if (!raw.startsWith("/record")) {
    return null;
  }
  let rel = raw.slice("/record".length).replace(/^\/+/, "");
  rel = path.posix.normalize(rel).replace(/^(\.\.(\/|$))+/, "");
  if (rel === "." || rel.includes("..")) {
    return "";
  }
  return rel;
}

function resolveRecordPath(recordDir, rel) {
  const root = path.resolve(recordDir);
  const full = path.resolve(root, rel);
  const relative = path.relative(root, full);
  if (relative.startsWith("..") || path.isAbsolute(relative)) {
    return null;
  }
  return full;
}

function tokenOk(req, token) {
  if (!token) {
    return true;
  }
  const url = new URL(req.url || "/", "http://localhost");
  return req.headers["x-record-token"] === token ||
    url.searchParams.get("token") === token;
}

function handleRecordRequest(req, res, { recordDir, token }) {
  cors(res);
  if (req.method === "OPTIONS") {
    res.writeHead(204);
    res.end();
    return true;
  }

  const url = new URL(req.url || "/", "http://localhost");
  const rel = recordRel(url.pathname);
  if (rel === null) {
    return false;
  }

  if (req.method === "PUT" && !tokenOk(req, token)) {
    res.writeHead(401, { "content-type": "text/plain; charset=utf-8" });
    res.end("unauthorized");
    return true;
  }

  if (req.method === "PUT") {
    if (!rel) {
      res.writeHead(400);
      res.end("missing object key");
      return true;
    }
    const ext = path.extname(rel).toLowerCase();
    if (!MIME[ext]) {
      res.writeHead(415);
      res.end("unsupported record type");
      return true;
    }
    const dest = resolveRecordPath(recordDir, rel);
    if (!dest) {
      res.writeHead(403);
      res.end("forbidden");
      return true;
    }
    const length = Number(req.headers["content-length"] || 0);
    if (!Number.isFinite(length) || length <= 0 || length > MAX_PUT_BYTES) {
      res.writeHead(413);
      res.end("invalid content-length");
      return true;
    }
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    const tmp = dest + ".part";
    const out = fs.createWriteStream(tmp);
    let received = 0;
    req.on("data", (chunk) => {
      received += chunk.length;
      if (received > MAX_PUT_BYTES) {
        req.destroy();
        out.destroy();
        fs.unlink(tmp, () => {});
      }
    });
    pipeline(req, out, (err) => {
      if (err || received !== length) {
        fs.unlink(tmp, () => {});
        if (!res.headersSent) {
          res.writeHead(500);
          res.end("write failed");
        }
        return;
      }
      fs.rename(tmp, dest, (renameErr) => {
        if (renameErr) {
          res.writeHead(500);
          res.end("commit failed");
          return;
        }
        console.log(new Date().toISOString(), "record put", rel, length);
        res.writeHead(201, { "content-type": "application/json" });
        res.end(JSON.stringify({ ok: true, key: rel, bytes: length }));
      });
    });
    return true;
  }

  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405, { allow: "GET, HEAD, PUT, OPTIONS" });
    res.end("method not allowed");
    return true;
  }

  const dest = resolveRecordPath(recordDir, rel);
  if (!dest) {
    res.writeHead(403);
    res.end("forbidden");
    return true;
  }

  fs.stat(dest, (err, st) => {
    if (err) {
      res.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
      res.end("not found");
      return;
    }
    if (st.isDirectory()) {
      fs.readdir(dest, (readErr, names) => {
        if (readErr) {
          res.writeHead(500);
          res.end("list failed");
          return;
        }
        res.writeHead(200, { "content-type": "application/json; charset=utf-8" });
        res.end(JSON.stringify({ path: rel || ".", files: names }));
      });
      return;
    }
    const ext = path.extname(dest).toLowerCase();
    const type = MIME[ext] || "application/octet-stream";
    const range = req.headers.range;
    if (range) {
      const match = /^bytes=(\d*)-(\d*)$/.exec(range);
      if (!match) {
        res.writeHead(416);
        res.end();
        return;
      }
      const start = match[1] ? Number(match[1]) : 0;
      const end = match[2] ? Number(match[2]) : st.size - 1;
      if (start < 0 || end >= st.size || start > end) {
        res.writeHead(416);
        res.end();
        return;
      }
      res.writeHead(206, {
        "content-type": type,
        "content-length": end - start + 1,
        "content-range": `bytes ${start}-${end}/${st.size}`,
        "accept-ranges": "bytes",
      });
      if (req.method === "HEAD") {
        res.end();
        return;
      }
      fs.createReadStream(dest, { start, end }).pipe(res);
      return;
    }
    res.writeHead(200, {
      "content-type": type,
      "content-length": st.size,
      "accept-ranges": "bytes",
    });
    if (req.method === "HEAD") {
      res.end();
      return;
    }
    fs.createReadStream(dest).pipe(res);
  });
  return true;
}

module.exports = { handleRecordRequest };
