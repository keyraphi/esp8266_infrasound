// Parsing helpers shared by the live view and the analysis page.
// Kept free of DOM access so it can be unit tested under Node.
(function () {
  const MF_HEADER_SIZE = 32;
  const MF_RECORD_SIZE = 8;
  const MF_MAGIC = "INFRASND";

  // "t0;v0,v1,..." — samples in one message are contiguous, because the
  // server flushes a batch whenever it detects a gap.
  function parseSseBatch(data, samplePeriodMs) {
    const empty = { times: [], values: [] };
    if (typeof data !== "string") return empty;

    const semi = data.indexOf(";");
    if (semi <= 0) return empty;

    const t0 = Number.parseInt(data.slice(0, semi), 10);
    if (!Number.isFinite(t0)) return empty;

    const body = data.slice(semi + 1);
    if (body.length === 0) return empty;

    const parts = body.split(",");
    const times = [];
    const values = [];
    for (let i = 0; i < parts.length; i += 1) {
      const v = Number.parseFloat(parts[i]);
      if (!Number.isFinite(v)) return empty;
      times.push(t0 + i * samplePeriodMs);
      values.push(v);
    }
    return { times: times, values: values };
  }

  function parseFileHeader(dv) {
    let magic = "";
    for (let i = 0; i < 8; i += 1) magic += String.fromCharCode(dv.getUint8(i));
    if (magic !== MF_MAGIC) {
      // Legacy recording: bare 4-byte floats, no header, uniform 50 Hz.
      return { legacy: true, version: 0, recSize: 4, rateHz: 50, epoch0Ms: 0 };
    }
    return {
      legacy: false,
      version: dv.getUint16(8, true),
      recSize: dv.getUint16(10, true),
      rateHz: dv.getUint32(12, true),
      epoch0Ms: Number(dv.getBigUint64(16, true)),
    };
  }

  function parseRecords(dv, byteOffset, count) {
    const times = [];
    const values = [];
    for (let i = 0; i < count; i += 1) {
      const at = byteOffset + i * MF_RECORD_SIZE;
      if (at + MF_RECORD_SIZE > dv.byteLength) break;
      times.push(dv.getUint32(at, true));
      values.push(dv.getFloat32(at + 4, true));
    }
    return { times: times, values: values };
  }

  function parseLegacyRecords(dv, byteOffset, count) {
    const times = [];
    const values = [];
    for (let i = 0; i < count; i += 1) {
      const at = byteOffset + i * 4;
      if (at + 4 > dv.byteLength) break;
      times.push(i * 20);
      values.push(dv.getFloat32(at, true));
    }
    return { times: times, values: values };
  }

  globalThis.InfrasoundParsing = {
    MF_HEADER_SIZE: MF_HEADER_SIZE,
    MF_RECORD_SIZE: MF_RECORD_SIZE,
    parseSseBatch: parseSseBatch,
    parseFileHeader: parseFileHeader,
    parseRecords: parseRecords,
    parseLegacyRecords: parseLegacyRecords,
  };
})();
