import ReactNativeBlobUtil from 'react-native-blob-util';
import Share from 'react-native-share';
import {parseUint64LE, parseFloat64LE} from './BinaryParser';
import {formatKstFromCompact} from '../utils/formatting';

const RECORD_SIZE = 24;

/**
 * Save binary data as .bin file in the cache directory.
 */
export async function saveBinFile(
  data: Uint8Array,
  filename: string = 'location.bin',
): Promise<string> {
  const path = `${ReactNativeBlobUtil.fs.dirs.CacheDir}/${filename}`;
  // Convert Uint8Array to base64 for writing
  const {Buffer} = require('buffer');
  const base64 = Buffer.from(data).toString('base64');
  await ReactNativeBlobUtil.fs.writeFile(path, base64, 'base64');
  return path;
}

/**
 * Export records as CSV, returns the file path.
 */
export async function exportCsv(
  pageData: Uint8Array,
  currentPage: number,
  viewSize: number,
): Promise<string> {
  const totalRecords = Math.floor(pageData.byteLength / RECORD_SIZE);
  const rows = ['KST Time,Latitude,Longitude'];

  for (let i = 0; i < totalRecords; i++) {
    const offset = i * RECORD_SIZE;
    const recordBytes = pageData.slice(offset, offset + RECORD_SIZE);
    const compactTime = parseUint64LE(recordBytes, 0);
    const lat = parseFloat64LE(recordBytes, 8);
    const lon = parseFloat64LE(recordBytes, 16);

    if (compactTime === 0n && lat === 0 && lon === 0) {
      continue;
    }
    rows.push(
      `${formatKstFromCompact(compactTime)},${lat.toFixed(6)},${lon.toFixed(6)}`,
    );
  }

  const step = viewSize / 10;
  const filename = `pages_${currentPage}-${currentPage + step - 1}.csv`;
  const path = `${ReactNativeBlobUtil.fs.dirs.CacheDir}/${filename}`;
  await ReactNativeBlobUtil.fs.writeFile(path, rows.join('\n'), 'utf8');
  return path;
}

/**
 * Share a file via the system share sheet.
 */
export async function shareFile(
  filePath: string,
  mimeType: string = 'application/octet-stream',
): Promise<void> {
  await Share.open({
    url: `file://${filePath}`,
    type: mimeType,
  });
}
