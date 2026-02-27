import {useState, useEffect, useRef, useCallback} from 'react';
import {Alert} from 'react-native';
import {bleService} from '../services/BleService';
import {parseUint64LE, parseFloat64LE, buildPageCommand} from '../services/BinaryParser';
import {saveBinFile, exportCsv, shareFile} from '../services/FileService';
import {formatKstFromCompact, timeStamp} from '../utils/formatting';
import type {
  BleState,
  LiveGps,
  FileStats,
  DownloadProgress,
  GpsRecord,
} from '../types';

const RECORD_SIZE = 24;
const DEBOUNCE_MS = 300;

export function useBle() {
  const [bleState, setBleState] = useState<BleState>('disconnected');
  const [deviceName, setDeviceName] = useState('');
  const [liveGps, setLiveGps] = useState<LiveGps>({
    compactTime: '\u2014',
    kst: '\u2014',
    lat: '\u2014',
    lon: '\u2014',
  });
  const [fileStats, setFileStats] = useState<FileStats>({
    fileSize: 0,
    totalPages: 0,
  });
  const [downloadProgress, setDownloadProgress] = useState<DownloadProgress>({
    current: 0,
    total: 0,
    percent: 0,
    visible: false,
  });
  const [records, setRecords] = useState<GpsRecord[]>([]);
  const [currentPage, setCurrentPage] = useState(0);
  const [viewSize, setViewSize] = useState(10);
  const [lastPageData, setLastPageData] = useState<Uint8Array | null>(null);
  const [logMessages, setLogMessages] = useState<string[]>([]);

  const isDownloadingRef = useRef(false);
  const pollTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const currentPageRef = useRef(0);
  const totalPagesRef = useRef(0);
  const fileSizeRef = useRef(0);
  const viewSizeRef = useRef(10);

  // Page cache: pageIndex -> Uint8Array(240). Last page is never cached.
  const pageCacheRef = useRef<Map<number, Uint8Array>>(new Map());

  // Debounce timer for page navigation / view size changes
  const debounceTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const loadingRef = useRef(false);

  // Keep refs in sync
  useEffect(() => {
    currentPageRef.current = currentPage;
  }, [currentPage]);
  useEffect(() => {
    totalPagesRef.current = fileStats.totalPages;
    fileSizeRef.current = fileStats.fileSize;
  }, [fileStats]);
  useEffect(() => {
    viewSizeRef.current = viewSize;
  }, [viewSize]);

  const log = useCallback((msg: string) => {
    const line = `[${timeStamp()}] ${msg}`;
    setLogMessages(prev => [...prev, line]);
  }, []);

  const clearLog = useCallback(() => {
    setLogMessages([]);
  }, []);

  // Setup BLE callbacks
  useEffect(() => {
    bleService.onLiveGps = (data: Uint8Array) => {
      if (data.length !== 24) {
        return;
      }
      const compactTime = parseUint64LE(data, 0);
      const lat = parseFloat64LE(data, 8);
      const lon = parseFloat64LE(data, 16);

      setLiveGps({
        compactTime: compactTime.toString(),
        kst: formatKstFromCompact(compactTime),
        lat: lat.toFixed(6),
        lon: lon.toFixed(6),
      });
    };

    bleService.onDisconnect = () => {
      stopPolling();
      setBleState('disconnected');
      setDeviceName('');
      log('Disconnected');
    };

    bleService.onLog = (msg: string) => {
      log(msg);
    };

    return () => {
      bleService.onLiveGps = null;
      bleService.onDisconnect = null;
      bleService.onLog = null;
    };
  }, [log]);

  const updatePageStats = useCallback(
    (size: number) => {
      const s = size >>> 0;
      const pages = Math.ceil(s / 240);
      setFileStats({fileSize: s, totalPages: pages});
    },
    [],
  );

  const parsePageRecords = useCallback(
    (pageData: Uint8Array): GpsRecord[] => {
      const totalRecords = Math.floor(pageData.byteLength / RECORD_SIZE);
      const result: GpsRecord[] = [];
      for (let i = 0; i < totalRecords; i++) {
        const offset = i * RECORD_SIZE;
        const recordBytes = pageData.slice(offset, offset + RECORD_SIZE);
        const compactTime = parseUint64LE(recordBytes, 0);
        const lat = parseFloat64LE(recordBytes, 8);
        const lon = parseFloat64LE(recordBytes, 16);
        result.push({compactTime, lat, lon});
      }
      return result;
    },
    [],
  );

  const fetchOnePage = useCallback(
    async (page: number): Promise<Uint8Array> => {
      const tp = totalPagesRef.current;
      const isLastPage = tp > 0 && page === tp - 1;

      // Use cache for non-last pages
      if (!isLastPage) {
        const cached = pageCacheRef.current.get(page);
        if (cached) {
          return cached;
        }
      }

      const payload = buildPageCommand(page);
      const result = await bleService.sendCommand(payload, 0x01, 5000);
      if (result.type !== 'page') {
        throw new Error('Unexpected response type');
      }

      // Cache non-last pages (append-only data → completed pages are immutable)
      if (!isLastPage) {
        pageCacheRef.current.set(page, result.pageData);
      }

      return result.pageData;
    },
    [],
  );

  const loadPage = useCallback(
    async (startPage: number) => {
      startPage = startPage >>> 0;
      const blePagesNeeded = viewSizeRef.current / 10;
      const tp = totalPagesRef.current;
      const maxPage = tp > 0 ? tp : startPage + blePagesNeeded;
      const endPage = Math.min(startPage + blePagesNeeded, maxPage);
      const count = endPage - startPage;

      const combined = new Uint8Array(count * 240);
      for (let i = 0; i < count; i++) {
        const data = await fetchOnePage(startPage + i);
        combined.set(data, i * 240);
      }

      setCurrentPage(startPage);
      setLastPageData(new Uint8Array(combined));
      setRecords(parsePageRecords(combined));

      const cached = pageCacheRef.current.size;
      log(`loaded pages ${startPage}-${endPage - 1} (${count * 10} records, cache: ${cached})`);
    },
    [fetchOnePage, parsePageRecords, log],
  );

  // Debounced page load: cancels previous pending request
  const debouncedLoadPage = useCallback(
    (startPage: number) => {
      if (debounceTimerRef.current) {
        clearTimeout(debounceTimerRef.current);
      }
      debounceTimerRef.current = setTimeout(async () => {
        debounceTimerRef.current = null;
        if (loadingRef.current) {
          return;
        }
        loadingRef.current = true;
        try {
          await loadPage(startPage);
        } catch (err: any) {
          log(`page cmd failed: ${err.message}`);
        } finally {
          loadingRef.current = false;
        }
      }, DEBOUNCE_MS);
    },
    [loadPage, log],
  );

  const clearPageCache = useCallback(() => {
    pageCacheRef.current.clear();
  }, []);

  // Polling
  const startPolling = useCallback(() => {
    stopPolling();
    pollTimerRef.current = setInterval(async () => {
      if (!bleService.isConnected || isDownloadingRef.current) {
        return;
      }
      try {
        const result = await bleService.sendCommand(
          new Uint8Array([0x00]),
          0x00,
        );
        if (result.type !== 'size') {
          return;
        }
        const newSize = result.size >>> 0;
        if (newSize === fileSizeRef.current) {
          return;
        }

        const oldSize = fileSizeRef.current;
        const oldTotalPages = totalPagesRef.current;
        const wasOnLastPage =
          currentPageRef.current >=
          (oldTotalPages > 0 ? oldTotalPages - 1 : 0);

        updatePageStats(newSize);

        const newTotalPages = Math.ceil(newSize / 240);

        if (wasOnLastPage && newTotalPages > 0) {
          const lastPage = newTotalPages - 1;
          await loadPage(lastPage);
          log(
            `auto-refresh: ${oldSize} -> ${newSize} bytes, showing page ${lastPage}`,
          );
        } else {
          log(
            `auto-refresh: ${oldSize} -> ${newSize} bytes (staying on page ${currentPageRef.current})`,
          );
        }
      } catch {
        // Silently ignore poll errors
      }
    }, 10000);
  }, [updatePageStats, loadPage, log]);

  const stopPolling = useCallback(() => {
    if (pollTimerRef.current) {
      clearInterval(pollTimerRef.current);
      pollTimerRef.current = null;
    }
  }, []);

  // Connect
  const connectDevice = useCallback(async () => {
    try {
      setBleState('scanning');
      const name = await bleService.connect();
      setBleState('connected');
      setDeviceName(name);

      // Auto-load file size and last page
      try {
        const sizeResult = await bleService.sendCommand(
          new Uint8Array([0x00]),
          0x00,
        );
        if (sizeResult.type === 'size') {
          updatePageStats(sizeResult.size);
          const tp = Math.ceil(sizeResult.size / 240);
          if (tp > 0) {
            await loadPage(tp - 1);
          }
        }
      } catch (e: any) {
        log(`auto-load failed: ${e.message}`);
      }

      startPolling();
    } catch (err: any) {
      setBleState('disconnected');
      log(`Connect failed: ${err.message}`);
    }
  }, [updatePageStats, loadPage, startPolling, log]);

  // Disconnect
  const disconnectDevice = useCallback(async () => {
    stopPolling();
    await bleService.disconnect();
    setBleState('disconnected');
    setDeviceName('');
    setLiveGps({compactTime: '\u2014', kst: '\u2014', lat: '\u2014', lon: '\u2014'});
    setFileStats({fileSize: 0, totalPages: 0});
    setCurrentPage(0);
    setLastPageData(null);
    setRecords([]);
    setDownloadProgress({current: 0, total: 0, percent: 0, visible: false});
  }, [stopPolling]);

  // Get file size
  const getFileSize = useCallback(async () => {
    try {
      const result = await bleService.sendCommand(
        new Uint8Array([0x00]),
        0x00,
      );
      if (result.type === 'size') {
        updatePageStats(result.size);
        log(`size=${result.size}`);
      }
    } catch (err: any) {
      log(`size cmd failed: ${err.message}`);
    }
  }, [updatePageStats, log]);

  // Download all
  const downloadAll = useCallback(async () => {
    try {
      isDownloadingRef.current = true;

      const sizeResp = await bleService.sendCommand(
        new Uint8Array([0x00]),
        0x00,
      );
      if (sizeResp.type !== 'size') {
        return;
      }
      const fileSize = sizeResp.size >>> 0;
      updatePageStats(fileSize);

      if (fileSize === 0) {
        const path = await saveBinFile(new Uint8Array(0));
        await shareFile(path);
        log('download complete: location.bin (0 bytes)');
        return;
      }

      const totalPages = Math.ceil(fileSize / 240);
      const allData = new Uint8Array(totalPages * 240);

      setDownloadProgress({current: 0, total: totalPages, percent: 0, visible: true});

      for (let page = 0; page < totalPages; page++) {
        // Use cache for non-last pages during download too
        const isLast = page === totalPages - 1;
        const cached = !isLast ? pageCacheRef.current.get(page) : undefined;

        let pageData: Uint8Array;
        if (cached) {
          pageData = cached;
        } else {
          const payload = buildPageCommand(page);
          const result = await bleService.sendCommand(payload, 0x01, 7000);
          if (result.type !== 'page') {
            continue;
          }
          pageData = result.pageData;
          // Cache non-last pages
          if (!isLast) {
            pageCacheRef.current.set(page, pageData);
          }
        }

        allData.set(pageData, page * 240);

        // Live table update
        const step = viewSizeRef.current / 10;
        const viewStart = Math.floor(page / step) * step;
        const viewEnd = Math.min(viewStart + step, page + 1);
        const sliced = allData.slice(viewStart * 240, viewEnd * 240);
        setCurrentPage(viewStart);
        setLastPageData(new Uint8Array(sliced));
        setRecords(parsePageRecords(sliced));

        const pct = ((page + 1) / totalPages) * 100;
        setDownloadProgress({
          current: page + 1,
          total: totalPages,
          percent: pct,
          visible: true,
        });

        if (page % 32 === 0 || page === totalPages - 1) {
          log(`download progress: ${page + 1}/${totalPages}`);
        }
      }

      const trimmed = allData.slice(0, fileSize);
      const path = await saveBinFile(trimmed);
      await shareFile(path);
      log(`download complete: location.bin (${fileSize} bytes), cache: ${pageCacheRef.current.size} pages`);

      setTimeout(() => {
        setDownloadProgress(prev => ({...prev, visible: false}));
      }, 3000);
    } catch (err: any) {
      log(`download failed: ${err.message}`);
    } finally {
      isDownloadingRef.current = false;
    }
  }, [updatePageStats, parsePageRecords, log]);

  // Read device log page (0x03)
  const readLog = useCallback(
    async (page: number) => {
      try {
        const p = Math.max(0, Math.min(15, page)) & 0xff;
        const payload = new Uint8Array([0x03, p]);
        const result = await bleService.sendCommand(payload, 0x03, 5000);
        if (result.type === 'log') {
          const text = new TextDecoder()
            .decode(result.logData)
            .replace(/\0+$/, '');
          log(`[log p${p}] ${text}`);
        }
      } catch (err: any) {
        log(`log cmd failed: ${err.message}`);
      }
    },
    [log],
  );

  // Delete all data
  const deleteAllData = useCallback(async () => {
    Alert.alert('Delete All Data', '정말 모든 데이터를 삭제하시겠습니까?', [
      {text: 'Cancel', style: 'cancel'},
      {
        text: 'Delete',
        style: 'destructive',
        onPress: async () => {
          try {
            const result = await bleService.sendCommand(
              new Uint8Array([0x02]),
              0x02,
            );
            if (result.type === 'delete' && result.success) {
              clearPageCache();
              updatePageStats(0);
              setCurrentPage(0);
              setLastPageData(null);
              setRecords([]);
              log('delete=success (cache cleared)');
            } else {
              log('delete=fail');
            }
          } catch (err: any) {
            log(`delete cmd failed: ${err.message}`);
          }
        },
      },
    ]);
  }, [updatePageStats, clearPageCache, log]);

  // Debounced page navigation
  const goToPage = useCallback(
    (page: number) => {
      debouncedLoadPage(page);
    },
    [debouncedLoadPage],
  );

  const goFirst = useCallback(() => goToPage(0), [goToPage]);

  const goPrev = useCallback(() => {
    const step = viewSizeRef.current / 10;
    goToPage(Math.max(0, currentPageRef.current - step));
  }, [goToPage]);

  const goNext = useCallback(() => {
    const step = viewSizeRef.current / 10;
    goToPage(currentPageRef.current + step);
  }, [goToPage]);

  const goLast = useCallback(() => {
    const tp = totalPagesRef.current;
    if (tp <= 0) {
      return;
    }
    const step = viewSizeRef.current / 10;
    const lastStart = Math.max(0, Math.floor((tp - 1) / step) * step);
    goToPage(lastStart);
  }, [goToPage]);

  // Debounced view size change
  const changeViewSize = useCallback(
    (newSize: number) => {
      newSize = Math.max(10, Math.round(newSize / 10) * 10);
      if (newSize === viewSizeRef.current) {
        return;
      }
      setViewSize(newSize);
      viewSizeRef.current = newSize;

      if (lastPageData && bleService.isConnected) {
        const step = newSize / 10;
        const alignedStart =
          Math.floor(currentPageRef.current / step) * step;
        debouncedLoadPage(alignedStart);
      }
    },
    [lastPageData, debouncedLoadPage],
  );

  // Export CSV
  const doExportCsv = useCallback(async () => {
    if (!lastPageData) {
      return;
    }
    try {
      const path = await exportCsv(lastPageData, currentPage, viewSize);
      await shareFile(path, 'text/csv');
      const totalRecords = Math.floor(lastPageData.byteLength / RECORD_SIZE);
      log(`exported ${totalRecords} records as CSV`);
    } catch (err: any) {
      log(`CSV export failed: ${err.message}`);
    }
  }, [lastPageData, currentPage, viewSize, log]);

  // Cleanup
  useEffect(() => {
    return () => {
      stopPolling();
      if (debounceTimerRef.current) {
        clearTimeout(debounceTimerRef.current);
      }
    };
  }, [stopPolling]);

  return {
    bleState,
    deviceName,
    liveGps,
    fileStats,
    downloadProgress,
    records,
    currentPage,
    viewSize,
    lastPageData,
    logMessages,
    connectDevice,
    disconnectDevice,
    getFileSize,
    downloadAll,
    deleteAllData,
    readLog,
    goToPage,
    goFirst,
    goPrev,
    goNext,
    goLast,
    changeViewSize,
    doExportCsv,
    clearLog,
    log,
  };
}
