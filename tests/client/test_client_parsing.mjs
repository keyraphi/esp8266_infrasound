import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const src = fs.readFileSync(path.join(root, "static", "measurement_parsing.js"), "utf8");
new Function(src)();
const P = globalThis.InfrasoundParsing;

let checks = 0;
function test(name, fn) {
  try {
    fn();
    checks += 1;
    console.log(`ok   ${name}`);
  } catch (err) {
    console.log(`FAIL ${name}: ${err.message}`);
    process.exitCode = 1;
  }
}

test("parses a batched SSE message", () => {
  const r = P.parseSseBatch("1000;-0.12345,0.23456,1.00000", 20);
  assert.deepEqual(r.times, [1000, 1020, 1040]);
  assert.equal(r.values.length, 3);
  assert.ok(Math.abs(r.values[0] + 0.12345) < 1e-6);
  assert.ok(Math.abs(r.values[2] - 1.0) < 1e-6);
});

test("parses a single-sample batch", () => {
  const r = P.parseSseBatch("40;2.5", 20);
  assert.deepEqual(r.times, [40]);
  assert.deepEqual(r.values, [2.5]);
});

test("rejects a malformed SSE message", () => {
  assert.deepEqual(P.parseSseBatch("nonsense", 20), { times: [], values: [] });
  assert.deepEqual(P.parseSseBatch("", 20), { times: [], values: [] });
  assert.deepEqual(P.parseSseBatch("1000;", 20), { times: [], values: [] });
});

test("rejects a batch containing a non-numeric value", () => {
  assert.deepEqual(P.parseSseBatch("1000;1.0,bogus,2.0", 20), { times: [], values: [] });
});

test("rejects a batch with a trailing comma", () => {
  assert.deepEqual(P.parseSseBatch("1000;1.0,", 20), { times: [], values: [] });
});

test("parses a v1 file header", () => {
  const buf = new ArrayBuffer(32);
  const dv = new DataView(buf);
  const magic = "INFRASND";
  for (let i = 0; i < 8; i += 1) dv.setUint8(i, magic.charCodeAt(i));
  dv.setUint16(8, 1, true);
  dv.setUint16(10, 8, true);
  dv.setUint32(12, 50, true);
  dv.setBigUint64(16, 1785926400123n, true);

  const h = P.parseFileHeader(dv);
  assert.equal(h.legacy, false);
  assert.equal(h.version, 1);
  assert.equal(h.recSize, 8);
  assert.equal(h.rateHz, 50);
  assert.equal(h.epoch0Ms, 1785926400123);
});

test("detects a legacy headerless file", () => {
  const dv = new DataView(new ArrayBuffer(32));
  const h = P.parseFileHeader(dv);
  assert.equal(h.legacy, true);
  assert.equal(h.rateHz, 50);
  assert.equal(h.epoch0Ms, 0);
});

test("parses interleaved uint32/float32 records", () => {
  const buf = new ArrayBuffer(16);
  const dv = new DataView(buf);
  dv.setUint32(0, 0, true);
  dv.setFloat32(4, 1.5, true);
  dv.setUint32(8, 20, true);
  dv.setFloat32(12, -2.5, true);

  const r = P.parseRecords(dv, 0, 2);
  assert.deepEqual(r.times, [0, 20]);
  assert.deepEqual(r.values, [1.5, -2.5]);
});

test("parses legacy bare-float records at 50 Hz", () => {
  const buf = new ArrayBuffer(12);
  const dv = new DataView(buf);
  dv.setFloat32(0, 1.5, true);
  dv.setFloat32(4, -2.5, true);
  dv.setFloat32(8, 0.25, true);
  const r = P.parseLegacyRecords(dv, 0, 3);
  assert.deepEqual(r.times, [0, 20, 40]);
  assert.deepEqual(r.values, [1.5, -2.5, 0.25]);
});

test("legacy parsing stops at a truncated trailing record", () => {
  const buf = new ArrayBuffer(6);          // one whole float plus 2 stray bytes
  const dv = new DataView(buf);
  dv.setFloat32(0, 3.5, true);
  const r = P.parseLegacyRecords(dv, 0, 2);
  assert.deepEqual(r.times, [0]);
  assert.deepEqual(r.values, [3.5]);
});

console.log(`\n${checks} tests passed`);
