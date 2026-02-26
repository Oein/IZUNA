export interface GpsRecord {
  compactTime: bigint;
  lat: number;
  lon: number;
}

export type BleState = 'disconnected' | 'scanning' | 'connecting' | 'connected';

export interface FileStats {
  fileSize: number;
  totalPages: number;
}

export interface DownloadProgress {
  current: number;
  total: number;
  percent: number;
  visible: boolean;
}

export interface LiveGps {
  compactTime: string;
  kst: string;
  lat: string;
  lon: string;
}

export interface PageViewState {
  records: GpsRecord[];
  currentPage: number;
  viewSize: number;
  totalPages: number;
  lastPageData: Uint8Array | null;
}
