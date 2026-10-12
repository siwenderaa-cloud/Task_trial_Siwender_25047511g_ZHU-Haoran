const $ = id => document.getElementById(id);
const feed = $('feed');
let busy = false;
let viewing = false;
let lastSequence = -1;
let lastState = '';

function viewStream() {
  feed.src = '/stream?t=' + Date.now();
  viewing = true;
}

function render(s) {
  $('connection').dataset.state = s.state;
  const labels = { stopped: 'Stream paused', starting: 'Connecting…', live: 'Connected', waiting: 'Waiting for camera', disconnected: 'Disconnected', error: 'Connection error' };
  $('connection-text').textContent = s.sequence === 0 && s.state === 'stopped' ? 'Not connected' : (labels[s.state] || s.state);
  $('start').disabled = busy || s.running;
  $('start').lastChild.textContent = s.running ? (s.connected ? 'Stream is running' : 'Connecting…') : 'Start stream';
  $('stop').disabled = busy || !s.running;
  $('message').textContent = s.message;
  $('resolution').textContent = s.width ? `${s.width} × ${s.height}` : '—';
  $('fps').textContent = s.sessionFrames > 1 ? `${s.fps.toFixed(2)} fps` : '—';
  $('frames').textContent = String(s.sessionFrames);
  $('feed-tag').textContent = s.connected ? 'LIVE FEED' : s.running ? 'CONNECTING' : s.sequence ? 'PAUSED' : 'STANDBY';
  $('feed-dot').classList.toggle('live', s.connected);
  if (s.sequence > 0) {
    $('empty').hidden = true;
    feed.hidden = false;
    $('frame-info').textContent = `${s.width} × ${s.height} · ${(s.bytes / 1024).toFixed(1)} KB · FRAME ${s.sequence}`;
    $('updated').textContent = 'Updated ' + new Date(s.lastFrameAt).toLocaleTimeString([], { hour12: false });
    $('save').setAttribute('aria-disabled', 'false');
    $('save').tabIndex = 0;
  } else {
    // A restarted backend has no frame yet: do not present an old image as live.
    $('empty').hidden = false;
    feed.hidden = true;
    $('frame-info').textContent = 'Waiting for the first frame';
    $('updated').textContent = '—';
    $('save').setAttribute('aria-disabled', 'true');
    $('save').tabIndex = -1;
    if (!s.running) feed.removeAttribute('src');
  }
  feed.alt = s.connected ? 'Live image from HBVCAM connected to ESP32-P4' : 'Last received image from HBVCAM';
  $('stage-badge').hidden = !s.sequence || s.connected;
  $('stage-badge').textContent = s.running ? 'Waiting · last received frame' : s.state === 'disconnected' ? 'Disconnected · last received frame' : 'Paused · last received frame';
  if (s.running && (!viewing || lastState === 'stopped' || lastState === 'disconnected')) viewStream();
  if (!s.running && (viewing || (s.sequence && !feed.getAttribute('src')))) {
    viewing = false;
    if (s.sequence) feed.src = '/api/frame?t=' + s.sequence;
    else feed.removeAttribute('src');
  }
  lastSequence = s.sequence;
  lastState = s.state;
}

async function refresh() {
  try {
    const res = await fetch('/api/status', { cache: 'no-store', signal: AbortSignal.timeout(4000) });
    if (!res.ok) throw new Error('Status unavailable');
    render(await res.json());
  } catch {
    $('connection').dataset.state = 'disconnected';
    $('connection-text').textContent = 'Server disconnected';
    $('message').textContent = 'Start the local server on your Mac, then refresh this page.';
    $('start').disabled = true; $('stop').disabled = true;
    $('start').lastChild.textContent = 'Server offline';
    $('fps').textContent = '—';
    $('save').setAttribute('aria-disabled', 'true'); $('save').tabIndex = -1;
    feed.alt = 'Last received image from HBVCAM';
    $('feed-dot').classList.remove('live'); $('feed-tag').textContent = 'OFFLINE';
    if (lastSequence > 0) { $('stage-badge').hidden = false; $('stage-badge').textContent = 'Server disconnected · last received frame'; }
  }
}

async function control(action) {
  busy = true; $('start').disabled = true; $('stop').disabled = true;
  try {
    const res = await fetch('/api/' + action, { method: 'POST', signal: AbortSignal.timeout(5000) });
    if (!res.ok) throw new Error('Control request failed');
    busy = false; render(await res.json());
  } catch (error) { busy = false; $('message').textContent = error.message; await refresh(); }
}
$('start').addEventListener('click', () => control('start'));
$('stop').addEventListener('click', () => control('stop'));
feed.addEventListener('error', () => { viewing = false; });
refresh();
setInterval(refresh, 1000);
