import { crc32, toBase64, fromBase64, MAX_LAYOUT_BYTES } from './device-codec.mjs';

export class FlightdeckSerial {
  constructor(serial = globalThis.navigator?.serial) {
    this.serial = serial; this.port = null; this.pending = new Map(); this.nextId = 1; this.epoch = 0;
    this.queue = Promise.resolve(); this.onDisconnect = () => {}; this.onStatus = () => {};
  }
  get connected() { return !!this.port && this.verified; }
  async choose() {
    if (!this.serial) throw new Error('USB needs desktop Chrome or Edge on HTTPS or localhost.');
    // Call from the click handler, before any unrelated asynchronous work.
    const port = await this.serial.requestPort();
    return this.open(port);
  }
  async open(port) {
    if (this.port) await this.close();
    await port.open({baudRate: 115200, bufferSize: 16384});
    this.port = port; this.verified = false; this.epoch++; this.queue = Promise.resolve();
    this.writer = port.writable.getWriter();
    this.reading = this.readLoop(port);
    try {
      // Opening a USB UART may reset the board. Retry only the harmless handshake.
      let info;
      for (let attempt = 0; attempt < 4; attempt++) {
        try { info = await this.request('hello', {}, 1800); break; }
        catch (e) { if (!this.port || attempt === 3) throw e; }
      }
      if (info?.product !== 'flightdeck' || info.protocol !== 2 || info.width !== 64 || info.height !== 32)
        throw new Error('This port is not running Flightdeck USB firmware v2. Flash the updated firmware first.');
      this.verified = true; return info;
    } catch (e) { await this.close(); throw e; }
  }
  exclusive(fn) {
    const epoch = this.epoch;
    const result = this.queue.then(() => {
      if (epoch !== this.epoch) throw new Error("USB session changed.");
      return fn();
    });
    this.queue = result.catch(() => {});
    return result;
  }
  command(cmd, fields = {}) {
    return this.exclusive(() => {
      if (!this.connected) throw new Error('Connect a Flightdeck ESP32 first.');
      return this.request(cmd, fields);
    });
  }
  request(cmd, fields = {}, timeout = 5000) {
    if (!this.port) return Promise.reject(new Error('USB disconnected.'));
    const id = this.nextId++, bytes = new TextEncoder().encode(JSON.stringify({...fields, id, cmd}) + '\n');
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`ESP32 timed out (${cmd}). Close Serial Monitor and reconnect.`)); }, timeout);
      this.pending.set(id, {resolve, reject, timer});
      this.writer.write(bytes).catch(error => {
        const item = this.pending.get(id); if (!item) return;
        clearTimeout(item.timer); this.pending.delete(id); item.reject(error);
      });
    });
  }
  async readLoop(port) {
    let buffer = '', discard = false;
    const decoder = new TextDecoder();
    this.reader = port.readable.getReader();
    try {
      while (this.port === port) {
        const {value, done} = await this.reader.read(); if (done) break;
        for (const ch of decoder.decode(value, {stream: true})) {
          if (ch === '\n') {
            if (!discard) this.receive(buffer);
            buffer = ''; discard = false;
          } else if (!discard) {
            buffer += ch;
            if (buffer.length > 32768) { buffer = ''; discard = true; }
          }
        }
      }
    } catch { /* Unplug and cancelled reads share the same cleanup. */ }
    finally {
      this.reader.releaseLock(); this.reader = null;
      this.rejectPending('USB disconnected.');
      if (!this.closing && this.port === port) {
        this.verified = false;
        this.writer?.releaseLock(); this.writer = null; this.port = null;
        await port.close().catch(() => {}); this.onDisconnect();
      }
    }
  }
  receive(line) {
    let response; try { response = JSON.parse(line); } catch { return; } // Boot diagnostics are not protocol messages.
    const item = this.pending.get(response.id); if (!item) return;
    this.pending.delete(response.id); clearTimeout(item.timer);
    if (response.ok === true) item.resolve(response);
    else item.reject(new Error(response.error || 'The ESP32 rejected this operation.'));
  }
  rejectPending(message) {
    for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(new Error(message)); }
    this.pending.clear();
  }
  upload(kind, bytes) {
    return this.exclusive(async () => {
      if (!this.connected) throw new Error('Connect a Flightdeck ESP32 first.');
      if (!['layout', 'frame'].includes(kind) || bytes.length > MAX_LAYOUT_BYTES) throw new Error('Invalid USB upload.');
      await this.request('upload_begin', {kind, length: bytes.length, crc: crc32(bytes)});
      try {
        for (let offset = 0; offset < bytes.length; offset += 384)
          await this.request('upload_chunk', {offset, data: toBase64(bytes.subarray(offset, offset + 384))});
        return await this.request('upload_commit');
      } catch (error) { await this.request('upload_abort').catch(() => {}); throw error; }
    });
  }
  downloadLayout() {
    return this.exclusive(async () => {
      if (!this.connected) throw new Error('Connect a Flightdeck ESP32 first.');
      const info = await this.request('layout_info');
      if (!Number.isInteger(info.length) || info.length < 65 || info.length > MAX_LAYOUT_BYTES) throw new Error('Invalid board layout size.');
      const bytes = new Uint8Array(info.length);
      for (let offset = 0; offset < bytes.length; offset += 384) {
        const reply = await this.request('layout_chunk', {offset});
        const chunk = fromBase64(reply.data);
        if (chunk.length !== Math.min(384, bytes.length-offset)) throw new Error('Incomplete layout readback.');
        bytes.set(chunk, offset);
      }
      if (crc32(bytes) !== info.crc) throw new Error('Board layout checksum failed.');
      return bytes;
    });
  }
  async close() {
    if (!this.port) return;
    const port = this.port; this.epoch++; this.closing = true; this.verified = false;
    this.rejectPending('USB disconnected.');
    try { await this.reader?.cancel(); await this.reading; } catch { /* Already removed. */ }
    try { this.writer?.releaseLock(); await port.close(); } catch { /* Already removed. */ }
    this.port = null; this.writer = null; this.closing = false; this.onDisconnect();
  }
}

// A single frame in flight and one replaceable pending frame prevent drag events
// from building a stale FIFO of several seconds of display updates.
export class LivePreview {
  constructor(device, onError = () => {}) { this.device = device; this.onError = onError; this.enabled = false; this.pending = null; this.running = false; }
  submit(pixels, brightness) {
    if (!this.enabled || !this.device.connected) return;
    const bytes = new Uint8Array(4097); bytes[0] = brightness; bytes.set(pixels, 1);
    this.pending = bytes;
    if (!this.running) this.flush();
  }
  async flush() {
    this.running = true;
    try {
      while (this.enabled && this.pending) { const bytes = this.pending; this.pending = null; await this.device.upload('frame', bytes); }
    } catch (error) { this.enabled = false; this.pending = null; this.onError(error); }
    finally { this.running = false; }
  }
  async stop() { this.enabled = false; this.pending = null; if (this.device.connected) await this.device.command('resume'); }
}
