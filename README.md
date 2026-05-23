# FLEX MBO PCAP → Indicative Auction (IAP / IAV)

Reads TSE FLEX MBO PCAP captures, maintains per-symbol order books, and computes **Indicative Auction Price** and **Indicative Auction Volume** using venue static data and [JPX tick size rules](https://www.jpx.co.jp/english/equities/trading/domestic/07.html).

## Project layout

```
source/          C++ core (PCAP I/O, FLEX protocol, order book, IAP/IAV, pipeline)
gui/             Python GUI only (subprocess to C++ binary)
test/            C++ unit tests and fixtures
build/           CMake output (pcap_reader.exe, test executables)
```

## Build

```powershell
cmake -S . -B build
cmake --build build
```

Produces `build/pcap_reader.exe` (or `build/source/pcap_reader.exe` depending on generator) and test binaries under `build/test/`.

## GUI

```powershell
python gui/auction_gui.py
```

While PCAPs load, the status bar shows live progress (file N/M, packet counts, ~%).

## Command line

Use `--progress` to print PCAP load status on **stderr** (stdout stays clean for JSON).

```powershell
# Order books only
.\build\pcap_reader.exe capture.pcap --export-books-json --quiet

# Full pipeline: venue + PCAP(s) → JSON + CSV
.\build\pcap_reader.exe 20241105_051.test.pcap `
  --venue TseVenue.20241105.json `
  --export-pipeline-json --csv-out auction_results.csv --depth 20 --quiet --progress

# Folder: all .pcap files in the folder
.\build\pcap_reader.exe --pcap-dir C:\path\to\pcaps `
  --venue TseVenue.20241105.json --export-pipeline-json --quiet
```

Multiple PCAPs are **merged by timestamp** when their capture times overlap or are
adjacent (within 60 seconds). Otherwise they are loaded **one file after another**
in the order given (CLI argument order, or sorted filename order for `--pcap-dir`).

When using `--pcap-dir` or a GUI folder, only files with the **same PCAP format**
(link type and microsecond vs nanosecond timestamps) are loaded. Incompatible files
(e.g. `flex_sample.pcap` mixed with `20241105_051.test.pcap`) are skipped with a
warning. `051` and `052` captures are compatible and load together.

### Large PCAP files

All packets are held in memory in one `PcapFile`. Multi-file loading reads **one file
at a time** (not all files duplicated in RAM at once). A lightweight header-only pass
determines time ranges before loading payloads. Very large captures (many GB) may still
require more RAM than available; use fewer/smaller files or split captures by time.
```

## GUI (Python)

```powershell
python gui\auction_gui.py
```

The GUI runs `pcap_reader` with `--export-pipeline-json`; it does not implement PCAP or auction logic in Python.

## Tests (C++)

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

Or run directly:

```powershell
.\build\test\test_auction.exe
.\build\test\test_venue.exe
```

Tests cover venue JSON loading and IAP/IAV on synthetic order books (`test/fixtures/venue_mini.json`).
