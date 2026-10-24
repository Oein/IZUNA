# IZUNA PC Dumper

BLE로 장치의 전체 위치 기록을 받아 `location.bin` / `location.csv` / `location.gpx`로 저장합니다.
macOS / Windows / Linux 지원 ([bleak](https://github.com/hbldh/bleak)).

```sh
uv run izuna_dump.py                  # 스캔 → 전체 다운로드 (./izuna_YYYYmmdd_HHMMSS/)
uv run izuna_dump.py -o ./dump --logs # 출력 폴더 지정 + 장치 로그 저장
uv run izuna_dump.py --delete         # 다운로드 검증 후 장치 데이터 삭제
uv run izuna_dump.py --wipe           # 다운로드 없이 장치 데이터 전체 삭제 (-y: 확인 생략)
uv run izuna_dump.py --from-bin location.bin  # 기존 .bin → CSV/GPX 변환
```

`uv`가 없으면 `pip install bleak` 후 `python izuna_dump.py ...`로 실행하세요.

- GPX 시간은 UTC로 기록되므로 Lightroom, `exiftool -geotag location.gpx` 등에 바로 사용할 수 있습니다.
- 장치는 한 번에 하나의 central만 연결되므로, 휴대폰 앱/웹이 연결 중이면 먼저 끊어주세요.
- macOS에서는 터미널 앱에 Bluetooth 권한이 필요합니다 (시스템 설정 → 개인정보 보호 및 보안 → Bluetooth).
