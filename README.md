# FLEX MBO PCAP → Indicative Auction (IAP / IAV)

Reads TSE FLEX Full MBO PCAP captures, maintains per-symbol order books, and computes **Indicative Auction Price** and **Indicative Auction Volume** using venue static data and [JPX tick size rules](https://www.jpx.co.jp/english/equities/trading/domestic/07.html).

All PCAP loading, order-book reconstruction, and auction math run in **C++** (`source/`). The optional Python GUI (`gui/`) only launches the C++ binary and displays results.

## 1. OS and version

- **OS:** Windows 10 (build 19045)
- Tested on Windows 10 with MSYS2 UCRT64 toolchain

### AI tools used

This project was developed with assistance from **Cursor AI** (Claude-based coding agent). AI was used for architecture design, C++ implementation, test authoring, debugging, and documentation. All code was reviewed and built locally.
The AI model is combination of Opus 4.7, Sonnet 4.6, Codex 5.3, GPT-5.5, Composer 2.5

## 2. Compiler and version

- **C++ compiler:** g++ 15.2.0 (MSYS2 UCRT64), **C++17**
- **Python (GUI only):** 3.12.x

## 3. How to compile

### CMake (recommended)

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

If `cmake` fails on your machine, use the direct g++ build below.

### Direct g++ (MinGW)

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force build\source, build\test | Out-Null
$flags = "-std=c++17", "-Isource", "-Wall", "-Wextra", "-O2"
$core = @("pcap_io","flex_protocol","flex_tags","order_book","jpx_tick","venue_loader","auction","pipeline")
$objs = foreach ($n in $core) {
  $o = "build/source/$n.o"
  g++ $flags -c "source/$n.cpp" -o $o
  $o
}
ar rcs build/source/libmbo_core.a $objs
g++ $flags source/main.cpp build/source/libmbo_core.a -o build/pcap_reader.exe
foreach ($t in @("test_auction","test_venue","test_pcap_io","test_order_book","test_jpx_tick","test_pipeline")) {
  g++ $flags "test/$t.cpp" build/source/libmbo_core.a -o "build/test/$t.exe"
}
```

Produces:

- `build/pcap_reader.exe` — main program
- `build/test/test_*.exe` — unit tests

## 4. How to run

### Optional Python GUI

```powershell
python gui/auction_gui.py

specify json file
specify single pcap file/folder
click button 'Compute IAP/IAV' 
```

### Primary deliverable: CSV for sample PCAPs

```powershell
.\build\pcap_reader.exe 20241105_051.test.pcap 20241105_052.test.pcap `
  --venue TseVenue.20241105.json `
  --csv-out auction_results.csv --quiet --progress
```

Output CSV format: **`symbol,iap,iav`** (one row per **stock** on the PCAP channel, security types 1–4 from the venue file). Symbols with no crossing book have an empty IAP and IAV `0`.

### Other CLI examples

```powershell
# Single PCAP, channel auto-detected from filename (051 → channel 51)
.\build\pcap_reader.exe 20241105_051.test.pcap `
  --venue TseVenue.20241105.json --csv-out auction_results.csv --quiet

# All compatible .pcap files in a folder
.\build\pcap_reader.exe --pcap-dir . `
  --venue TseVenue.20241105.json --csv-out auction_results.csv --quiet --progress

# Export order books as JSON (no venue required)
.\build\pcap_reader.exe capture.pcap --export-books-json --quiet
```

Use `--progress` to print PCAP load status on **stderr** (stdout stays clean for JSON).

Select venue JSON and PCAP file/folder, then click **Compute IAP/IAV**. Progress appears in the status bar.

## 5. Other notes

- **Stocks** are defined as venue instruments with `securityType` **1–4** (`"01"`–`"04"` in JSON). Bonds and other types are excluded from IAP/IAV output.
- **CSV scope:** all stocks on the detected PCAP channel (from filename, e.g. `_051.`), not only symbols with activity in the capture.
- **Multi-PCAP:** files are merged by timestamp when capture times overlap or are adjacent (≤60 s); otherwise loaded sequentially.
- **Format filter:** `--pcap-dir` skips PCAPs with incompatible link type or timestamp resolution (e.g. `flex_sample.pcap` vs nanosecond captures).
- **Venue JSON** provides tick size table, lot size (`unitOfTrading`), and reference/min/max prices per symbol.
- **Large PCAPs:** one file loaded at a time during multi-file ingest; all packets are kept in memory for processing.

## Project layout

```
source/          C++ core (PCAP I/O, FLEX protocol, order book, IAP/IAV, pipeline)
gui/             Python GUI only (subprocess to C++ binary)
test/            C++ unit tests and fixtures
build/           Build output
```

## Tests (C++)

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Or run directly:

```powershell
.\build\test\test_auction.exe
.\build\test\test_venue.exe
.\build\test\test_pcap_io.exe
.\build\test\test_order_book.exe
.\build\test\test_jpx_tick.exe
.\build\test\test_pipeline.exe
```

Tests cover:

- Venue JSON loading, security type filtering, channel detection
- PCAP multi-file merge / sequential load / format filter
- Order book A/D/E/C/R tags and FLEX UDP parsing
- JPX tick tables and IAP/IAV (including TSE tie-break rules)
- Pipeline CSV output and channel-wide stock scope

Fixtures: `test/fixtures/venue_mini.json`, `test/fixtures/venue_mixed.json`.
