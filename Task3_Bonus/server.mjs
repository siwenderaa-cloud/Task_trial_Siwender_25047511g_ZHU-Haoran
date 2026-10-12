import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { spawn } from 'node:child_process';
import readline from 'node:readline';

// One acquisition worker owns the serial port. All viewers share its latest frame.
const root = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.PORT || 4174);
if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('PORT must be 1024..65535');
const host = '127.0.0.1';
const clients = new Set();
let worker = null;
let desired = false;
let latest = null;
let sequence = 0;
let sessionFrames = 0;
let sessionStarted = 0;
let sessionFirstFrame = 0;
let serialPort = '';
let state = 'stopped';
let message = 'Ready. Start the stream to connect your camera.';
let lastLog = '';

function status() {
  const age = latest ? Date.now() - latest.time : null;
  const fresh = desired && age !== null && age < 12000 && state === 'live';
  return { state: desired && state === 'live' && !fresh ? 'waiting' : state,
    running: desired, connected: fresh, message: desired && state === 'live' && !fresh ? 'Waiting for a new camera frame. Check the camera connection.' : message, serialPort,
    sequence, sessionFrames, width: latest?.width ?? null, height: latest?.height ?? null,
    bytes: latest?.jpeg.length ?? null, lastFrameAt: latest?.time ?? null,
    frameAgeMs: age, fps: sessionFrames > 1 ? (sessionFrames - 1) * 1000 / Math.max(1, latest.time - sessionFirstFrame) : 0,
    sessionStarted: sessionStarted || null, transport: 'MJPEG over HTTP', lastLog };
}

function finishStreams() {
  for (const res of clients) res.end('--camera-frame--\r\n');
  clients.clear();
}

function onWorkerEvent(event, child) {
  if (child !== worker) return;
  if (event.type === 'open') {
    serialPort = event.port;
    message = 'Serial connected. Waiting for a complete camera frame.';
  } else if (event.type === 'log') {
    lastLog = String(event.message).slice(0, 400);
  } else if (event.type === 'error') {
    desired = false; state = 'disconnected'; message = event.message;
    console.error('[camera]', message); finishStreams();
  } else if (event.type === 'frame' && desired) {
    const jpeg = Buffer.from(event.data, 'base64');
    if (jpeg.length < 4 || jpeg.length > 1048576 || jpeg[0] !== 255 || jpeg[1] !== 216 || jpeg.at(-2) !== 255 || jpeg.at(-1) !== 217) return;
    latest = { jpeg, width: event.width, height: event.height, time: Date.now() };
    state = 'live'; message = 'Receiving real frames from HBVCAM through ESP32-P4.';
    sequence++; sessionFrames++;
    if (sessionFrames === 1) sessionFirstFrame = latest.time;
    console.log(`[frame] ${sequence} ${latest.width}x${latest.height} ${jpeg.length} bytes`);
    const header = Buffer.from(`--camera-frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${jpeg.length}\r\nX-Sequence: ${sequence}\r\n\r\n`);
    const chunk = Buffer.concat([header, jpeg, Buffer.from('\r\n')]);
    // Skip a slow viewer's frame instead of allowing an unbounded output queue.
    for (const res of clients) if (!res.destroyed && !res.writableNeedDrain) res.write(chunk);
  }
}

function command(action) {
  if (worker && !worker.killed && !worker.stdin.destroyed) worker.stdin.write(action + '\n');
}

function start() {
  if (desired) return;
  desired = true; state = 'starting'; message = 'Connecting to the camera…';
  sessionFrames = 0; sessionStarted = Date.now(); sessionFirstFrame = 0;
  if (worker && !worker.killed) { command('start'); return; }
  const args = ['-u', path.join(root, 'bridge/serial_bridge.py')];
  if (process.env.SERIAL_PORT) args.push('--port', process.env.SERIAL_PORT);
  const child = spawn(process.env.PYTHON || 'python3', args, { cwd: root, stdio: ['pipe', 'pipe', 'pipe'] });
  worker = child;
  const lines = readline.createInterface({ input: child.stdout });
  lines.on('line', line => {
    try { onWorkerEvent(JSON.parse(line), child); }
    catch { console.error('[bridge] Invalid event'); }
  });
  child.stderr.on('data', data => console.error('[bridge]', String(data).trim().slice(0, 1000)));
  child.stdin.on('error', () => {});
  child.on('error', error => {
    if (worker !== child) return;
    state = 'disconnected'; desired = false; message = `Cannot run Python: ${error.message}`;
    worker = null; finishStreams();
  });
  child.on('exit', () => {
    if (worker !== child) return;
    worker = null;
    if (desired) { state = 'disconnected'; desired = false; message = 'Camera connection closed. Check the cable and try Start stream.'; }
    finishStreams();
  });
  command('start');
}

function stop() {
  desired = false; state = 'stopped'; message = 'Stream paused. The last received frame is shown.';
  command('stop'); finishStreams();
}

const assets = { '/': ['dist/index.html', 'text/html; charset=utf-8'],
  '/app.js': ['dist/app.js', 'text/javascript; charset=utf-8'],
  '/style.css': ['dist/style.css', 'text/css; charset=utf-8'] };
function json(res, code, value) {
  res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(value));
}

const server = http.createServer(async (req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  // Local camera controls only accept requests from this local page.
  if (![`${host}:${port}`, `localhost:${port}`].includes(req.headers.host)) return json(res, 403, { error: 'Local access only' });
  const url = new URL(req.url, `http://${host}:${port}`);
  if (req.method === 'POST') {
    const origin = req.headers.origin;
    if ((origin && ![`http://${host}:${port}`, `http://localhost:${port}`].includes(origin)) || req.headers['sec-fetch-site'] === 'cross-site') return json(res, 403, { error: 'Use the local camera page' });
    req.resume();
    if (url.pathname === '/api/start') { start(); return json(res, 200, status()); }
    if (url.pathname === '/api/stop') { stop(); return json(res, 200, status()); }
    return json(res, 404, { error: 'Not found' });
  }
  if (req.method !== 'GET') return json(res, 405, { error: 'Method not allowed' });
  if (url.pathname === '/api/status') return json(res, 200, status());
  if (url.pathname === '/api/frame') {
    if (!latest) return json(res, 404, { error: 'No camera frame received yet' });
    res.writeHead(200, { 'Content-Type': 'image/jpeg', 'Content-Length': latest.jpeg.length,
      'Cache-Control': 'no-store', ...(url.searchParams.has('download') ? { 'Content-Disposition': 'attachment; filename="hbvcam-frame.jpg"' } : {}) });
    return res.end(latest.jpeg);
  }
  if (url.pathname === '/stream') {
    if (!desired) return json(res, 409, { error: 'Start the stream first' });
    if (clients.size >= 8) return json(res, 503, { error: 'Too many viewers' });
    res.writeHead(200, { 'Content-Type': 'multipart/x-mixed-replace; boundary=camera-frame',
      'Cache-Control': 'no-store, no-cache', 'X-Accel-Buffering': 'no' });
    res.flushHeaders(); clients.add(res);
    if (latest && state === 'live') res.write(Buffer.concat([Buffer.from(`--camera-frame\r\nContent-Type: image/jpeg\r\nContent-Length: ${latest.jpeg.length}\r\n\r\n`), latest.jpeg, Buffer.from('\r\n')]));
    res.on('close', () => clients.delete(res)); return;
  }
  if (url.pathname === '/favicon.ico') { res.writeHead(204); return res.end(); }
  const asset = assets[url.pathname];
  if (!asset) return json(res, 404, { error: 'Not found' });
  try {
    const data = await readFile(path.join(root, asset[0]));
    res.writeHead(200, { 'Content-Type': asset[1], 'Cache-Control': 'no-store',
      'Content-Security-Policy': "default-src 'self'; img-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'" });
    res.end(data);
  } catch { json(res, 500, { error: 'Page asset unavailable' }); }
});
server.on('error', error => { console.error(error.code === 'EADDRINUSE' ? `Port ${port} is in use. Try PORT=4175 python3 run.py` : error.message); process.exitCode = 1; });
server.listen(port, host, () => console.log(`Camera Stream: http://${host}:${port}\nPress Ctrl+C to stop the server. Camera capture starts only when you press Start stream.`));
function shutdown() {
  desired = false; command('quit'); finishStreams(); server.close();
  const child = worker;
  setTimeout(() => { child?.kill('SIGTERM'); process.exit(0); }, 800).unref();
}
process.on('SIGINT', shutdown); process.on('SIGTERM', shutdown);
