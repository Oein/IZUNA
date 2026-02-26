export function formatKstFromCompact(compactBigInt: bigint): string {
  const s = compactBigInt.toString();
  if (compactBigInt <= 0n || s.length !== 14) {
    return '\u2014';
  }
  const y = s.slice(0, 4);
  const mo = s.slice(4, 6);
  const d = s.slice(6, 8);
  const h = s.slice(8, 10);
  const mi = s.slice(10, 12);
  const sec = s.slice(12, 14);
  return `${y}-${mo}-${d} ${h}:${mi}:${sec}`;
}

export function formatBytes(bytes: number): string {
  return bytes.toLocaleString() + ' bytes';
}

export function timeStamp(): string {
  return new Date().toLocaleTimeString();
}
