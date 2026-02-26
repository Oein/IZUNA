/**
 * Parse uint64 little-endian from a Uint8Array at the given offset.
 */
export function parseUint64LE(data: Uint8Array, offset: number): bigint {
  const view = new DataView(data.buffer, data.byteOffset + offset, 8);
  const low = view.getUint32(0, true);
  const high = view.getUint32(4, true);
  return (BigInt(high) << 32n) | BigInt(low);
}

/**
 * Parse float64 little-endian from a Uint8Array at the given offset.
 */
export function parseFloat64LE(data: Uint8Array, offset: number): number {
  const view = new DataView(data.buffer, data.byteOffset + offset, 8);
  return view.getFloat64(0, true);
}

/**
 * Parse a 4-byte little-endian uint32 from a Uint8Array.
 */
export function parseUint32LE(data: Uint8Array, offset: number): number {
  return (
    data[offset] |
    (data[offset + 1] << 8) |
    (data[offset + 2] << 16) |
    (data[offset + 3] << 24)
  );
}

/**
 * Build a page-read command payload: [0x01, pageLE(4 bytes)]
 */
export function buildPageCommand(page: number): Uint8Array {
  const p = page >>> 0;
  const payload = new Uint8Array(5);
  payload[0] = 0x01;
  payload[1] = p & 0xff;
  payload[2] = (p >>> 8) & 0xff;
  payload[3] = (p >>> 16) & 0xff;
  payload[4] = (p >>> 24) & 0xff;
  return payload;
}
