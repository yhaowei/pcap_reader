#!/usr/bin/env python3
"""
Tkinter GUI for TSE FLEX MBO indicative auction (IAP / IAV).

All PCAP loading, order-book reconstruction, and auction math run in the C++
pcap_reader binary; this module only launches it and displays JSON results.
"""

from __future__ import annotations

import json
import subprocess
import sys
import threading
from pathlib import Path
from typing import Callable

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BOOK_DEPTH = 20
JPX_TICK_RULES_URL = "https://www.jpx.co.jp/english/equities/trading/domestic/07.html"

def _find_pcap_reader() -> Path:
    candidates = [
        PROJECT_ROOT / "build" / "pcap_reader.exe",
        PROJECT_ROOT / "build" / "source" / "pcap_reader.exe",
        PROJECT_ROOT / "build" / "pcap_reader",
        PROJECT_ROOT / "build" / "source" / "pcap_reader",
    ]
    for path in candidates:
        if path.exists():
            return path
    return candidates[0]


PCAP_READER = _find_pcap_reader()


def format_book_price(price_raw: int) -> str:
    if price_raw == (1 << 64) - 1:
        return "MARKET"
    yen = price_raw / 10_000
    return f"{yen:.4f}".rstrip("0").rstrip(".")


def resolve_pcap_args(pcap_path: Path) -> list[str]:
    if pcap_path.is_dir():
        return ["--pcap-dir", str(pcap_path)]
    return [str(pcap_path)]


def run_cpp_pipeline(
    venue: Path,
    pcap: Path,
    csv_out: Path | None,
    on_progress: Callable[[str], None] | None = None,
) -> dict:
    if not PCAP_READER.exists():
        raise FileNotFoundError(
            f"C++ binary not found: {PCAP_READER}\n"
            "Build with: cmake -S . -B build && cmake --build build"
        )

    cmd = [
        str(PCAP_READER),
        *resolve_pcap_args(pcap),
        "--venue",
        str(venue),
        "--export-pipeline-json",
        "--depth",
        str(BOOK_DEPTH),
        "--quiet",
        "--progress",
    ]
    if csv_out is not None:
        cmd.extend(["--csv-out", str(csv_out)])

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=str(PROJECT_ROOT),
    )
    assert proc.stderr is not None
    assert proc.stdout is not None

    stderr_tail: list[str] = []

    def read_stderr() -> None:
        for line in proc.stderr:
            stderr_tail.append(line)
            stripped = line.strip()
            if on_progress and stripped.startswith("[pcap-progress]"):
                on_progress(stripped[len("[pcap-progress]") :].strip())

    stderr_thread = threading.Thread(target=read_stderr, daemon=True)
    stderr_thread.start()

    stdout_text = proc.stdout.read()
    proc.wait()
    stderr_thread.join(timeout=1.0)

    if proc.returncode != 0:
        err = "".join(stderr_tail).strip() or stdout_text.strip() or f"exit {proc.returncode}"
        raise RuntimeError(err)
    return json.loads(stdout_text)


class AuctionViewerApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("TSE Indicative Auction (IAP / IAV)")
        self.root.geometry("900x720")
        self.root.minsize(720, 600)

        self.venue_path = tk.StringVar()
        self.pcap_path = tk.StringVar()
        self.csv_path = tk.StringVar(value=str(PROJECT_ROOT / "auction_results.csv"))
        self.status = tk.StringVar(
            value="Load TseVenue JSON and PCAP file/folder, then click Compute."
        )
        self.symbol_filter = tk.StringVar(value="All")

        self.results: list[dict] = []
        self.books: dict[str, dict] = {}

        self._build_ui()

    def _build_ui(self) -> None:
        frm = ttk.Frame(self.root, padding=8)
        frm.pack(fill=tk.BOTH, expand=True)

        row = 0
        ttk.Label(frm, text="Venue JSON:").grid(row=row, column=0, sticky=tk.W)
        ttk.Entry(frm, textvariable=self.venue_path, width=58).grid(
            row=row, column=1, sticky=tk.EW, padx=4
        )
        ttk.Button(frm, text="Browse...", command=self._browse_venue).grid(row=row, column=2)
        row += 1

        pcap_row = ttk.Frame(frm)
        pcap_row.grid(row=row, column=0, columnspan=3, sticky=tk.EW, pady=(6, 0))
        ttk.Label(pcap_row, text="PCAP file/folder:").pack(side=tk.LEFT)
        ttk.Entry(pcap_row, textvariable=self.pcap_path, width=52).pack(
            side=tk.LEFT, fill=tk.X, expand=True, padx=4
        )
        ttk.Button(pcap_row, text="File...", command=self._load_pcap_file).pack(side=tk.LEFT)
        ttk.Button(pcap_row, text="Folder...", command=self._load_pcap_folder).pack(
            side=tk.LEFT, padx=(4, 0)
        )
        row += 1

        ttk.Label(frm, text="Output CSV:").grid(row=row, column=0, sticky=tk.W, pady=(6, 0))
        ttk.Entry(frm, textvariable=self.csv_path, width=58).grid(
            row=row, column=1, sticky=tk.EW, padx=4, pady=(6, 0)
        )
        ttk.Button(frm, text="Browse...", command=self._browse_csv).grid(
            row=row, column=2, pady=(6, 0)
        )
        row += 1

        actions = ttk.Frame(frm)
        actions.grid(row=row, column=0, columnspan=3, sticky=tk.W, pady=(10, 4))
        ttk.Button(actions, text="Compute IAP/IAV", command=self._compute).pack(side=tk.LEFT)
        ttk.Label(actions, text="  Symbol:").pack(side=tk.LEFT)
        self.symbol_combo = ttk.Combobox(
            actions,
            textvariable=self.symbol_filter,
            values=["All"],
            width=14,
            state="readonly",
        )
        self.symbol_combo.pack(side=tk.LEFT, padx=4)
        self.symbol_combo.bind("<<ComboboxSelected>>", lambda _e: self._refresh_views())
        row += 1

        ttk.Label(frm, textvariable=self.status).grid(
            row=row, column=0, columnspan=3, sticky=tk.W, pady=(4, 6)
        )
        row += 1

        ttk.Label(
            frm,
            text=f"Tick rules: {JPX_TICK_RULES_URL}",
            font=("Segoe UI", 8),
        ).grid(row=row, column=0, columnspan=3, sticky=tk.W)
        row += 1

        self.paned = ttk.PanedWindow(frm, orient=tk.VERTICAL)
        self.paned.grid(row=row, column=0, columnspan=3, sticky=tk.NSEW)
        frm.rowconfigure(row, weight=1)
        frm.columnconfigure(1, weight=1)

        iap_frame = ttk.LabelFrame(self.paned, text="IAP / IAV Results", padding=4)
        self.paned.add(iap_frame, weight=1)

        cols = ("symbol", "iap", "iav")
        self.tree = ttk.Treeview(iap_frame, columns=cols, show="headings", height=10)
        for col, title, width in [
            ("symbol", "Symbol", 100),
            ("iap", "IAP", 120),
            ("iav", "IAV", 120),
        ]:
            self.tree.heading(col, text=title)
            self.tree.column(col, width=width, anchor=tk.CENTER)

        iap_scroll = ttk.Scrollbar(iap_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=iap_scroll.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        iap_scroll.pack(side=tk.RIGHT, fill=tk.Y)

        self.book_frame = ttk.LabelFrame(
            self.paned, text="Order Book Depth (select a symbol)", padding=4
        )
        self.paned.add(self.book_frame, weight=2)
        self.paned.forget(self.book_frame)

        book_cols = ("bid_ord", "bid_qty", "bid_px", "ask_px", "ask_qty", "ask_ord")
        self.book_tree = ttk.Treeview(
            self.book_frame, columns=book_cols, show="headings", height=14
        )
        for col, title, width in [
            ("bid_ord", "Bid #", 60),
            ("bid_qty", "Bid Qty", 90),
            ("bid_px", "Bid Price", 110),
            ("ask_px", "Ask Price", 110),
            ("ask_qty", "Ask Qty", 90),
            ("ask_ord", "Ask #", 60),
        ]:
            self.book_tree.heading(col, text=title)
            anchor = tk.E if col.endswith(("qty", "ord")) else tk.CENTER
            self.book_tree.column(col, width=width, anchor=anchor)

        book_scroll = ttk.Scrollbar(
            self.book_frame, orient=tk.VERTICAL, command=self.book_tree.yview
        )
        self.book_tree.configure(yscrollcommand=book_scroll.set)
        self.book_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        book_scroll.pack(side=tk.RIGHT, fill=tk.Y)

        default_venue = PROJECT_ROOT / "TseVenue.20241105.json"
        if default_venue.exists():
            self.venue_path.set(str(default_venue))

        default_pcap = PROJECT_ROOT / "20241105_051.test.pcap"
        if default_pcap.exists():
            self.pcap_path.set(str(default_pcap))

    def _hide_book_panel(self) -> None:
        if str(self.book_frame) in self.paned.panes():
            self.paned.forget(self.book_frame)

    def _show_book_panel(self, symbol: str) -> None:
        self.book_frame.configure(text=f"Order Book Depth — {symbol} (latest)")
        if str(self.book_frame) not in self.paned.panes():
            self.paned.add(self.book_frame, weight=2)

    def _browse_venue(self) -> None:
        path = filedialog.askopenfilename(
            title="Select TseVenue JSON",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
            initialdir=str(PROJECT_ROOT),
        )
        if path:
            self.venue_path.set(path)

    def _load_pcap_file(self) -> None:
        path = filedialog.askopenfilename(
            title="Select PCAP file",
            filetypes=[("PCAP files", "*.pcap *.pcapng"), ("All files", "*.*")],
            initialdir=str(PROJECT_ROOT),
        )
        if path:
            self.pcap_path.set(path)

    def _load_pcap_folder(self) -> None:
        path = filedialog.askdirectory(
            title="Select folder containing PCAP files",
            initialdir=str(PROJECT_ROOT),
        )
        if path:
            self.pcap_path.set(path)

    def _browse_csv(self) -> None:
        path = filedialog.asksaveasfilename(
            title="Save CSV",
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv")],
            initialdir=str(PROJECT_ROOT),
            initialfile="auction_results.csv",
        )
        if path:
            self.csv_path.set(path)

    def _compute(self) -> None:
        venue = self.venue_path.get().strip()
        pcap = self.pcap_path.get().strip()
        if not venue or not pcap:
            messagebox.showwarning("Input", "Please select Venue JSON and PCAP file/folder.")
            return

        csv_path = Path(self.csv_path.get().strip()) if self.csv_path.get().strip() else None
        self.status.set("Running C++ pipeline (PCAP + IAP/IAV)...")

        def on_progress(message: str) -> None:
            self.root.after(0, lambda m=message: self.status.set(m))

        def worker() -> None:
            try:
                data = run_cpp_pipeline(
                    Path(venue),
                    Path(pcap),
                    csv_path,
                    on_progress=on_progress,
                )
            except Exception as exc:  # noqa: BLE001
                self.root.after(0, lambda e=exc: self._on_compute_failed(e))
                return
            self.root.after(0, lambda d=data: self._on_compute_done(d, csv_path))

        threading.Thread(target=worker, daemon=True).start()

    def _on_compute_done(self, data: dict, csv_path: Path | None) -> None:
        self.results = data.get("results", [])
        self.books = data.get("books", {})
        symbols = ["All"] + [r["symbol"] for r in self.results]
        self.symbol_combo.configure(values=symbols)
        self.symbol_filter.set("All")
        valid = sum(1 for r in self.results if r.get("valid"))
        nfiles = len(data.get("pcap_files", []))
        csv_msg = f" CSV: {csv_path}" if csv_path else ""
        self.status.set(
            f"Done: {nfiles} PCAP file(s), {valid}/{len(self.results)} symbols with IAP.{csv_msg}"
        )
        self._refresh_views()

    def _on_compute_failed(self, exc: Exception) -> None:
        messagebox.showerror("Compute failed", str(exc))
        self.status.set(str(exc))

    def _refresh_views(self) -> None:
        self._refresh_table()
        self._refresh_book()

    def _refresh_table(self) -> None:
        for item in self.tree.get_children():
            self.tree.delete(item)

        selected = self.symbol_filter.get()
        rows = self.results
        if selected != "All":
            rows = [r for r in self.results if r.get("symbol") == selected]

        for row in rows:
            iap = row.get("iap", "") if row.get("valid") else "-"
            self.tree.insert("", tk.END, values=(row.get("symbol", ""), iap, row.get("iav", 0)))

    def _refresh_book(self) -> None:
        for item in self.book_tree.get_children():
            self.book_tree.delete(item)

        selected = self.symbol_filter.get()
        if selected == "All" or not self.books:
            self._hide_book_panel()
            return

        book = self.books.get(selected)
        if not book:
            self._show_book_panel(selected)
            self.book_tree.insert("", tk.END, values=("-", "-", "-", "-", "-", "-"))
            return

        self._show_book_panel(selected)

        bids = book.get("bids", [])[:BOOK_DEPTH]
        asks = book.get("asks", [])[:BOOK_DEPTH]
        rows = max(len(bids), len(asks), 1)

        for i in range(rows):
            bid = bids[i] if i < len(bids) else {}
            ask = asks[i] if i < len(asks) else {}
            bid_px = format_book_price(int(bid["price"])) if bid else ""
            ask_px = format_book_price(int(ask["price"])) if ask else ""
            self.book_tree.insert(
                "",
                tk.END,
                values=(
                    bid.get("orders", "") if bid else "",
                    bid.get("qty", "") if bid else "",
                    bid_px,
                    ask_px,
                    ask.get("qty", "") if ask else "",
                    ask.get("orders", "") if ask else "",
                ),
            )

        order_count = book.get("order_count", "?")
        iap_row = next((r for r in self.results if r.get("symbol") == selected), None)
        iap_txt = (
            f"IAP={iap_row.get('iap')} IAV={iap_row.get('iav')}"
            if iap_row and iap_row.get("valid")
            else ""
        )
        self.status.set(
            f"{selected}: {order_count} orders, "
            f"{len(bids)} bid / {len(asks)} ask levels. {iap_txt}".strip()
        )


def main() -> None:
    root = tk.Tk()
    AuctionViewerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
