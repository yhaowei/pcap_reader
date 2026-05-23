# FLEX MBO PCAP → Indicative Auction (IAP / IAV)

## 1. which OS and version you use

```
The project is built with cursor on windows 10, so the AI model is combination of Opus 4.7, Sonnet 4.6, Codex 5.3, GPT-5.5, Composer 2.5
```

## 2.	which compiler and version you use,

```
G++ 15.2.0 and python 3.12.12 are used
```

## 3.	how to compile your code,

```
If cmake works on Windows then run bellow 2 lines in powershell

cmake -S . -B build
cmake --build build
```


```
Also can build with bellow raw g++ command

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
g++ $flags test/test_auction.cpp build/source/libmbo_core.a -o build/test/test_auction.exe
g++ $flags test/test_venue.cpp build/source/libmbo_core.a -o build/test/test_venue.exe
g++ $flags test/test_pcap_io.cpp build/source/libmbo_core.a -o build/test/test_pcap_io.exe

```


## 4.	how to run your program,

```
Run with Python GUI

specify json file
specify single pcap file/folder
click button 'Compute IAP/IAV' 
```

```
Run with Command line

Please refer Command line section bellow
```

## 5.	anything else that you believe we should know.

```
It supports loading multiple PCAPs files and large PCAP files

Multiple files would be merged during loading(Refer details in bellow Command Line section)

Loading progress is visible

Besides CSV file, IAP/IAV result is also visible on Python GUI

The latest order book will be displayed on GUI when sybmol selected in the dropdown list

There are test cases created for venue load funtion, pcap load function and calculation function
```

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
