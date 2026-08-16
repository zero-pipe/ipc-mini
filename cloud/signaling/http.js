"use strict";

const http = require("http");
const fs = require("fs");
const path = require("path");

const { handleRecordRequest } = require("./record_http");

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".ico": "image/x-icon",
};

function createHttpServer({ publicDir, rooms, recordDir, recordToken }) {
  return http.createServer((req, res) => {
    const url = new URL(req.url || "/", "http://localhost");
    if (url.pathname === "/record" || url.pathname.startsWith("/record/")) {
      handleRecordRequest(req, res, { recordDir, token: recordToken });
      return;
    }
    if (req.method !== "GET" && req.method !== "HEAD") {
      res.writeHead(405, { allow: "GET, HEAD" });
      res.end("method not allowed");
      return;
    }
    if (url.pathname === "/healthz") {
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ ok: true, rooms: rooms.roomCount() }));
      return;
    }

    let rel = url.pathname === "/" ? "/viewer.html" : url.pathname;
    rel = path.normalize(rel).replace(/^(\.\.[/\\])+/, "");
    const filePath = path.resolve(publicDir, `.${rel}`);
    const relativePath = path.relative(publicDir, filePath);
    if (relativePath.startsWith("..") || path.isAbsolute(relativePath)) {
      res.writeHead(403);
      res.end("forbidden");
      return;
    }
    fs.readFile(filePath, (err, data) => {
      if (err) {
        res.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
        res.end("ipc_mini signaling — try /viewer.html\n");
        return;
      }
      const ext = path.extname(filePath).toLowerCase();
      res.writeHead(200, { "content-type": MIME[ext] || "application/octet-stream" });
      res.end(data);
    });
  });
}

module.exports = { createHttpServer };
