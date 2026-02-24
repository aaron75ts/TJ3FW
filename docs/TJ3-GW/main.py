"""TJ3 Gateway - BLE Configuration Tool"""

import argparse
import asyncio
import logging
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import ttkbootstrap as ttk_bs
from ttkbootstrap.constants import *
import struct

from ble_manager import TJ3BLEManager
from constants import (
    ENERGY_SNAPSHOT_UUID, ENERGY_CONFIG_UUID, ENERGY_CMD_UUID, ENERGY_PULSE_UUID,
    ENERGY_ALARM_UUID, DIAG_LOG_COUNT_UUID, DIAG_LOG_LEVEL_UUID,
    DIAG_LOG_STREAM_UUID, CHAR_PROPERTIES
)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Command OP Code definitions: (op_code, label, kind)
_CMD_OPCODES = [
    (0x66, "Io/Ior 資訊取得",    "READ"),
    (0xF6, "AI 量測數據取得",    "READ"),
    (0xD8, "最大電流取得",       "READ"),
    (0xF9, "需量數據取得",       "READ"),
    (0xD5, "設備名稱設定",       "WRITE"),
    (0xD6, "設備名稱取得",       "READ"),
    (0xE5, "通訊/需量設定",      "WRITE"),
    (0xE6, "通訊/需量取得",      "READ"),
    (0xB5, "AI 監控設定",        "WRITE"),
    (0xB6, "AI 監控取得",        "READ"),
    (0xA5, "MQTT 設定",          "WRITE"),
    (0xA6, "MQTT 取得",          "READ"),
    (0xF0, "Echo 測試",           "WRITE"),
]


class TJ3GatewayApp:
    """Main application for TJ3 BLE Gateway"""
    
    def __init__(self, root):
        self.root = root
        self.root.title("TJ3 Gateway - BLE Configuration Tool")
        self.root.geometry("1280x1024")
        
        self.ble_manager = TJ3BLEManager()
        self.loop = asyncio.new_event_loop()
        self._loop_thread = threading.Thread(
            target=self._run_event_loop, daemon=True, name="BLE-EventLoop"
        )
        self._loop_thread.start()
        
        # Variables
        self.connected = tk.BooleanVar(value=False)
        self.selected_device = None
        
        self._create_ui()
        # no need to call _schedule_async_tasks – loop runs in its own thread
    
    def _create_ui(self):
        """重設計後的單畫面布局:
        Top (Connection) | Left (Browser) | Right (Config + Activity Log) | Bottom (App Log)
        """
        self.root.title("TJ3 Gateway · BLE Configuration Tool")
        self.root.geometry("1440x960")
        self.root.minsize(1100, 780)
        self._apply_global_styles()

        # ── Top: Connection Panel ───────────────────────────────────────
        top = ttk_bs.Frame(self.root, padding=(8, 6, 8, 4))
        top.pack(fill=X, side=TOP)
        self._create_connection_panel(top)
        ttk_bs.Separator(self.root, orient=HORIZONTAL).pack(fill=X)

        # ── Vertical PanedWindow: (work area) / (App Log) ─────────────
        vp = ttk.PanedWindow(self.root, orient=VERTICAL)
        vp.pack(fill=BOTH, expand=True)

        # ── Work area: single frame, right panel fills entirely ──────
        work = ttk_bs.Frame(vp)
        vp.add(work, weight=4)

        right_frame = ttk_bs.Frame(work)
        right_frame.pack(fill=BOTH, expand=True, padx=6, pady=4)
        self._create_right_panel(right_frame)

        # ── App Log (bottom pane, resizable) ────────────────────────
        log_outer = ttk_bs.Frame(vp)
        vp.add(log_outer, weight=1)
        self._create_app_log(log_outer)
        self._setup_log_handler()

    def _apply_global_styles(self):
        """Set ≥ 14 pt fonts globally for all ttk / tk widgets."""
        import tkinter.font as tkfont
        for fname in ("TkDefaultFont", "TkTextFont", "TkMenuFont",
                      "TkHeadingFont", "TkCaptionFont", "TkTooltipFont",
                      "TkSmallCaptionFont"):
            try:
                tkfont.nametofont(fname).configure(size=14)
            except Exception:
                pass
        try:
            tkfont.nametofont("TkFixedFont").configure(size=13)
        except Exception:
            pass
        # Treeview rowheight must be set via ttk style; named fonts don't propagate there
        style = ttk_bs.Style.instance
        if style is None:
            style = ttk_bs.Style()
        style.configure(".",               font=("TkDefaultFont", 14))
        style.configure("Treeview",        font=("TkDefaultFont", 14), rowheight=28)
        style.configure("Treeview.Heading",font=("TkDefaultFont", 14, "bold"))

    # ══════════════════════════════════════════════════════════════════════
    # ❶ Connection Panel
    # ══════════════════════════════════════════════════════════════════════
    def _create_connection_panel(self, parent):
        """Top bar: scan, device list, connect button, status + MTU badges"""
        row1 = ttk_bs.Frame(parent)
        row1.pack(fill=X)
        ttk_bs.Label(row1, text="TJ3 Gateway",
                     font=("Helvetica", 15, "bold")).pack(side=LEFT)
        self.status_label = ttk_bs.Label(
            row1, text="● Disconnected", foreground="red",
            font=("TkDefaultFont", 14, "bold"))
        self.status_label.pack(side=LEFT, padx=(14, 0))
        self.mtu_label = ttk_bs.Label(row1, text="", foreground="gray",
                                       font=("TkDefaultFont", 14))
        self.mtu_label.pack(side=LEFT, padx=(14, 0))

        row2 = ttk_bs.Frame(parent)
        row2.pack(fill=X, pady=(4, 2))
        self.scan_btn = ttk_bs.Button(
            row2, text="🔍 掃描裝置", bootstyle=PRIMARY,
            command=self._on_scan_clicked, width=14)
        self.scan_btn.pack(side=LEFT, padx=(0, 8))
        self.scan_count_lbl = ttk_bs.Label(
            row2, text="", foreground="gray", font=("TkDefaultFont", 14))
        self.scan_count_lbl.pack(side=LEFT)
        self.connect_btn = ttk_bs.Button(
            row2, text="Connect", bootstyle=SUCCESS,
            command=self._on_connect_clicked, width=12)
        self.connect_btn.pack(side=RIGHT)

        tree_wrap = ttk_bs.Frame(parent)
        tree_wrap.pack(fill=X, pady=(0, 2))
        self.device_tree = ttk_bs.Treeview(
            tree_wrap, columns=("name", "address", "rssi"),
            show="headings", height=4, selectmode="browse")
        self.device_tree.heading("name",    text="Device Name")
        self.device_tree.heading("address", text="Address")
        self.device_tree.heading("rssi",    text="RSSI")
        self.device_tree.column("name",    width=250, anchor=W)
        self.device_tree.column("address", width=200, anchor=W)
        self.device_tree.column("rssi",    width=90,  anchor=CENTER)
        self.device_tree.tag_configure("tj3",   foreground="#1a73e8",
                                        font=("Courier", 13, "bold"))
        self.device_tree.tag_configure("other", foreground="gray")
        dev_sb = ttk_bs.Scrollbar(tree_wrap, orient=VERTICAL,
                                   command=self.device_tree.yview)
        self.device_tree.configure(yscrollcommand=dev_sb.set)
        self.device_tree.pack(side=LEFT, fill=X, expand=True)
        dev_sb.pack(side=RIGHT, fill=Y)
        self.device_tree.bind("<Double-1>", lambda _: self._on_connect_clicked())
        self._scanned_devices: list[dict] = []

    # ══════════════════════════════════════════════════════════════════════
    # ❷ Left Panel – Characteristic Browser
    # ══════════════════════════════════════════════════════════════════════
    def _create_char_browser(self, parent):
        """Characteristics Browser tab: BLE GATT tree (H+V scrollbars, resizable
        columns) + raw data display area."""

        # ── Treeview with both scrollbars in a grid container ───────────
        tree_outer = ttk_bs.Frame(parent)
        tree_outer.pack(fill=BOTH, expand=True, padx=6)
        tree_outer.grid_rowconfigure(0, weight=1)
        tree_outer.grid_columnconfigure(0, weight=1)

        columns = ("UUID", "Name", "Properties", "Handle")
        self.char_tree = ttk_bs.Treeview(
            tree_outer, columns=columns, show="tree headings")

        # Headings – clickable to sort would be a future enhancement
        self.char_tree.heading("#0",         text="Service",    anchor=W)
        self.char_tree.heading("UUID",       text="UUID",       anchor=W)
        self.char_tree.heading("Name",       text="Name",       anchor=W)
        self.char_tree.heading("Properties", text="Props",      anchor=W)
        self.char_tree.heading("Handle",     text="Hdl",        anchor=CENTER)

        # All columns stretch=True so user can drag the dividers
        self.char_tree.column("#0",         width=140, minwidth=80,  stretch=True)
        self.char_tree.column("UUID",       width=220, minwidth=120, stretch=True)
        self.char_tree.column("Name",       width=140, minwidth=80,  stretch=True)
        self.char_tree.column("Properties", width=90,  minwidth=60,  stretch=True)
        self.char_tree.column("Handle",     width=50,  minwidth=40,  stretch=False, anchor=CENTER)

        char_vsb = ttk_bs.Scrollbar(tree_outer, orient=VERTICAL,
                                     command=self.char_tree.yview)
        char_hsb = ttk_bs.Scrollbar(tree_outer, orient=HORIZONTAL,
                                     command=self.char_tree.xview)
        self.char_tree.configure(yscrollcommand=char_vsb.set,
                                  xscrollcommand=char_hsb.set)

        self.char_tree.grid(row=0, column=0, sticky="nsew")
        char_vsb.grid(row=0, column=1, sticky="ns")
        char_hsb.grid(row=1, column=0, sticky="ew")

        self.char_tree.bind("<<TreeviewSelect>>", self._on_char_selected)

        btn_row = ttk_bs.Frame(parent)
        btn_row.pack(fill=X, padx=6, pady=(3, 2))
        ttk_bs.Button(btn_row, text="Read",   bootstyle=(INFO, OUTLINE),
                      command=self._on_read_clicked,  width=8).pack(side=LEFT, padx=(0, 3))
        ttk_bs.Button(btn_row, text="Write",  bootstyle=(WARNING, OUTLINE),
                      command=self._on_write_value,   width=8).pack(side=LEFT, padx=(0, 3))
        ttk_bs.Button(btn_row, text="Notify", bootstyle=(SECONDARY, OUTLINE),
                      command=self._on_notify_clicked, width=8).pack(side=LEFT)

        ttk_bs.Separator(parent, orient=HORIZONTAL).pack(fill=X, padx=6, pady=(4, 2))
        ttk_bs.Label(parent, text="Raw Data",
                     font=("TkDefaultFont", 14, "bold")).pack(anchor=W, padx=6)

        raw_wrap = ttk_bs.Frame(parent)
        raw_wrap.pack(fill=BOTH, expand=True, padx=6, pady=(2, 6))
        self.detail_canvas = tk.Canvas(raw_wrap)
        detail_sb = ttk_bs.Scrollbar(raw_wrap, orient=VERTICAL,
                                      command=self.detail_canvas.yview)
        self.detail_frame = ttk_bs.Frame(self.detail_canvas)
        self.detail_frame.bind(
            "<Configure>",
            lambda e: self.detail_canvas.configure(
                scrollregion=self.detail_canvas.bbox("all")))
        self.detail_canvas.create_window((0, 0), window=self.detail_frame, anchor="nw")
        self.detail_canvas.configure(yscrollcommand=detail_sb.set)
        self.detail_canvas.pack(side=LEFT, fill=BOTH, expand=True)
        detail_sb.pack(side=RIGHT, fill=Y)

        self.current_char_uuid: str | None = None
        self.char_data_widgets: dict = {}
        ttk_bs.Label(self.detail_frame, text="← 點擊特徵值自動讀取",
                     foreground="gray",
                     font=("TkDefaultFont", 14)).pack(pady=15, padx=8)

    # ══════════════════════════════════════════════════════════════════════
    # ❸ Right Panel – Config Area + Activity Log
    # ══════════════════════════════════════════════════════════════════════
    def _create_right_panel(self, parent):
        """Right panel: vertical pane with config (top) and activity log (bottom)"""
        paned = ttk.PanedWindow(parent, orient=VERTICAL)
        paned.pack(fill=BOTH, expand=True)

        config_frame = ttk_bs.Frame(paned)
        self._create_config_area(config_frame)
        paned.add(config_frame, weight=2)

        log_frame = ttk_bs.Frame(paned)
        self._create_activity_log(log_frame)
        paned.add(log_frame, weight=1)

    def _create_config_area(self, parent):
        """Notebook: Characteristics Browser | Meter Config (8c02) | Commands (8c03)"""
        self.config_notebook = ttk_bs.Notebook(parent)
        self.config_notebook.pack(fill=BOTH, expand=True, padx=4, pady=4)

        browser_frame = ttk_bs.Frame(self.config_notebook)
        self.config_notebook.add(browser_frame, text="🔍 Characteristics")
        self._create_char_browser(browser_frame)

        meter_frame = ttk_bs.Frame(self.config_notebook)
        self.config_notebook.add(meter_frame, text="⚙ Meter Config (8c02)")
        self._create_meter_config_panel(meter_frame)

        cmd_frame = ttk_bs.Frame(self.config_notebook)
        self.config_notebook.add(cmd_frame, text="🧰 Commands (8c03)")
        self._create_cmd_panel(cmd_frame)

    # ── Meter Config Panel ─────────────────────────────────────────────
    def _create_meter_config_panel(self, parent):
        """Meter Config (8c02): CT Ratio + CH Igain for 6 channels"""
        action_row = ttk_bs.Frame(parent)
        action_row.pack(fill=X, padx=12, pady=(8, 4))
        ttk_bs.Button(action_row, text="Read from Device", bootstyle=INFO,
                      command=self._on_read_config, width=18).pack(side=LEFT, padx=(0, 8))
        ttk_bs.Button(action_row, text="Write to Device", bootstyle=WARNING,
                      command=self._on_write_config, width=18).pack(side=LEFT)
        self.config_status_label = ttk_bs.Label(action_row, text="", foreground="gray")
        self.config_status_label.pack(side=LEFT, padx=(12, 0))

        canvas = tk.Canvas(parent)
        sb = ttk_bs.Scrollbar(parent, orient=VERTICAL, command=canvas.yview)
        inner = ttk_bs.Frame(canvas)
        inner.bind("<Configure>",
                   lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.configure(yscrollcommand=sb.set)
        canvas.pack(side=LEFT, fill=BOTH, expand=True)
        sb.pack(side=RIGHT, fill=Y)

        cols_frame = ttk_bs.Frame(inner)
        cols_frame.pack(fill=X, padx=12, pady=8)

        self.config_ct_ratio_vars: list[tk.StringVar] = []
        self.config_ch_igain_vars: list[tk.StringVar] = []

        ttk_bs.Label(cols_frame, text="CH",       width=5,
                     font=("TkDefaultFont", 14, "bold")).grid(row=0, column=0, padx=4, pady=2)
        ttk_bs.Label(cols_frame, text="CT Ratio", width=14,
                     font=("TkDefaultFont", 14, "bold")).grid(row=0, column=1, padx=4, pady=2)
        ttk_bs.Label(cols_frame, text="CH Igain", width=14,
                     font=("TkDefaultFont", 14, "bold")).grid(row=0, column=2, padx=4, pady=2)
        ttk_bs.Separator(cols_frame, orient=HORIZONTAL).grid(
            row=1, column=0, columnspan=3, sticky=EW, pady=4)

        for i in range(6):
            row = i + 2
            ttk_bs.Label(cols_frame, text=f"CH{i+1}", width=5).grid(
                row=row, column=0, padx=4, pady=3)
            ct_var = tk.StringVar(value="1000")
            ttk_bs.Entry(cols_frame, textvariable=ct_var, width=14).grid(
                row=row, column=1, padx=4, pady=3)
            self.config_ct_ratio_vars.append(ct_var)
            ig_var = tk.StringVar(value="1")
            ttk_bs.Entry(cols_frame, textvariable=ig_var, width=14).grid(
                row=row, column=2, padx=4, pady=3)
            self.config_ch_igain_vars.append(ig_var)

        hex_lf = ttk_bs.LabelFrame(inner, text="Raw Hex Preview")
        hex_lf.pack(fill=X, padx=12, pady=(0, 8))
        self.config_hex_preview = tk.Text(
            hex_lf, height=2, wrap=WORD, font=("Courier", 13), state="disabled")
        self.config_hex_preview.pack(fill=X, padx=6, pady=4)

    # ── Activity Log ───────────────────────────────────────────────────
    def _create_activity_log(self, parent):
        """TX/RX monitor (tab 1) + System Log / 8c12 stream (tab 2)"""
        hdr = ttk_bs.Frame(parent)
        hdr.pack(fill=X, padx=6, pady=(4, 0))
        ttk_bs.Label(hdr, text="📨 Activity Log",
                     font=("TkDefaultFont", 14, "bold")).pack(side=LEFT)
        ttk_bs.Button(hdr, text="Clear All", bootstyle=(SECONDARY, OUTLINE),
                      command=self._activity_log_clear_all, width=10).pack(side=RIGHT, padx=4)

        nb = ttk_bs.Notebook(parent)
        nb.pack(fill=BOTH, expand=True, padx=6, pady=(2, 4))

        # Tab 1 – TX / RX
        tx_frame = ttk_bs.Frame(nb)
        nb.add(tx_frame, text="TX / RX")
        self.act_log_text = self._make_log_text(tx_frame, padx=2, pady=2)
        self.cmd_response_text = self.act_log_text   # backward-compat alias

        # Tab 2 – System Log (8c12)
        sys_frame = ttk_bs.Frame(nb)
        nb.add(sys_frame, text="System Log (8c12)")
        sys_ctrl = ttk_bs.Frame(sys_frame)
        sys_ctrl.pack(fill=X, padx=4, pady=(4, 2))
        ttk_bs.Button(sys_ctrl, text="▶ Start", bootstyle=(SUCCESS, OUTLINE),
                      command=self._on_start_log_stream, width=9).pack(side=LEFT, padx=(0, 4))
        ttk_bs.Button(sys_ctrl, text="■ Stop",  bootstyle=(DANGER, OUTLINE),
                      command=self._on_stop_log_stream,  width=9).pack(side=LEFT, padx=(0, 10))
        ttk_bs.Label(sys_ctrl, text="Log Level:",
                     font=("TkDefaultFont", 14)).pack(side=LEFT)
        self.log_level_var = tk.IntVar(value=3)
        for name, val in [("OFF", 0), ("ERR", 1), ("WRN", 2), ("INF", 3), ("DBG", 4)]:
            ttk_bs.Radiobutton(sys_ctrl, text=name,
                               variable=self.log_level_var,
                               value=val).pack(side=LEFT, padx=3)
        ttk_bs.Button(sys_ctrl, text="Set", bootstyle=(INFO, OUTLINE),
                      command=self._on_set_log_level, width=5).pack(side=LEFT, padx=(4, 0))
        ttk_bs.Button(sys_ctrl, text="Clear", bootstyle=(SECONDARY, OUTLINE),
                      command=lambda: self._clear_text(self.sys_log_text),
                      width=7).pack(side=RIGHT)
        self.sys_log_text = self._make_log_text(sys_frame, padx=2, pady=(0, 2))
        self.log_stream_text = self.sys_log_text      # backward-compat alias

    def _activity_log_clear_all(self):
        """Clear TX/RX and System Log tabs"""
        for w in (self.act_log_text, self.sys_log_text):
            w.delete("1.0", tk.END)

    def _clear_text(self, widget):
        """Clear a log text widget"""
        widget.delete("1.0", tk.END)

    # ── Commands Panel (8c03) ──────────────────────────────────────────
    def _create_cmd_panel(self, parent):
        """OP Code listbox + scrollable dynamic form (8c03 protocol)"""
        left_outer = ttk_bs.Frame(parent, width=230)
        left_outer.pack(side=LEFT, fill=Y, padx=(8, 0), pady=8)
        left_outer.pack_propagate(False)

        ttk_bs.Label(left_outer, text="OP Codes",
                     font=("TkDefaultFont", 14, "bold")).pack(anchor=W, pady=(0, 2))
        legend = ttk_bs.Frame(left_outer)
        legend.pack(anchor=W, pady=(0, 5))
        ttk_bs.Label(legend, text="■", foreground="#1a73e8").pack(side=LEFT)
        ttk_bs.Label(legend, text=" READ  ", foreground="gray",
                     font=("TkDefaultFont", 14)).pack(side=LEFT)
        ttk_bs.Label(legend, text="■", foreground="#e8710a").pack(side=LEFT)
        ttk_bs.Label(legend, text=" WRITE", foreground="gray",
                     font=("TkDefaultFont", 14)).pack(side=LEFT)

        lb_wrap = ttk_bs.Frame(left_outer)
        lb_wrap.pack(fill=BOTH, expand=True)
        self.cmd_listbox = tk.Listbox(
            lb_wrap, width=28, selectmode=tk.SINGLE,
            font=("Courier", 13), activestyle="dotbox")
        lb_scroll = ttk_bs.Scrollbar(lb_wrap, command=self.cmd_listbox.yview)
        self.cmd_listbox.configure(yscrollcommand=lb_scroll.set)
        for op_code, label, kind in _CMD_OPCODES:
            self.cmd_listbox.insert(tk.END, f"0x{op_code:02X}  {label}")
            color = "#1a73e8" if kind == "READ" else "#e8710a"
            self.cmd_listbox.itemconfig(tk.END, foreground=color)
        self.cmd_listbox.pack(side=LEFT, fill=BOTH, expand=True)
        lb_scroll.pack(side=RIGHT, fill=Y)
        self.cmd_listbox.bind("<<ListboxSelect>>", self._on_cmd_opcode_selected)

        ttk_bs.Separator(parent, orient=VERTICAL).pack(side=LEFT, fill=Y, padx=5, pady=8)

        right = ttk_bs.Frame(parent)
        right.pack(side=LEFT, fill=BOTH, expand=True, padx=(0, 8), pady=8)

        form_wrap = ttk_bs.Frame(right)
        form_wrap.pack(fill=BOTH, expand=True)
        self.cmd_form_canvas = tk.Canvas(form_wrap)
        cmd_form_scroll = ttk_bs.Scrollbar(
            form_wrap, orient=VERTICAL, command=self.cmd_form_canvas.yview)
        self.cmd_form_inner = ttk_bs.Frame(self.cmd_form_canvas)
        self.cmd_form_inner.bind(
            "<Configure>",
            lambda e: self.cmd_form_canvas.configure(
                scrollregion=self.cmd_form_canvas.bbox("all")))
        self.cmd_form_canvas.create_window(
            (0, 0), window=self.cmd_form_inner, anchor="nw")
        self.cmd_form_canvas.configure(yscrollcommand=cmd_form_scroll.set)
        self.cmd_form_canvas.pack(side=LEFT, fill=BOTH, expand=True)
        cmd_form_scroll.pack(side=RIGHT, fill=Y)

        self.cmd_form_widgets: dict = {}
        self.current_cmd_opcode: int | None = None
        self.cmd_status_lbl = ttk_bs.Label(right, text="")
        ttk_bs.Label(self.cmd_form_inner,
                     text="← 請選擇左側 OP Code",
                     foreground="gray",
                     font=("TkDefaultFont", 14)).pack(pady=40, padx=20)

    # ══════════════════════════════════════════════════════════════════════
    # ❹ App Log (bottom strip)
    # ══════════════════════════════════════════════════════════════════════
    def _create_app_log(self, parent):
        """App log panel (bottom pane of root vertical PanedWindow)."""
        ttk_bs.Separator(parent, orient=HORIZONTAL).pack(fill=X)
        hdr = ttk_bs.Frame(parent, padding=(8, 4, 8, 0))
        hdr.pack(fill=X)
        ttk_bs.Label(hdr, text="📋 App Log",
                     font=("TkDefaultFont", 14, "bold")).pack(side=LEFT)
        ttk_bs.Button(hdr, text="Clear", bootstyle=(SECONDARY, OUTLINE),
                      command=lambda: self._clear_text(self.log_text),
                      width=7).pack(side=RIGHT)
        self.log_text = self._make_log_text(parent, padx=6, pady=(2, 4))

    def _run_event_loop(self):
        """Run asyncio event loop in a dedicated background thread"""
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    @staticmethod
    def _make_log_text(parent, **pack_kw) -> tk.Text:
        """Create a copy-friendly readonly tk.Text with scrollbar and level color tags.

        Uses key-binding instead of state='disabled' so Cmd/Ctrl+C works on macOS.
        """
        frame = ttk_bs.Frame(parent)
        frame.pack(fill=BOTH, expand=True, **pack_kw)
        frame.grid_rowconfigure(0, weight=1)
        frame.grid_columnconfigure(0, weight=1)

        bg = '#1e1e1e'   # dark editor background
        text = tk.Text(frame, font=("TkFixedFont", 12), wrap=WORD,
                       bg=bg, fg='#d4d4d4', insertbackground=bg,
                       selectbackground='#264f78', relief='flat', bd=0)
        vsb = ttk_bs.Scrollbar(frame, orient=VERTICAL, command=text.yview)
        text.configure(yscrollcommand=vsb.set)
        text.grid(row=0, column=0, sticky='nsew')
        vsb.grid(row=0, column=1, sticky='ns')

        # Level colour tags
        text.tag_configure('DEBUG',    foreground='#808080')
        text.tag_configure('INFO',     foreground='#d4d4d4')
        text.tag_configure('WARNING',  foreground='#FFA500')
        text.tag_configure('ERROR',    foreground='#FF6347')
        text.tag_configure('CRITICAL', foreground='#FF4444',
                           font=("TkFixedFont", 12, "bold"))
        text.tag_configure('TX',       foreground='#6db3f2')
        text.tag_configure('RX',       foreground='#98d982')
        text.tag_configure('SYSTEM',   foreground='#c586c0')

        # Block edits but allow navigation + copy shortcuts
        def _block_edit(e):
            # Allow Cmd (Meta) and Ctrl combos (copy, select-all, etc.)
            if e.state & 0x8 or e.state & 0x4:
                return None
            # Allow navigation / modifier keys
            if e.keysym in ('Left', 'Right', 'Up', 'Down', 'Prior', 'Next',
                            'Home', 'End', 'Shift_L', 'Shift_R',
                            'Control_L', 'Control_R', 'Meta_L', 'Meta_R',
                            'Alt_L', 'Alt_R', 'Tab'):
                return None
            return "break"
        text.bind("<Key>", _block_edit)
        return text

    def _make_text_readonly(self, text_widget):
        """Legacy shim – new widgets use _make_log_text; kept for widgets
        created elsewhere that still call this method."""
        pass  # key-binding readonly is handled inside _make_log_text

    @staticmethod
    def _append_text(text_widget, text: str, tag: str = None):
        """Append text to a log text widget with an optional color tag."""
        text_widget.insert(tk.END, text, (tag,) if tag else ())
        text_widget.see(tk.END)

    def _show_copy_menu(self, event, text_widget):
        """Context menu for text widgets (right-click)."""
        try:
            menu = tk.Menu(self.root, tearoff=0)
            menu.add_command(label="複製 (Copy)",
                             command=lambda: text_widget.event_generate("<<Copy>>"))
            menu.add_command(label="全選 (Select All)",
                             command=lambda: text_widget.tag_add(tk.SEL, "1.0", tk.END))
            menu.post(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def _setup_log_handler(self):
        """Route Python logging to the App Log text widget with level colours."""
        app = self

        class TextHandler(logging.Handler):
            def __init__(self, text_widget):
                super().__init__()
                self.text_widget = text_widget

            def emit(self, record):
                msg = self.format(record)
                tag = record.levelname  # DEBUG / INFO / WARNING / ERROR / CRITICAL
                # Schedule on main thread (handler may fire from background threads)
                app.root.after(0, lambda m=msg, t=tag:
                               app._append_text(self.text_widget, m + '\n', t))

        handler = TextHandler(self.log_text)
        handler.setFormatter(
            logging.Formatter('%(asctime)s %(levelname)-8s %(message)s',
                              datefmt='%H:%M:%S'))
        logging.getLogger().addHandler(handler)
    
    def _schedule_async_tasks(self):
        """Legacy stub – event loop now runs in a background thread"""
        pass

    def _on_scan_clicked(self):
        """Handle scan button click"""
        self.scan_btn.config(state='disabled', text="Scanning...")
        self.scan_count_lbl.config(text="")
        # Clear existing results
        self.device_tree.delete(*self.device_tree.get_children())
        self._scanned_devices.clear()
        self.status_label.config(text="● Scanning...", foreground="orange")

        async def scan():
            try:
                devices = await self.ble_manager.scan_devices(timeout=5.0)
                self._scanned_devices = devices

                # Populate treeview
                self.device_tree.delete(*self.device_tree.get_children())
                for d in devices:
                    rssi_str = f"{d['rssi']} dBm" if d['rssi'] != -999 else "N/A"
                    tag = "tj3" if d["is_tj3"] else "other"
                    self.device_tree.insert(
                        "", tk.END,
                        values=(d["name"], d["address"], rssi_str),
                        tags=(tag,),
                    )

                # Auto-select first TJ3 device
                for idx, item in enumerate(self.device_tree.get_children()):
                    if self._scanned_devices[idx]["is_tj3"]:
                        self.device_tree.selection_set(item)
                        self.device_tree.see(item)
                        break

                # Update count label
                tj3_n = sum(1 for d in devices if d["is_tj3"])
                all_n  = len(devices)
                self.scan_count_lbl.config(
                    text=f"共 {all_n} 台裝置，{tj3_n} 台 TJ3（粗體藍字）"
                )

            except Exception as e:
                logger.error(f"Scan error: {e}")
                messagebox.showerror("Scan Error", f"Failed to scan: {e}")
            finally:
                self.scan_btn.config(state='normal', text="🔍 Scan for Devices")
                if not self.ble_manager.is_connected():
                    self.status_label.config(text="● Disconnected", foreground="red")

        asyncio.run_coroutine_threadsafe(scan(), self.loop)
    
    def _on_connect_clicked(self):
        """Handle connect button click"""
        if self.ble_manager.is_connected():
            asyncio.run_coroutine_threadsafe(self._disconnect(), self.loop)
            return

        sel = self.device_tree.selection()
        if not sel:
            messagebox.showerror("Error", "請先在表格中選擇一台裝置")
            return

        # Map selected row → scanned devices list
        row_idx = self.device_tree.index(sel[0])
        if row_idx >= len(self._scanned_devices):
            messagebox.showerror("Error", "裝置索引錯誤，請重新 Scan")
            return
        address = self._scanned_devices[row_idx]["address"]
        name    = self._scanned_devices[row_idx]["name"]

        self.connect_btn.config(state='disabled')
        self.status_label.config(text=f"● Connecting to {name}…", foreground="orange")
        asyncio.run_coroutine_threadsafe(self._connect(address), self.loop)
    
    async def _connect(self, address: str):
        """Connect to device"""
        success = await self.ble_manager.connect(address)
        
        # Re-enable button
        self.connect_btn.config(state='normal')
        
        if success:
            self.connected.set(True)
            self.status_label.config(text="● Connected", foreground="green")
            self.connect_btn.config(text="Disconnect", bootstyle=DANGER)
            # Show MTU info
            try:
                mtu = self.ble_manager.client.mtu_size
                self.mtu_label.config(text=f"| MTU: {mtu}")
            except Exception:
                self.mtu_label.config(text="")
            await self._refresh_characteristics()
            await self._sync_log_level_from_device()
            await self._setup_cmd_indication()
        else:
            self.status_label.config(text="● Connection Failed", foreground="red")
            messagebox.showerror(
                "Connection Error", 
                "Failed to connect to device.\n\n"
                "Possible reasons:\n"
                "- Device is out of range\n"
                "- Device is already connected to another app\n"
                "- Bluetooth interference\n\n"
                "Try:\n"
                "1. Scan again to refresh device list\n"
                "2. Move closer to the device\n"
                "3. Restart the device"
            )
    
    async def _disconnect(self):
        """Disconnect from device"""
        self.connect_btn.config(state='disabled')
        self.status_label.config(text="● Disconnecting...", foreground="orange")
        
        await self.ble_manager.disconnect()
        
        self.connect_btn.config(state='normal')
        self.connected.set(False)
        self.status_label.config(text="● Disconnected", foreground="red")
        self.mtu_label.config(text="")
        self.connect_btn.config(text="Connect", bootstyle=SUCCESS)
        self.char_tree.delete(*self.char_tree.get_children())
        # Reset raw data placeholder
        self._clear_detail_view()
        ttk_bs.Label(self.detail_frame, text="← 點擊特徵值自動讀取",
                     foreground="gray",
                     font=("TkDefaultFont", 14)).pack(pady=15, padx=8)

        # Show info message with reconnection tip
        messagebox.showinfo(
            "Disconnected",
            "Device disconnected successfully.\n\n"
            "To reconnect:\n"
            "1. Wait 2-3 seconds for device to restart advertising\n"
            "2. Click 'Scan for Devices' to refresh\n"
            "3. Select device and click 'Connect'"
        )
    
    async def _refresh_characteristics(self):
        """Refresh characteristics list"""
        chars = self.ble_manager.get_characteristics()
        
        # Group by service
        energy_chars = {}
        diag_chars = {}
        other_chars = {}
        
        for uuid, info in chars.items():
            if "4d6f8c0" in uuid and uuid.endswith("112233445500"):
                if "4d6f8c00" in uuid or "4d6f8c01" in uuid or "4d6f8c02" in uuid or \
                   "4d6f8c03" in uuid or "4d6f8c04" in uuid or "4d6f8c05" in uuid:
                    energy_chars[uuid] = info
            elif "4d6f8c1" in uuid:
                diag_chars[uuid] = info
            else:
                other_chars[uuid] = info
        
        self.char_tree.delete(*self.char_tree.get_children())
        
        # Add Energy Service
        if energy_chars:
            energy_node = self.char_tree.insert("", "end", text="Energy Service", open=True)
            for uuid, info in energy_chars.items():
                props = ", ".join(info["properties"])
                self.char_tree.insert(
                    energy_node, "end", values=(uuid, info["name"], props, info["handle"])
                )
        
        # Add Diagnostics Service
        if diag_chars:
            diag_node = self.char_tree.insert("", "end", text="Diagnostics Service", open=True)
            for uuid, info in diag_chars.items():
                props = ", ".join(info["properties"])
                self.char_tree.insert(
                    diag_node, "end", values=(uuid, info["name"], props, info["handle"])
                )
        
        # Add other services
        if other_chars:
            other_node = self.char_tree.insert("", "end", text="Other Services", open=False)
            for uuid, info in other_chars.items():
                props = ", ".join(info["properties"])
                self.char_tree.insert(
                    other_node, "end", values=(uuid, info["name"], props, info["handle"])
                )
    
    def _on_read_clicked(self):
        """Handle read button click"""
        selection = self.char_tree.selection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a characteristic")
            return
        
        item = self.char_tree.item(selection[0])
        if not item["values"]:
            return
        
        uuid = item["values"][0]
        asyncio.run_coroutine_threadsafe(self._read_and_display_char(uuid), self.loop)
    
    def _on_char_selected(self, event):
        """Handle characteristic selection"""
        selection = self.char_tree.selection()
        if not selection:
            return
        
        item = self.char_tree.item(selection[0])
        if not item["values"]:  # Service node selected
            return
        
        uuid = item["values"][0]
        props = item["values"][2]
        
        self.current_char_uuid = uuid
        
        # Check if it's a readable characteristic
        if "read" in props.lower():
            # Auto-read for READ characteristics
            asyncio.run_coroutine_threadsafe(self._read_and_display_char(uuid), self.loop)
        elif "write" in props.lower():
            # Show write form for WRITE characteristics
            self._show_write_form(uuid)
    
    def _parse_meter_config(self, data: bytes) -> dict:
        """Parse Meter Config data (24 bytes)"""
        if len(data) < 24:
            return {"error": f"Invalid length: {len(data)} bytes, expected 24"}
        
        result = {"ct_ratio": [], "ch_igain": []}
        
        # Parse CT Ratio (6 channels × 2 bytes, Little Endian)
        for i in range(6):
            offset = i * 2
            value = int.from_bytes(data[offset:offset+2], byteorder='little')
            result["ct_ratio"].append(value)
        
        # Parse CH Igain (6 channels × 2 bytes, Little Endian)
        for i in range(6):
            offset = 12 + i * 2
            value = int.from_bytes(data[offset:offset+2], byteorder='little')
            result["ch_igain"].append(value)
        
        return result
    
    def _parse_meter_snapshot(self, data: bytes) -> dict:
        """Parse Meter Snapshot data (20 bytes)"""
        if len(data) < 20:
            return {"error": f"Invalid length: {len(data)} bytes, expected 20"}
        
        # Simplified parsing - actual format needs to be confirmed from firmware
        result = {"raw_hex": data.hex()}
        
        # Try to parse common fields (this is a placeholder)
        # Actual format should be documented in firmware
        result["note"] = "Detailed snapshot parsing requires firmware documentation"
        
        return result
    
    def _parse_generic_data(self, data: bytes, uuid: str) -> dict:
        """Parse generic characteristic data"""
        result = {"raw_hex": data.hex(), "length": len(data)}
        
        # Try to parse as common types
        if len(data) == 1:
            result["uint8"] = data[0]
        elif len(data) == 2:
            result["uint16_le"] = int.from_bytes(data, byteorder='little')
            result["uint16_be"] = int.from_bytes(data, byteorder='big')
        elif len(data) == 4:
            result["uint32_le"] = int.from_bytes(data, byteorder='little')
            result["uint32_be"] = int.from_bytes(data, byteorder='big')
        
        return result
    
    async def _read_and_display_char(self, uuid: str):
        """Read characteristic and display parsed data"""
        value = await self.ble_manager.read_characteristic(uuid)
        if not value:
            self._clear_detail_view()
            label = ttk_bs.Label(self.detail_frame, text="Failed to read characteristic", foreground="red")
            label.pack(anchor=W, pady=5)
            return
        
        # Parse data based on UUID
        if "4d6f8c02" in uuid:  # Meter Config
            parsed = self._parse_meter_config(value)
        elif "4d6f8c01" in uuid:  # Meter Snapshot
            parsed = self._parse_meter_snapshot(value)
        else:
            parsed = self._parse_generic_data(value, uuid)
        
        # Display parsed data
        self._display_parsed_data(uuid, value, parsed, readonly=True)
    
    def _clear_detail_view(self):
        """Clear detail view"""
        for widget in self.detail_frame.winfo_children():
            widget.destroy()
        self.char_data_widgets.clear()
    
    def _display_parsed_data(self, uuid: str, raw_data: bytes, parsed: dict, readonly: bool = True):
        """Display parsed characteristic data"""
        self._clear_detail_view()
        
        # Header
        header = ttk_bs.Label(
            self.detail_frame, 
            text=f"UUID: {uuid[:8]}...{uuid[-12:]}", 
            font=('TkDefaultFont', 14, 'bold')
        )
        header.pack(anchor=W, pady=(0, 10))
        
        # Raw data
        raw_frame = ttk_bs.LabelFrame(self.detail_frame, text="Raw Data")
        raw_frame.pack(fill=X, pady=5)
        
        raw_text = tk.Text(raw_frame, height=2, wrap=WORD)
        raw_text.insert('1.0', raw_data.hex())
        raw_text.config(state='disabled')
        raw_text.pack(fill=X, padx=5, pady=5)
        
        # Parsed data
        if "error" in parsed:
            error_label = ttk_bs.Label(self.detail_frame, text=parsed["error"], foreground="red")
            error_label.pack(anchor=W, pady=5)
            return
        
        # Display Meter Config fields
        if "ct_ratio" in parsed:
            self._display_meter_config_fields(parsed, readonly)
        # Display generic fields
        else:
            self._display_generic_fields(parsed, readonly)
    
    def _display_meter_config_fields(self, parsed: dict, readonly: bool):
        """Display Meter Config fields"""
        config_frame = ttk_bs.LabelFrame(self.detail_frame, text="Meter Configuration")
        config_frame.pack(fill=X, pady=5)
        
        # CT Ratio section
        ct_label = ttk_bs.Label(config_frame, text="CT Ratio (Current Transformer):", font=('TkDefaultFont', 14, 'bold'))
        ct_label.pack(anchor=W, padx=5, pady=(5, 2))
        
        for i, value in enumerate(parsed["ct_ratio"], 1):
            row_frame = ttk_bs.Frame(config_frame)
            row_frame.pack(fill=X, padx=20, pady=2)
            
            ttk_bs.Label(row_frame, text=f"CH{i}:", width=10).pack(side=LEFT)
            
            if readonly:
                ttk_bs.Label(row_frame, text=str(value)).pack(side=LEFT, padx=5)
            else:
                var = tk.IntVar(value=value)
                entry = ttk_bs.Entry(row_frame, textvariable=var, width=10)
                entry.pack(side=LEFT, padx=5)
                self.char_data_widgets[f"ct_ratio_{i-1}"] = var
        
        # CH Igain section
        igain_label = ttk_bs.Label(config_frame, text="CH Igain (Current Gain):", font=('TkDefaultFont', 14, 'bold'))
        igain_label.pack(anchor=W, padx=5, pady=(10, 2))
        
        for i, value in enumerate(parsed["ch_igain"], 1):
            row_frame = ttk_bs.Frame(config_frame)
            row_frame.pack(fill=X, padx=20, pady=2)
            
            ttk_bs.Label(row_frame, text=f"CH{i}:", width=10).pack(side=LEFT)
            
            if readonly:
                ttk_bs.Label(row_frame, text=str(value)).pack(side=LEFT, padx=5)
            else:
                var = tk.IntVar(value=value)
                entry = ttk_bs.Entry(row_frame, textvariable=var, width=10)
                entry.pack(side=LEFT, padx=5)
                self.char_data_widgets[f"ch_igain_{i-1}"] = var
    
    def _display_generic_fields(self, parsed: dict, readonly: bool):
        """Display generic parsed fields"""
        fields_frame = ttk_bs.LabelFrame(self.detail_frame, text="Parsed Fields")
        fields_frame.pack(fill=X, pady=5)
        
        for key, value in parsed.items():
            if key == "raw_hex":
                continue
            
            row_frame = ttk_bs.Frame(fields_frame)
            row_frame.pack(fill=X, padx=5, pady=2)
            
            ttk_bs.Label(row_frame, text=f"{key}:", width=15).pack(side=LEFT)
            ttk_bs.Label(row_frame, text=str(value)).pack(side=LEFT, padx=5)
    
    def _show_write_form(self, uuid: str):
        """Show write form for WRITE characteristics"""
        self._clear_detail_view()
        
        # Header
        header = ttk_bs.Label(
            self.detail_frame,
            text=f"Write to: {uuid[:8]}...{uuid[-12:]}",
            font=('TkDefaultFont', 14, 'bold')
        )
        header.pack(anchor=W, pady=(0, 10))
        
        # Parse UUID and show appropriate form
        if "4d6f8c02" in uuid:  # Meter Config
            self._show_meter_config_write_form()
        else:
            self._show_generic_write_form()
    
    def _show_meter_config_write_form(self):
        """Show Meter Config write form"""
        # Read current values first
        asyncio.run_coroutine_threadsafe(
            self._read_and_display_char_for_write(self.current_char_uuid), 
            self.loop
        )
    
    async def _read_and_display_char_for_write(self, uuid: str):
        """Read characteristic and display for editing"""
        value = await self.ble_manager.read_characteristic(uuid)
        if not value:
            self._clear_detail_view()
            label = ttk_bs.Label(self.detail_frame, text="Failed to read current values", foreground="red")
            label.pack(anchor=W, pady=5)
            return
        
        # Parse data
        if "4d6f8c02" in uuid:  # Meter Config
            parsed = self._parse_meter_config(value)
        else:
            parsed = self._parse_generic_data(value, uuid)
        
        # Display as editable
        self._display_parsed_data(uuid, value, parsed, readonly=False)
    
    def _show_generic_write_form(self):
        """Show generic hex input form"""
        info_label = ttk_bs.Label(self.detail_frame, text="Enter hex data:")
        info_label.pack(anchor=W, pady=5)
        
        hex_entry = ttk_bs.Entry(self.detail_frame, width=40)
        hex_entry.pack(fill=X, padx=5, pady=5)
        self.char_data_widgets["hex_input"] = hex_entry
    
    def _on_write_value(self):
        """Handle write value button click"""
        if not self.current_char_uuid:
            messagebox.showwarning("Warning", "Please select a characteristic")
            return
        
        # Build data from widgets
        if "4d6f8c02" in self.current_char_uuid:  # Meter Config
            data = self._build_meter_config_data()
        elif "hex_input" in self.char_data_widgets:
            hex_str = self.char_data_widgets["hex_input"].get().replace(" ", "")
            try:
                data = bytes.fromhex(hex_str)
            except ValueError:
                messagebox.showerror("Error", "Invalid hex format")
                return
        else:
            messagebox.showwarning("Warning", "No data to write")
            return
        
        if data:
            asyncio.run_coroutine_threadsafe(self._write_char(self.current_char_uuid, data), self.loop)
    
    def _build_meter_config_data(self) -> bytes:
        """Build Meter Config data from widgets"""
        data = bytearray()
        
        # CT Ratio (6 channels × 2 bytes)
        for i in range(6):
            key = f"ct_ratio_{i}"
            if key in self.char_data_widgets:
                value = self.char_data_widgets[key].get()
                data.extend(value.to_bytes(2, byteorder='little'))
            else:
                data.extend((0).to_bytes(2, byteorder='little'))
        
        # CH Igain (6 channels × 2 bytes)
        for i in range(6):
            key = f"ch_igain_{i}"
            if key in self.char_data_widgets:
                value = self.char_data_widgets[key].get()
                data.extend(value.to_bytes(2, byteorder='little'))
            else:
                data.extend((0).to_bytes(2, byteorder='little'))
        
        return bytes(data)
    
    async def _write_char(self, uuid: str, data: bytes):
        """Write characteristic value"""
        success = await self.ble_manager.write_characteristic(uuid, data)
        if success:
            messagebox.showinfo("Write Success", f"Written {len(data)} bytes to {uuid}")
            # Re-read to show updated values
            asyncio.run_coroutine_threadsafe(self._read_and_display_char(uuid), self.loop)
    
    def _on_notify_clicked(self):
        """Handle notify button click"""
        selection = self.char_tree.selection()
        if not selection:
            messagebox.showwarning("Warning", "Please select a characteristic")
            return
        
        item = self.char_tree.item(selection[0])
        if not item["values"]:
            return
        
        uuid = item["values"][0]
        
        def callback(data: bytes):
            logger.info(f"Notification: {data.hex()}")
        
        asyncio.run_coroutine_threadsafe(
            self.ble_manager.enable_notification(uuid, callback), self.loop
        )
    
    def _populate_config_fields(self, data: bytes):
        """Populate individual config fields from 24-byte raw data"""
        if len(data) < 24:
            self.config_status_label.config(
                text=f"✗ Invalid length: {len(data)} bytes (expected 24)", foreground="red"
            )
            return
        for i in range(6):
            val = int.from_bytes(data[i*2:i*2+2], byteorder='little')
            self.config_ct_ratio_vars[i].set(str(val))
        for i in range(6):
            val = int.from_bytes(data[12+i*2:12+i*2+2], byteorder='little')
            self.config_ch_igain_vars[i].set(str(val))
        self._update_config_hex_preview(data)
        self.config_status_label.config(text="✓ Loaded", foreground="green")

    def _build_config_bytes(self) -> bytes | None:
        """Build 24-byte config from individual fields, returns None on validation error"""
        data = bytearray()
        try:
            for i, var in enumerate(self.config_ct_ratio_vars):
                v = int(var.get())
                if not (0 <= v <= 65535):
                    messagebox.showerror("Validation Error", f"CT Ratio CH{i+1} must be 0–65535")
                    return None
                data.extend(v.to_bytes(2, byteorder='little'))
            for i, var in enumerate(self.config_ch_igain_vars):
                v = int(var.get())
                if not (0 <= v <= 65535):
                    messagebox.showerror("Validation Error", f"CH Igain CH{i+1} must be 0–65535")
                    return None
                data.extend(v.to_bytes(2, byteorder='little'))
        except ValueError as e:
            messagebox.showerror("Validation Error", f"Invalid value: {e}")
            return None
        return bytes(data)

    def _update_config_hex_preview(self, data: bytes):
        """Update raw hex preview text widget"""
        self.config_hex_preview.config(state='normal')
        self.config_hex_preview.delete('1.0', tk.END)
        self.config_hex_preview.insert('1.0', data.hex(' ').upper())
        self.config_hex_preview.config(state='disabled')

    def _on_read_config(self):
        """Read meter config and fill individual fields"""
        self.config_status_label.config(text="Reading…", foreground="orange")

        async def read():
            value = await self.ble_manager.read_characteristic(ENERGY_CONFIG_UUID)
            if value:
                self._populate_config_fields(value)
            else:
                self.config_status_label.config(text="✗ Read failed", foreground="red")

        asyncio.run_coroutine_threadsafe(read(), self.loop)

    def _on_write_config(self):
        """Write meter config built from individual fields"""
        data = self._build_config_bytes()
        if data is None:
            return
        self._update_config_hex_preview(data)
        self.config_status_label.config(text="Writing…", foreground="orange")

        async def write():
            success = await self.ble_manager.write_characteristic(ENERGY_CONFIG_UUID, data)
            if success:
                self.config_status_label.config(text=f"✓ Written {len(data)} bytes", foreground="green")
                messagebox.showinfo("Write Success", f"Meter Config written successfully ({len(data)} bytes)")
            else:
                err = getattr(self.ble_manager, 'last_error', '')
                self.config_status_label.config(text="✗ Write failed", foreground="red")
                messagebox.showerror("Write Error", f"Failed to write Meter Config\n\n{err}")

        asyncio.run_coroutine_threadsafe(write(), self.loop)
    
    def _on_set_log_level(self):
        """Set device log level"""
        level = self.log_level_var.get()
        data = struct.pack("<B", level)
        asyncio.run_coroutine_threadsafe(
            self.ble_manager.write_characteristic(DIAG_LOG_LEVEL_UUID, data), self.loop
        )

    async def _sync_log_level_from_device(self):
        """Read log level from device and update UI to match"""
        value = await self.ble_manager.read_characteristic(DIAG_LOG_LEVEL_UUID)
        if value and len(value) >= 1:
            level = value[0]
            if 0 <= level <= 4:
                self.log_level_var.set(level)
                logger.info(f"Log Level synced from device: {level}")
    
    def _on_start_log_stream(self):
        """Start log stream – 8c15 NOTIFY"""
        app = self

        def _level_tag(text: str) -> str:
            """Infer a colour tag from Zephyr syslog priority or level keyword."""
            if text.startswith('<3>') or ' ERR ' in text or ' err ' in text:
                return 'ERROR'
            if text.startswith('<4>') or ' WRN ' in text or ' wrn ' in text:
                return 'WARNING'
            if text.startswith('<6>') or ' INF ' in text or ' inf ' in text:
                return 'INFO'
            if text.startswith('<7>') or ' DBG ' in text or ' dbg ' in text:
                return 'DEBUG'
            return 'SYSTEM'

        def callback(data: bytes):
            try:
                text = data.decode('utf-8', errors='replace')
                tag = _level_tag(text)
                app.root.after(0, lambda t=text, g=tag:
                               app._append_text(app.log_stream_text, t, g))
            except Exception:
                pass

        asyncio.run_coroutine_threadsafe(
            self.ble_manager.enable_notification(DIAG_LOG_STREAM_UUID, callback), self.loop
        )
    
    def _on_stop_log_stream(self):
        """Stop log stream"""
        asyncio.run_coroutine_threadsafe(
            self.ble_manager.disable_notification(DIAG_LOG_STREAM_UUID), self.loop
        )
    
    # ── Command (8c03) tab ──────────────────────────────────────────────────

    @staticmethod
    def _crc16_ccitt(data: bytes) -> int:
        """CRC16-CCITT (poly 0x1021, init 0xFFFF)"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
        return crc

    def _protocol_compose(self, op_code: int, payload: bytes = b'') -> bytes:
        """Build: [STX][LEN_L][LEN_H][OP][payload][ETX][CRC_L][CRC_H]"""
        len_val = 1 + len(payload) + 1  # OP + payload + ETX
        frame = bytearray()
        frame.append(0x02)                          # STX
        frame.append(len_val & 0xFF)                # LEN_L
        frame.append((len_val >> 8) & 0xFF)         # LEN_H
        frame.append(op_code)
        frame.extend(payload)
        frame.append(0x03)                          # ETX
        crc = self._crc16_ccitt(bytes(frame[1:]))   # CRC: LEN_L → ETX
        frame.append(crc & 0xFF)
        frame.append((crc >> 8) & 0xFF)
        return bytes(frame)

    def _cmd_clear_form(self):
        """Clear the command form panel"""
        for w in self.cmd_form_inner.winfo_children():
            w.destroy()
        self.cmd_form_widgets.clear()

    def _cmd_clear_response(self):
        """Clear response log"""
        self.cmd_response_text.delete('1.0', tk.END)

    def _on_cmd_opcode_selected(self, _event=None):
        """Handle OP code selection in listbox"""
        sel = self.cmd_listbox.curselection()
        if not sel:
            return
        op_code, label, kind = _CMD_OPCODES[sel[0]]
        self.current_cmd_opcode = op_code
        self._cmd_clear_form()
        self._cmd_build_form(op_code, label, kind)

    def _cmd_build_form(self, op_code: int, label: str, kind: str):
        """Build the right-panel form for the selected opcode"""
        f = self.cmd_form_inner

        # Title row
        hdr = ttk_bs.Frame(f)
        hdr.pack(fill=X, padx=15, pady=(10, 0))
        ttk_bs.Label(hdr, text=f"0x{op_code:02X}",
                     font=('Courier', 13, 'bold')).pack(side=LEFT)
        ttk_bs.Label(hdr,
                     text=f"  {kind}",
                     foreground="#1a73e8" if kind == "READ" else "#e8710a",
                     font=('TkDefaultFont', 14, 'bold')).pack(side=LEFT)
        ttk_bs.Label(hdr, text=f"  {label}",
                     font=('TkDefaultFont', 14)).pack(side=LEFT, padx=10)

        ttk_bs.Separator(f, orient=HORIZONTAL).pack(fill=X, padx=15, pady=8)

        # Per-opcode form builders
        builders = {
            0xD5: self._cmd_form_0xD5,
            0xE5: self._cmd_form_0xE5,
            0xB5: self._cmd_form_0xB5,
            0xA5: self._cmd_form_0xA5,
            0xF0: self._cmd_form_echo,
        }
        builders.get(op_code, self._cmd_form_read_only)(f, op_code)

        # Send button row
        ttk_bs.Separator(f, orient=HORIZONTAL).pack(fill=X, padx=15, pady=8)
        btn_row = ttk_bs.Frame(f)
        btn_row.pack(fill=X, padx=15, pady=(0, 10))
        ttk_bs.Button(
            btn_row,
            text=f"▶  Send  0x{op_code:02X}",
            bootstyle=PRIMARY,
            width=20,
            command=self._cmd_send
        ).pack(side=LEFT, padx=(0, 10))
        self.cmd_status_lbl = ttk_bs.Label(btn_row, text="", foreground="gray")
        self.cmd_status_lbl.pack(side=LEFT)

    def _cmd_form_read_only(self, parent, op_code: int):
        """Generic READ-only command: no payload needed"""
        ttk_bs.Label(
            parent,
            text="此指令不需要輸入參數，直接點擊 Send 發送查詢。",
            foreground="gray"
        ).pack(anchor=W, padx=15, pady=5)

    def _cmd_form_echo(self, parent, _op_code: int):
        """0xF0 Echo – free hex payload"""
        ttk_bs.Label(parent, text="Payload (hex 字串，選填):",
                     font=('TkDefaultFont', 14, 'bold')).pack(anchor=W, padx=15)
        var = tk.StringVar(value="AB 03")
        ttk_bs.Entry(parent, textvariable=var, width=40).pack(anchor=W, padx=15, pady=3)
        self.cmd_form_widgets['hex_payload'] = var

    def _cmd_form_0xD5(self, parent, _op_code: int):
        """0xD5 Set Device Name"""
        ttk_bs.Label(parent, text="Device Name (最多 20 字元):",
                     font=('TkDefaultFont', 14, 'bold')).pack(anchor=W, padx=15, pady=(0, 3))
        var = tk.StringVar(value="TJ3-GW")
        ttk_bs.Entry(parent, textvariable=var, width=30).pack(anchor=W, padx=15)
        self.cmd_form_widgets['device_name'] = var

    def _cmd_form_0xE5(self, parent, _op_code: int):
        """0xE5 Set Comm/Demand config"""
        f = ttk_bs.LabelFrame(parent, text="需量 Demand Config")
        f.pack(fill=X, padx=15, pady=5)
        for key, lbl, val in [
            ('demand_alarm1', '目標電力 demand_alarm1 (kW, uint16):', '0'),
            ('demand_alarm2', '限界電力 demand_alarm2 (kW, uint16):', '0'),
            ('pulse_const',   '脈衝係數 pulse_const (uint32):',       '1000'),
        ]:
            ttk_bs.Label(f, text=lbl, font=('TkDefaultFont', 14)).pack(anchor=W, padx=8, pady=(5, 0))
            var = tk.StringVar(value=val)
            ttk_bs.Entry(f, textvariable=var, width=30).pack(anchor=W, padx=8)
            self.cmd_form_widgets[key] = var

        f2 = ttk_bs.LabelFrame(parent, text="Modem 設定")
        f2.pack(fill=X, padx=15, pady=5)
        for key, lbl, val in [
            ('modem_ip', 'Modem IP (e.g. 192.168.1.1):', '0.0.0.0'),
            ('apn',      'APN (最多 63 字元):',            'internet'),
        ]:
            ttk_bs.Label(f2, text=lbl, font=('TkDefaultFont', 14)).pack(anchor=W, padx=8, pady=(5, 0))
            var = tk.StringVar(value=val)
            ttk_bs.Entry(f2, textvariable=var, width=30).pack(anchor=W, padx=8)
            self.cmd_form_widgets[key] = var

    def _cmd_form_0xB5(self, parent, _op_code: int):
        """0xB5 Set AI Config (DI timing – matching current firmware handler)"""
        ttk_bs.Label(
            parent,
            text="韌體目前實作：DI1 / DI2 偵測時間 (各 uint16, Little Endian)",
            foreground="gray", font=('TkDefaultFont', 8)
        ).pack(anchor=W, padx=15, pady=(0, 5))
        for ch, prefix in [(1, 'di1'), (2, 'di2')]:
            frm = ttk_bs.LabelFrame(parent, text=f"DI{ch} 通道")
            frm.pack(fill=X, padx=15, pady=5)
            for suffix, lbl, val in [
                ('_on',  f'ON  time (ms, uint16):',  '500'),
                ('_off', f'OFF time (ms, uint16):', '500'),
            ]:
                ttk_bs.Label(frm, text=lbl, font=('TkDefaultFont', 14)).pack(anchor=W, padx=8, pady=(5, 0))
                var = tk.StringVar(value=val)
                ttk_bs.Entry(frm, textvariable=var, width=15).pack(anchor=W, padx=8)
                self.cmd_form_widgets[prefix + suffix] = var

    def _cmd_form_0xA5(self, parent, _op_code: int):
        """0xA5 Set MQTT Config"""
        f = ttk_bs.LabelFrame(parent, text="MQTT 連線設定")
        f.pack(fill=X, padx=15, pady=5)
        for key, lbl, val in [
            ('mqtt_url',  'Server URL (最多 80 字元):', 'mqtt://192.168.1.1:1883'),
            ('mqtt_port', 'Server Port (uint16):',     '1883'),
            ('mqtt_cid',  'Client ID (最多 16 字元):',  'TJ3GW_001'),
            ('mqtt_user', 'Username (最多 32 字元):',   ''),
            ('mqtt_pass', 'Password (最多 32 字元):',   ''),
        ]:
            ttk_bs.Label(f, text=lbl, font=('TkDefaultFont', 14)).pack(anchor=W, padx=8, pady=(5, 0))
            var = tk.StringVar(value=val)
            show = '*' if 'pass' in key else ''
            ttk_bs.Entry(f, textvariable=var, width=40, show=show).pack(anchor=W, padx=8)
            self.cmd_form_widgets[key] = var

    @staticmethod
    def _parse_response_fields(op_code: int, payload: bytes) -> list[tuple[str, str]]:
        """
        將各 READ OP Code 的 response payload 解析成 (欄位名稱, 值字串) 清單。
        Layout 與韌體 settings.h 的 struct 相同（含 struct alignment padding）。
        """
        fields: list[tuple[str, str]] = []
        try:
            # ── 0xB5 SET AI Config → echo 8 bytes (same layout as 0xB6) ─────
            # ── 0xB6 GET AI Config ──────────────────────────────────────────
            if op_code in (0xB5, 0xB6):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                elif len(payload) >= 8:
                    di1_on, di1_off, di2_on, di2_off = struct.unpack_from('<HHHH', payload)
                    fields = [
                        ("DI1  ON  time", f"{di1_on} ms"),
                        ("DI1  OFF time", f"{di1_off} ms"),
                        ("DI2  ON  time", f"{di2_on} ms"),
                        ("DI2  OFF time", f"{di2_off} ms"),
                    ]

            # ── 0xA5 SET MQTT → 1-byte result; 0xA6 GET MQTT → 167 bytes ───
            elif op_code in (0xA5, 0xA6):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                elif len(payload) >= 167:
                    # mqtt_config_t layout (with 1-byte alignment padding after server_url[81]):
                    # [0:81]    server_url   char[81]
                    # [81]      (padding)    alignment byte
                    # [82:84]   server_port  uint16_t LE
                    # [84:101]  client_id    char[17]
                    # [101:134] username     char[33]
                    # [134:167] password     char[33]
                    server_url  = payload[0:81].rstrip(b'\x00').decode('utf-8', errors='replace')
                    server_port = struct.unpack_from('<H', payload, 82)[0]
                    client_id   = payload[84:101].rstrip(b'\x00').decode('utf-8', errors='replace')
                    username    = payload[101:134].rstrip(b'\x00').decode('utf-8', errors='replace')
                    password_raw = payload[134:167].rstrip(b'\x00').decode('utf-8', errors='replace')
                    fields = [
                        ("server_url",  server_url  or "(empty)"),
                        ("server_port", str(server_port)),
                        ("client_id",   client_id   or "(empty)"),
                        ("username",    username    or "(empty)"),
                        ("password",    '*' * len(password_raw) if password_raw else "(empty)"),
                    ]

            # ── 0xD5 SET Device Name → 1-byte result ────────────────────────
            # ── 0xD6 GET Device Name → 82 bytes (device_info_t) ─────────────
            elif op_code in (0xD5, 0xD6):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                elif len(payload) >= 82:
                    # device_info_t: char device_name[21] + char location[61]
                    device_name = payload[0:21].rstrip(b'\x00').decode('utf-8', errors='replace')
                    location    = payload[21:82].rstrip(b'\x00').decode('utf-8', errors='replace')
                    fields = [
                        ("device_name", device_name or "(empty)"),
                        ("location",    location    or "(empty)"),
                    ]

            # ── 0xE5 SET Comm/Demand → 1-byte result ────────────────────────
            # ── 0xE6 GET Comm/Demand → 76 bytes (demand_config_t) ────────────
            elif op_code in (0xE5, 0xE6):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                elif len(payload) >= 76:
                    # demand_config_t: uint16 da1, uint16 da2, uint32 pulse_const,
                    #                  uint8[4] modem_ip, char apn[64]
                    da1, da2, pc = struct.unpack_from('<HHI', payload, 0)
                    modem_ip = f"{payload[8]}.{payload[9]}.{payload[10]}.{payload[11]}"
                    apn = payload[12:76].rstrip(b'\x00').decode('utf-8', errors='replace')
                    fields = [
                        ("demand_alarm1", f"{da1} kW"),
                        ("demand_alarm2", f"{da2} kW"),
                        ("pulse_const",   str(pc)),
                        ("modem_ip",      modem_ip),
                        ("apn",           apn or "(empty)"),
                    ]

            # ── 0x66 GET IOR Info / 0xF6 GET Realtime Current (mock 4 bytes) ─
            elif op_code in (0x66, 0xF6):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                else:
                    fields = [(f"byte[{i}]", f"0x{b:02X}  (未實作)") for i, b in enumerate(payload)]

            # ── 0xD8 GET Max Current / 0xF9 GET Demand Data (not impl) ───────
            elif op_code in (0xD8, 0xF9):
                if len(payload) == 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]
                else:
                    fields = [(f"byte[{i}]", f"0x{b:02X}  (未實作)") for i, b in enumerate(payload)]

            # ── 0xF0 Echo ────────────────────────────────────────────────────
            elif op_code == 0xF0:
                fields = [("echo payload", payload.hex(' ').upper() or "(empty)")]

            # ── 0xF1 Flash Test / 0xFA Fast Format / 0xFB Low-Level Format ───
            elif op_code in (0xF1, 0xFA, 0xFB):
                if len(payload) >= 1:
                    ok = payload[0] == 0x00
                    fields = [("result", "0x00  OK ✓" if ok else f"0x{payload[0]:02X}  Error ✗")]

            else:
                fields = [(f"byte[{i}]", f"0x{b:02X}") for i, b in enumerate(payload)]

        except Exception as e:
            fields = [("parse error", str(e))]

        return fields

    def _cmd_build_payload(self, op_code: int) -> bytes | None:
        """Build payload bytes from current form widgets"""
        w = self.cmd_form_widgets
        try:
            if op_code == 0xD5:
                name = w['device_name'].get()[:20]
                return name.encode('utf-8') + b'\x00'

            elif op_code == 0xE5:
                da1 = int(w['demand_alarm1'].get())
                da2 = int(w['demand_alarm2'].get())
                pc  = int(w['pulse_const'].get())
                ip_parts = [int(x) for x in w['modem_ip'].get().split('.')]
                if len(ip_parts) != 4:
                    raise ValueError("IP 格式錯誤，請使用 x.x.x.x 格式")
                apn_raw = w['apn'].get()[:63].encode()
                apn_padded = apn_raw + b'\x00' * (64 - len(apn_raw))
                return struct.pack('<HHI4B', da1, da2, pc, *ip_parts) + apn_padded

            elif op_code == 0xB5:
                return struct.pack('<HHHH',
                    int(w['di1_on'].get()),  int(w['di1_off'].get()),
                    int(w['di2_on'].get()),  int(w['di2_off'].get()))

            elif op_code == 0xA5:
                url  = w['mqtt_url'].get()[:80].encode().ljust(81, b'\x00')
                pad  = b'\x00'                                    # struct alignment padding
                port = struct.pack('<H', int(w['mqtt_port'].get()))
                cid  = w['mqtt_cid'].get()[:16].encode().ljust(17, b'\x00')
                user = w['mqtt_user'].get()[:32].encode().ljust(33, b'\x00')
                pwd  = w['mqtt_pass'].get()[:32].encode().ljust(33, b'\x00')
                return url + pad + port + cid + user + pwd         # 167 bytes total

            elif op_code == 0xF0:
                hex_str = w.get('hex_payload', tk.StringVar()).get().replace(' ', '')
                return bytes.fromhex(hex_str) if hex_str else b''

            else:
                return b''  # READ-only: no payload

        except Exception as e:
            messagebox.showerror("Payload Error", f"參數錯誤: {e}")
            return None

    def _cmd_send(self):
        """Build and write command packet to ENERGY_CMD_UUID"""
        if not self.ble_manager.is_connected():
            messagebox.showwarning("Warning", "尚未連線")
            return
        op = self.current_cmd_opcode
        if op is None:
            return
        payload = self._cmd_build_payload(op)
        if payload is None:
            return
        frame = self._protocol_compose(op, payload)
        logger.debug(f"CMD send 0x{op:02X}: {frame.hex(' ').upper()}")
        # Log TX to activity log
        import datetime as _dt
        _ts = _dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self._append_text(
            self.act_log_text,
            f"[{_ts}] TX (8c03) OP=0x{op:02X}  {frame.hex(' ').upper()}\n",
            'TX')
        self.cmd_status_lbl.config(text="Sending…", foreground="orange")

        async def _send():
            ok = await self.ble_manager.write_characteristic(ENERGY_CMD_UUID, frame)
            if ok:
                self.cmd_status_lbl.config(
                    text=f"✓ Sent {len(frame)} bytes", foreground="green")
            else:
                err = getattr(self.ble_manager, 'last_error', '')
                self.cmd_status_lbl.config(text="✗ Failed", foreground="red")
                messagebox.showerror("Send Error", f"指令發送失敗\n{err}")

        asyncio.run_coroutine_threadsafe(_send(), self.loop)

    async def _setup_cmd_indication(self):
        """Subscribe to ENERGY_CMD_UUID indication for command responses"""
        import datetime

        def on_indication(data: bytes):
            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if len(data) < 4:
                self._append_text(self.cmd_response_text,
                                  f"[{ts}]  raw={data.hex(' ').upper()}  (frame too short)\n",
                                  'RX')
                return

            op_code = data[3]
            op_str  = f"0x{op_code:02X}"
            # payload = bytes between OP and trailing [ETX CRC_L CRC_H]
            payload = data[4:-3] if len(data) >= 8 else data[4:]
            payload_hex = payload.hex(' ').upper()

            # ── raw line ─────────────────────────────────────────────────────
            line = f"[{ts}] OP={op_str}  raw={data.hex(' ').upper()}\n"
            if payload_hex:
                line += f"         payload={payload_hex}\n"

            # ── parsed fields ────────────────────────────────────────────────
            fields = self._parse_response_fields(op_code, payload)
            if fields:
                line += "         ┌─ parsed " + "─" * 35 + "\n"
                for name, val in fields:
                    line += f"         │  {name:<16}: {val}\n"
                line += "         └" + "─" * 43 + "\n"

            line += "\n"
            self._append_text(self.cmd_response_text, line, 'RX')
            logger.debug(f"CMD indication OP={op_str}: {data.hex(' ')}")

        ok = await self.ble_manager.enable_notification(ENERGY_CMD_UUID, on_indication)
        if ok:
            logger.info("CMD indication subscribed (ENERGY_CMD_UUID)")
        else:
            logger.warning("CMD indication subscribe failed")

    def run(self):
        """Run the application"""
        try:
            self.root.mainloop()
        finally:
            self.loop.call_soon_threadsafe(
                lambda: asyncio.ensure_future(self.ble_manager.disconnect(), loop=self.loop)
            )
            self.loop.call_soon_threadsafe(self.loop.stop)
            self._loop_thread.join(timeout=3.0)
            self.loop.close()


def main():
    """Main entry point"""
    parser = argparse.ArgumentParser(description="TJ3 Gateway - BLE Configuration Tool")
    parser.add_argument(
        "--debug", action="store_true",
        help="Enable DEBUG level logging for the application"
    )
    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
        logger.debug("[DEBUG] Debug mode enabled via --debug flag")

    root = ttk_bs.Window(themename="flatly")
    app = TJ3GatewayApp(root)
    app.run()


if __name__ == "__main__":
    main()
