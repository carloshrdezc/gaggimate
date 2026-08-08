// Minimal STORE-method (uncompressed) ZIP writer.
//
// Beanconqueror's own exporter disables compression on iOS because compressed
// archives came out corrupt there (see uiExportImportHelper.buildExportZIP at
// upstream b713c3e), so an uncompressed archive is a shape its importer already
// handles. Writing the ~60 lines of STORE here avoids pulling a zip dependency
// into the ESP32-hosted bundle, whose asset budget is tight (see the SPIFFS
// name-length note in vite.config.js).

const LOCAL_FILE_HEADER_SIGNATURE = 0x04034b50;
const CENTRAL_DIRECTORY_SIGNATURE = 0x02014b50;
const END_OF_CENTRAL_DIRECTORY_SIGNATURE = 0x06054b50;
// Version 2.0 / "made by" UNIX-agnostic value; matches what common writers emit.
const VERSION_NEEDED = 20;
// Bit 11: file name is UTF-8. Beanconqueror entry names are ASCII, but bean and
// note text is not, and the flag costs nothing.
const FLAG_UTF8 = 0x0800;
const METHOD_STORE = 0;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let value = i;
    for (let bit = 0; bit < 8; bit++) {
      value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    }
    table[i] = value >>> 0;
  }
  return table;
})();

function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) {
    crc = CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/**
 * Builds an uncompressed ZIP archive.
 *
 * @param {Record<string, string>} entries entry name -> UTF-8 text content.
 *   Insertion order is preserved, so the caller controls entry ordering.
 * @returns {Blob} `application/zip` blob
 */
export function createStoreZip(entries) {
  const encoder = new TextEncoder();
  const parts = [];
  const central = [];
  let offset = 0;

  for (const [name, content] of Object.entries(entries)) {
    const nameBytes = encoder.encode(name);
    const data = encoder.encode(content);
    const checksum = crc32(data);

    const localHeader = new Uint8Array(30 + nameBytes.length);
    const localView = new DataView(localHeader.buffer);
    localView.setUint32(0, LOCAL_FILE_HEADER_SIGNATURE, true);
    localView.setUint16(4, VERSION_NEEDED, true);
    localView.setUint16(6, FLAG_UTF8, true);
    localView.setUint16(8, METHOD_STORE, true);
    // Fixed DOS timestamp (1980-01-01 00:00) so the archive bytes are a pure
    // function of its contents — required for the deterministic-output tests.
    localView.setUint16(10, 0, true);
    localView.setUint16(12, 0x0021, true);
    localView.setUint32(14, checksum, true);
    localView.setUint32(18, data.length, true);
    localView.setUint32(22, data.length, true);
    localView.setUint16(26, nameBytes.length, true);
    localView.setUint16(28, 0, true);
    localHeader.set(nameBytes, 30);

    parts.push(localHeader, data);

    const centralHeader = new Uint8Array(46 + nameBytes.length);
    const centralView = new DataView(centralHeader.buffer);
    centralView.setUint32(0, CENTRAL_DIRECTORY_SIGNATURE, true);
    centralView.setUint16(4, VERSION_NEEDED, true);
    centralView.setUint16(6, VERSION_NEEDED, true);
    centralView.setUint16(8, FLAG_UTF8, true);
    centralView.setUint16(10, METHOD_STORE, true);
    centralView.setUint16(12, 0, true);
    centralView.setUint16(14, 0x0021, true);
    centralView.setUint32(16, checksum, true);
    centralView.setUint32(20, data.length, true);
    centralView.setUint32(24, data.length, true);
    centralView.setUint16(28, nameBytes.length, true);
    centralView.setUint16(30, 0, true);
    centralView.setUint16(32, 0, true);
    centralView.setUint16(34, 0, true);
    centralView.setUint16(36, 0, true);
    centralView.setUint32(38, 0, true);
    centralView.setUint32(42, offset, true);
    centralHeader.set(nameBytes, 46);
    central.push(centralHeader);

    offset += localHeader.length + data.length;
  }

  const centralSize = central.reduce((total, header) => total + header.length, 0);
  const end = new Uint8Array(22);
  const endView = new DataView(end.buffer);
  endView.setUint32(0, END_OF_CENTRAL_DIRECTORY_SIGNATURE, true);
  endView.setUint16(4, 0, true);
  endView.setUint16(6, 0, true);
  endView.setUint16(8, central.length, true);
  endView.setUint16(10, central.length, true);
  endView.setUint32(12, centralSize, true);
  endView.setUint32(16, offset, true);
  endView.setUint16(20, 0, true);

  return new Blob([...parts, ...central, end], { type: 'application/zip' });
}
