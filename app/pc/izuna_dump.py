# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22"]
# ///
"""
IZUNA (Georecord) 장치에서 BLE로 전체 위치 기록을 받아 PC에 저장합니다.

출력:
  location.bin  - 장치의 원본 파일 (24바이트 레코드: uint64 KST compact time, f64 lat, f64 lon)
  location.csv  - KST Time,Latitude,Longitude
  location.gpx  - 사진 지오태깅(Lightroom, ExifTool 등)용 트랙 (UTC)

사용 예:
  uv run izuna_dump.py                 # 스캔 후 첫 번째 장치에서 전체 다운로드
  uv run izuna_dump.py -o ./dump       # 출력 폴더 지정
  uv run izuna_dump.py --logs          # 장치 로그도 함께 저장
  uv run izuna_dump.py --delete        # 다운로드 검증 후 장치 데이터 삭제
  uv run izuna_dump.py --from-bin location.bin   # 이미 받은 .bin을 CSV/GPX로 변환
"""

import argparse
import asyncio
import struct
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

SERVICE_UUID = "000000ff-0000-1000-8000-00805f9b34fb"
DATA_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
CMD_CHAR_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"

PAGE_SIZE = 240
RECORD_SIZE = 24
LOG_PAGE_COUNT = 16  # esp32/main/log_buffer.h 의 LOG_PAGE_COUNT 와 맞출 것
GATT_COOLDOWN_S = 0.08

CMD_SIZE = 0x00
CMD_PAGE = 0x01
CMD_DELETE = 0x02
CMD_LOG = 0x03

EXPECTED_LEN = {CMD_SIZE: 4, CMD_PAGE: PAGE_SIZE, CMD_DELETE: 1, CMD_LOG: PAGE_SIZE}

KST = timezone(timedelta(hours=9))


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Record parsing / export
# ---------------------------------------------------------------------------


def parse_records(data: bytes):
    """(datetime KST | None, lat, lon) 리스트. 0으로 채워진 빈 레코드는 제외."""
    records = []
    for off in range(0, len(data) - RECORD_SIZE + 1, RECORD_SIZE):
        compact, lat, lon = struct.unpack_from("<Qdd", data, off)
        if compact == 0 and lat == 0 and lon == 0:
            continue
        records.append((compact_to_datetime(compact), lat, lon))
    return records


def compact_to_datetime(compact: int):
    s = str(compact)
    if len(s) != 14:
        return None
    try:
        return datetime.strptime(s, "%Y%m%d%H%M%S").replace(tzinfo=KST)
    except ValueError:
        return None


def write_csv(records, path: Path) -> None:
    rows = ["KST Time,Latitude,Longitude"]
    for dt, lat, lon in records:
        t = dt.strftime("%Y-%m-%d %H:%M:%S") if dt else "—"
        rows.append(f"{t},{lat:.6f},{lon:.6f}")
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def write_gpx(records, path: Path) -> None:
    pts = []
    for dt, lat, lon in records:
        if dt is None:
            continue
        utc = dt.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        pts.append(f'      <trkpt lat="{lat:.7f}" lon="{lon:.7f}"><time>{utc}</time></trkpt>')
    gpx = "\n".join(
        [
            '<?xml version="1.0" encoding="UTF-8"?>',
            '<gpx version="1.1" creator="IZUNA" xmlns="http://www.topografix.com/GPX/1/1">',
            "  <trk>",
            "    <name>IZUNA</name>",
            "    <trkseg>",
            *pts,
            "    </trkseg>",
            "  </trk>",
            "</gpx>",
            "",
        ]
    )
    path.write_text(gpx, encoding="utf-8")


def export_all(data: bytes, out_dir: Path) -> None:
    records = parse_records(data)
    write_csv(records, out_dir / "location.csv")
    write_gpx(records, out_dir / "location.gpx")
    log(f"records: {len(records)}")
    if records:
        times = [r[0] for r in records if r[0]]
        if times:
            log(f"range (KST): {min(times):%Y-%m-%d %H:%M:%S} ~ {max(times):%Y-%m-%d %H:%M:%S}")
    log(f"saved: {out_dir / 'location.csv'}")
    log(f"saved: {out_dir / 'location.gpx'}")


# ---------------------------------------------------------------------------
# BLE client
# ---------------------------------------------------------------------------


class Georecord:
    def __init__(self, client):
        self.client = client
        self.pending: tuple[int, asyncio.Future] | None = None

    async def start(self) -> None:
        await self.client.start_notify(CMD_CHAR_UUID, self._on_cmd)

    def _on_cmd(self, _sender, data: bytearray) -> None:
        if self.pending is None:
            log(f"FF02 notify (unmatched): len={len(data)}")
            return
        cmd, fut = self.pending
        self.pending = None
        if fut.done():
            return
        if len(data) == EXPECTED_LEN[cmd]:
            fut.set_result(bytes(data))
        else:
            fut.set_exception(RuntimeError(f"Unexpected response: cmd=0x{cmd:02x}, len={len(data)}"))

    async def command(self, payload: bytes, timeout: float = 3.0, retries: int = 2) -> bytes:
        cmd = payload[0]
        last_err: Exception | None = None
        for attempt in range(retries + 1):
            fut = asyncio.get_running_loop().create_future()
            self.pending = (cmd, fut)
            try:
                await self.client.write_gatt_char(CMD_CHAR_UUID, payload, response=True)
                return await asyncio.wait_for(fut, timeout)
            except (asyncio.TimeoutError, RuntimeError, OSError) as e:
                last_err = e
                self.pending = None
                if attempt < retries:
                    log(f"command 0x{cmd:02x} failed ({e!r}), retry {attempt + 1}/{retries}...")
                    await asyncio.sleep(0.15 * (attempt + 1))
            finally:
                await asyncio.sleep(GATT_COOLDOWN_S)
        raise RuntimeError(f"command 0x{cmd:02x} failed: {last_err!r}")

    async def file_size(self) -> int:
        return struct.unpack("<I", await self.command(bytes([CMD_SIZE])))[0]

    async def read_page(self, page: int) -> bytes:
        return await self.command(bytes([CMD_PAGE]) + struct.pack("<I", page), timeout=7.0)

    async def read_log_page(self, page: int) -> bytes:
        return await self.command(bytes([CMD_LOG, page]), timeout=5.0)

    async def delete_all(self) -> bool:
        return (await self.command(bytes([CMD_DELETE]), timeout=5.0))[0] == 0x01


def progress(done: int, total: int) -> None:
    width = 30
    filled = width * done // total
    pct = 100 * done / total
    sys.stderr.write(f"\r[{'#' * filled}{'.' * (width - filled)}] {done}/{total} ({pct:5.1f}%)")
    if done == total:
        sys.stderr.write("\n")
    sys.stderr.flush()


async def find_device(address: str | None, timeout: float):
    from bleak import BleakScanner

    if address:
        log(f"looking for {address}...")
        dev = await BleakScanner.find_device_by_address(address, timeout=timeout)
    else:
        log("scanning for Georecord...")
        dev = await BleakScanner.find_device_by_filter(
            lambda d, adv: SERVICE_UUID in [u.lower() for u in adv.service_uuids],
            timeout=timeout,
        )
    if dev is None:
        raise SystemExit("device not found (전원/거리 확인, 휴대폰 앱이 연결 중이면 먼저 끊어주세요)")
    log(f"found: {dev.name or '(unknown)'} [{dev.address}]")
    return dev


async def dump(args) -> None:
    from bleak import BleakClient

    out_dir: Path = args.output
    out_dir.mkdir(parents=True, exist_ok=True)

    dev = await find_device(args.address, args.scan_timeout)
    async with BleakClient(dev) as client:
        mtu = client.mtu_size
        log(f"connected, MTU={mtu}")
        if mtu < PAGE_SIZE + 3:
            log(f"warning: MTU {mtu} < {PAGE_SIZE + 3}, 페이지 응답이 잘릴 수 있습니다")

        g = Georecord(client)
        await g.start()

        size = await g.file_size()
        log(f"file size: {size:,} bytes ({size // RECORD_SIZE} records)")

        total_pages = (size + PAGE_SIZE - 1) // PAGE_SIZE
        buf = bytearray()
        for page in range(total_pages):
            buf += await g.read_page(page)
            progress(page + 1, total_pages)
        data = bytes(buf[:size])

        # 다운로드 도중 장치가 오래된 페이지를 잘라냈다면(용량 초과 compaction) 오프셋이 밀린다
        size_after = await g.file_size()
        if size_after < size:
            log(f"warning: 다운로드 중 장치 파일이 줄었습니다 ({size} -> {size_after}). 다시 받는 것을 권장합니다.")

        bin_path = out_dir / "location.bin"
        bin_path.write_bytes(data)
        log(f"saved: {bin_path} ({len(data):,} bytes)")
        export_all(data, out_dir)

        if args.logs:
            log_buf = bytearray()
            for page in range(LOG_PAGE_COUNT):
                log_buf += await g.read_log_page(page)
                progress(page + 1, LOG_PAGE_COUNT)
            text = bytes(log_buf).replace(b"\x00", b"").decode("utf-8", errors="replace")
            log_path = out_dir / "device_log.txt"
            log_path.write_text(text, encoding="utf-8")
            log(f"saved: {log_path}")

        if args.delete:
            if size_after < size:
                log("skip delete: 다운로드 데이터가 불완전할 수 있어 삭제하지 않습니다")
            elif size_after > size:
                # 다운로드 이후 새로 쌓인 레코드는 지우면 유실되므로 이어서 받는다
                log(f"skip delete: 다운로드 중 새 기록이 추가됨 ({size} -> {size_after}). 다시 실행하세요")
            else:
                ok = await g.delete_all()
                log("device data deleted" if ok else "delete failed")


def main() -> None:
    p = argparse.ArgumentParser(description="IZUNA 장치의 전체 위치 기록을 PC로 다운로드")
    p.add_argument("-o", "--output", type=Path, default=None, help="출력 폴더 (기본: ./izuna_YYYYmmdd_HHMMSS)")
    p.add_argument("-a", "--address", help="장치 주소 (macOS는 UUID). 생략 시 서비스 UUID로 스캔")
    p.add_argument("--scan-timeout", type=float, default=15.0, help="스캔 타임아웃 초 (기본 15)")
    p.add_argument("--logs", action="store_true", help="장치 로그 버퍼도 저장")
    p.add_argument("--delete", action="store_true", help="다운로드 후 장치의 위치 기록 삭제")
    p.add_argument("--from-bin", type=Path, help="BLE 대신 기존 location.bin 을 CSV/GPX로 변환")
    args = p.parse_args()

    if args.output is None:
        args.output = Path(f"izuna_{datetime.now():%Y%m%d_%H%M%S}")

    if args.from_bin:
        args.output.mkdir(parents=True, exist_ok=True)
        export_all(args.from_bin.read_bytes(), args.output)
        return

    try:
        asyncio.run(dump(args))
    except KeyboardInterrupt:
        log("\ninterrupted")
        sys.exit(130)


if __name__ == "__main__":
    main()
