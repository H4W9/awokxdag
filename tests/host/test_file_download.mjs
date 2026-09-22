import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

// Exercise the actual browser download assembler without a site/firmware build.
const html = await readFile(new URL('../../website/public/control.html', import.meta.url), 'utf8');
const start = html.indexOf('function finishFileDownload(name, size) {');
const end = html.indexOf('\nfunction downloadBlob(', start);
assert.ok(start >= 0 && end > start);
const source = html.slice(start, end);
const csv = Buffer.from(
  'WigleWifi-1.6,appRelease=AxD,model=ESP32,release=1.7.0,device=AxD,display=none,board=AxD,brand=AxD\r\n' +
  'MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\r\n' +
  '00:11:22:33:44:55,Test,[OPEN],2026-09-22 12:00:00,1,2412,-55,42.0,-83.0,200,5,,,WIFI\r\n'
);

function transfer(data = csv, preview = false) {
  const saved = [], previews = [], errors = [];
  const chunks = new Map();
  const total = Math.ceil(data.length / 96) || 1;
  // Deliberately receive chunks out of order, as a wireless relay can.
  for (let i = total; i >= 1; --i) {
    chunks.set(i, data.subarray((i - 1) * 96, i * 96).toString('base64'));
  }
  const context = vm.createContext({
    activeFileDownload: {name: 'wardrive-0001.csv', totalChunks: total, chunks, isPreview: preview},
    lastDownloadedFile: null,
    Blob, Uint8Array, atob,
    log() {}, formatBytes: String,
    alert: message => errors.push(message),
    getFileTypeInfo: () => ({mime: 'text/csv', isText: true}),
    isIosDevice: () => false,
    showFileReadyCard() {},
    downloadBlob: (blob, name) => saved.push({blob, name}),
    displayFilePreview: (name, blob) => previews.push({blob, name}),
  });
  context.resetDownloadProgress = () => { context.activeFileDownload = null; };
  vm.runInContext(source, context);
  return {context, chunks, saved, previews, errors,
    finish: (size = data.length) => context.finishFileDownload('wardrive-0001.csv', size)};
}

test('complete download preserves both CSV headers and all bytes in sequence', async () => {
  const t = transfer();
  t.finish();
  assert.equal(t.errors.length, 0);
  assert.equal(t.saved.length, 1);
  assert.equal(await t.saved[0].blob.text(), csv.toString());
  assert.equal(t.context.activeFileDownload, null);
});

test('missing header chunk never becomes a headerless download or preview', () => {
  for (const preview of [false, true]) {
    const t = transfer(csv, preview);
    t.chunks.delete(1);
    t.finish();
    assert.match(t.errors[0], /Missing chunk 1/);
    assert.equal(t.saved.length + t.previews.length, 0);
    assert.equal(t.context.lastDownloadedFile, null);
  }
});

test('missing middle or final chunks reject the download', () => {
  for (const i of [2, Math.ceil(csv.length / 96)]) {
    const t = transfer();
    t.chunks.delete(i);
    t.finish();
    assert.match(t.errors[0], /Missing chunk/);
    assert.equal(t.saved.length, 0);
  }
});

test('invalid base64 and short chunks reject the download', () => {
  for (const data of ['%%%invalid%%%', Buffer.from('short').toString('base64')]) {
    const t = transfer();
    t.chunks.set(1, data);
    t.finish();
    assert.equal(t.errors.length, 1);
    assert.equal(t.saved.length, 0);
  }
});

test('mismatched completion size or filename rejects the download', () => {
  const t = transfer();
  t.finish(csv.length + 96);
  assert.match(t.errors[0], /Chunk count/);
  assert.equal(t.saved.length, 0);
  const other = transfer();
  other.context.finishFileDownload('other.csv', csv.length);
  assert.match(other.errors[0], /did not match/);
  assert.equal(other.saved.length, 0);
});

test('complete preview and empty file remain supported', async () => {
  const t = transfer(csv, true);
  t.finish();
  assert.equal(t.errors.length, 0);
  assert.equal(await t.previews[0].blob.text(), csv.toString());
  assert.equal(t.saved.length, 0);
  const empty = transfer(Buffer.alloc(0));
  empty.finish();
  assert.equal(empty.errors.length, 0);
  assert.equal(empty.saved[0].blob.size, 0);
});

const reliableStart = html.indexOf('let lastReliableFile = null;');
const reliableEnd = html.indexOf('function parseFileMessage(', reliableStart);
assert.ok(reliableStart > 0 && reliableEnd > reliableStart);
const reliableSource = html.slice(reliableStart, reliableEnd);

function reliableTransfer(data, preview = false) {
  const t = transfer(data, preview);
  t.chunks.clear();
  const acknowledgments = [];
  const connection = {
    connected: true,
    cmdCh: {writeValue: async bytes => {
      const view = new DataView(bytes.buffer);
      assert.equal(bytes[0], 69);
      acknowledgments.push({token: view.getUint32(1, true), seq: view.getUint32(5, true)});
    }},
  };
  Object.assign(t.context.activeFileDownload, {reliable: true, token: 1234, connection});
  Object.assign(t.context, {setTimeout: () => 1, clearTimeout() {}, updateDownloadProgress() {}});
  vm.runInContext(reliableSource, t.context);
  return {...t, connection, acknowledgments, receive: async (seq, kind = 0, content, token = 1234, from = connection) => {
    const payload = content ?? data.subarray((seq - 1) * 96, seq * 96).toString('base64');
    t.context.parseReliableFileChunk(`$FILECHUNK,${token},${seq},${data.length},${kind},${payload}`, from);
    await connection.fileAckWrites;
  }};
}

test('browser ACK protocol recovers dropped chunks 12 and 15 for download and preview', async () => {
  const data = Buffer.concat(Array.from({length: 12}, () => csv));
  for (const preview of [false, true]) {
    const t = reliableTransfer(data, preview);
    const total = Math.ceil(data.length / 96);
    for (let seq = 1; seq <= total; ++seq) {
      // Model one chunk in flight: a dropped packet produces no ACK, so the
      // sender must repeat that same sequence before advancing the SD reader.
      if (![12, 15].includes(seq)) await t.receive(seq);
      if (!t.acknowledgments.some(ack => ack.seq === seq)) await t.receive(seq);
      assert.ok(t.acknowledgments.some(ack => ack.seq === seq));
    }
    await t.receive(total + 1, 1, 'wardrive-0001.csv');
    assert.equal(t.errors.length, 0);
    const artifacts = preview ? t.previews : t.saved;
    assert.equal(artifacts.length, 1);
    assert.deepEqual(Buffer.from(await artifacts[0].blob.arrayBuffer()), data);
    // Lost completion ACK: repeat completion, but don't save/preview twice.
    await t.receive(total + 1, 1, 'wardrive-0001.csv');
    assert.equal(artifacts.length, 1);
    assert.equal(t.acknowledgments.at(-1).seq, total + 1);
  }
});

test('lost browser ACK retries the same chunk without duplicating its bytes', async () => {
  const t = reliableTransfer(csv);
  const write = t.connection.cmdCh.writeValue;
  t.connection.cmdCh.writeValue = async () => { throw new Error('GATT busy'); };
  await t.receive(1);
  assert.equal(t.acknowledgments.length, 0);
  assert.equal(t.chunks.size, 1);
  t.connection.cmdCh.writeValue = write;
  await t.receive(1);
  assert.equal(t.acknowledgments.length, 1);
  assert.equal(t.chunks.size, 1);
});

test('bad chunk, foreign bridge, stale token, and incomplete completion are not ACKed', async () => {
  const t = reliableTransfer(csv);
  await t.receive(1, 0, '%%%');
  await t.receive(1, 0, undefined, 1234, {connected: true});
  assert.equal(t.acknowledgments.length, 0);
  await t.receive(1);
  await t.receive(2, 0, undefined, 9999);
  await t.receive(Math.ceil(csv.length / 96) + 1, 1, 'wardrive-0001.csv');
  assert.equal(t.acknowledgments.length, 1);
  assert.equal(t.saved.length, 0);
});

test('new request sends its token in little endian and rejects stale first chunks', async () => {
  const t = reliableTransfer(csv);
  const writes = [];
  t.connection.cmdCh.writeValue = async bytes => writes.push(bytes);
  Object.assign(t.context, {TGT_SCREEN: 1, crypto: {getRandomValues: a => {a[0] = 0x12345678;}}});
  t.context.activeFileDownload.index = 7;
  t.context.requestReliableFile(t.context.activeFileDownload);
  await t.connection.fileAckWrites;
  assert.deepEqual(Array.from(writes[0]), [68, 7, 1, 0x78, 0x56, 0x34, 0x12]);
  await t.receive(1, 0, undefined, 1234); // previous transfer, before any current data
  assert.equal(t.chunks.size, 0);
  await t.receive(1, 0, undefined, 0x12345678);
  assert.equal(t.chunks.size, 1);
});

test('reliable transfer handles an empty file and acknowledges terminal errors', async () => {
  const t = reliableTransfer(Buffer.alloc(0));
  await t.receive(1);
  await t.receive(2, 1, 'wardrive-0001.csv');
  assert.equal(t.saved[0].blob.size, 0);
  const failed = reliableTransfer(csv);
  await failed.receive(2, 2, 'Cannot open file');
  assert.match(failed.errors[0], /Cannot open file/);
  assert.equal(failed.acknowledgments[0].seq, 2);
  await failed.receive(2, 2, 'Cannot open file');
  assert.equal(failed.errors.length, 1);
  assert.equal(failed.acknowledgments.length, 2);
});

const resultStart = html.indexOf('function parseResult(dv, c){');
const resultEnd = html.indexOf('// Parse live WiGLE', resultStart);
const fileParserStart = html.indexOf('function parseFileMessage(');
const fileParserEnd = html.indexOf('\nif ($("connect-serial"))', fileParserStart);
assert.ok(resultStart >= 0 && resultEnd > resultStart && fileParserEnd > fileParserStart);

test('full BLE notification parser preserves files around 100 KiB and above 1 MiB', async () => {
  for (const size of [100 * 1024 - 1, 100 * 1024 + 1, 1024 * 1024 + 17]) {
    const data = Buffer.alloc(size);
    for (let i = 0; i < size; ++i) data[i] = (i * 17 + 31) & 255;
    const t = reliableTransfer(data);
    vm.runInContext(html.slice(resultStart, resultEnd) + html.slice(fileParserStart, fileParserEnd), t.context);
    const total = Math.ceil(size / 96);
    const receiveNotification = async (seq, kind, payload) => {
      // The actual source-prefixed notification format, including seq digit
      // transitions at 999/1000 and 9999/10000, routed through both dispatchers.
      const line = `$FILECHUNK,1234,${seq},${size},${kind},${payload}`;
      const bytes = Buffer.concat([Buffer.from([9]), Buffer.from(line)]);
      t.context.parseResult(new DataView(bytes.buffer, bytes.byteOffset, bytes.length), t.connection);
      await t.connection.fileAckWrites;
    };
    for (let seq = 1; seq <= total; ++seq) {
      const payload = data.subarray((seq - 1) * 96, seq * 96).toString('base64');
      if (![12, 15, 1000, 1024, 4096, 10000].includes(seq)) {
        await receiveNotification(seq, 0, payload);
      }
      if (t.acknowledgments.at(-1)?.seq !== seq) await receiveNotification(seq, 0, payload);
      assert.equal(t.acknowledgments.at(-1).seq, seq);
      if (seq % 1000 === 0) await receiveNotification(seq, 0, payload); // lost ACK -> duplicate
    }
    await receiveNotification(total + 1, 1, 'wardrive-0001.csv');
    assert.deepEqual(t.errors, []);
    assert.equal(t.saved.length, 1);
    assert.deepEqual(Buffer.from(await t.saved[0].blob.arrayBuffer()), data);
  }
});
