"""
runIT BLE control panel.

- Lists every discovered characteristic, selectable as the send/monitor target.
- Auto-generated packet builder: class -> packet -> form fields, sourced from
  decoder_types.py (regenerate it with sync_decoders_from_c.py after editing
  a dec_*.h file - this GUI never hardcodes the wire format).
- Batch sender: load a .txt file (one hex packet per line) and fire it at the
  selected characteristic.
- Monitor pane: toggle notifications per characteristic, see RX in HEX + TEXT.
"""
import asyncio
import ctypes as ct
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk, scrolledtext, filedialog

import decoder_types as dt
from ble_client import RunITBLEClient, format_hex, format_text

DEVICE_NAME = "runit"


class StdoutRedirector:
    """Mirrors print() output into the GUI's log pane."""

    def __init__(self, log_func):
        self.log_func = log_func
        self.buffer = ""

    def write(self, string):
        self.buffer += string
        if "\n" in self.buffer:
            lines = self.buffer.split("\n")
            for line in lines[:-1]:
                if line:
                    self.log_func(line)
            self.buffer = lines[-1]

    def flush(self):
        pass


class RunITGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("runIT - BLE Control Panel")
        self.geometry("980x800")

        self.ble = RunITBLEClient(device_name=DEVICE_NAME)
        self.loop = asyncio.new_event_loop()
        self.selected_cid = None
        self.batch_lines: list[str] = []

        self.packets_by_class: dict[int, list[type]] = {}
        for struct_cls in dt.PACKET_REGISTRY:
            self.packets_by_class.setdefault(int(struct_cls._class_header_), []).append(struct_cls)

        self.field_vars: dict[str, tuple[tk.Variable, type]] = {}

        self._build_ui()
        sys.stdout = StdoutRedirector(self.log)

        threading.Thread(target=self._start_async_loop, daemon=True).start()

    # ------------------------------------------------------------------ UI

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill=tk.X, padx=10, pady=8)

        self.btn_connect = ttk.Button(top, text="Connect BLE", command=self.on_connect_click)
        self.btn_connect.pack(side=tk.LEFT)

        self.btn_refresh = ttk.Button(top, text="Refresh Characteristics", command=self.on_refresh_click, state=tk.DISABLED)
        self.btn_refresh.pack(side=tk.LEFT, padx=6)

        self.target_label_var = tk.StringVar(value="Target: none selected")
        ttk.Label(top, textvariable=self.target_label_var, font=("Segoe UI", 9, "bold")).pack(side=tk.RIGHT)

        body = ttk.Frame(self)
        body.pack(fill=tk.BOTH, expand=True, padx=10, pady=4)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        self._build_char_list(body)
        self._build_packet_builder(body)
        self._build_batch_sender(self)
        self._build_monitor(self)

    def _build_char_list(self, parent):
        frame = ttk.LabelFrame(parent, text="Characteristics")
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 5))

        cols = ("id", "uuid", "name", "props", "monitor")
        self.tree = ttk.Treeview(frame, columns=cols, show="headings", height=14, selectmode="browse")
        for col, label, width in [
            ("id", "ID", 30),
            ("uuid", "UUID", 70),
            ("name", "Name", 130),
            ("props", "Properties", 140),
            ("monitor", "Monitor", 70),
        ]:
            self.tree.heading(col, text=label)
            self.tree.column(col, width=width, anchor=tk.W)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)

        btn_row = ttk.Frame(frame)
        btn_row.pack(fill=tk.X, padx=5, pady=(0, 5))
        self.btn_toggle_monitor = ttk.Button(btn_row, text="Toggle Monitor", command=self.on_toggle_monitor_click, state=tk.DISABLED)
        self.btn_toggle_monitor.pack(side=tk.LEFT)

        read_row = ttk.Frame(frame)
        read_row.pack(fill=tk.X, padx=5, pady=(0, 5))
        self.btn_read = ttk.Button(read_row, text="Read Selected", command=self.on_read_click, state=tk.DISABLED)
        self.btn_read.pack(side=tk.LEFT)

    def _build_packet_builder(self, parent):
        frame = ttk.LabelFrame(parent, text="Packet Builder (auto-generated from decoder_types.py)")
        frame.grid(row=0, column=1, sticky="nsew", padx=(5, 0))

        ttk.Label(frame, text="Class:").grid(row=0, column=0, padx=8, pady=5, sticky=tk.E)
        self.class_var = tk.StringVar()
        self.class_cb = ttk.Combobox(frame, textvariable=self.class_var, state="readonly", width=30)
        self.class_cb["values"] = [f"{c.name} (0x{int(c.value):02X})" for c in dt.DecoderClass]
        self.class_cb.grid(row=0, column=1, padx=8, pady=5, sticky=tk.W)
        self.class_cb.bind("<<ComboboxSelected>>", self.on_class_select)

        ttk.Label(frame, text="Packet:").grid(row=1, column=0, padx=8, pady=5, sticky=tk.E)
        self.packet_var = tk.StringVar()
        self.packet_cb = ttk.Combobox(frame, textvariable=self.packet_var, state="readonly", width=30)
        self.packet_cb.grid(row=1, column=1, padx=8, pady=5, sticky=tk.W)
        self.packet_cb.bind("<<ComboboxSelected>>", self.on_packet_select)

        self.form_frame = ttk.Frame(frame)
        self.form_frame.grid(row=2, column=0, columnspan=2, sticky="ew", padx=8, pady=5)

        self.preview_var = tk.StringVar(value="Preview: -")
        ttk.Label(frame, textvariable=self.preview_var, font=("Consolas", 9), foreground="#0057b8").grid(
            row=3, column=0, columnspan=2, padx=8, pady=(5, 0), sticky=tk.W
        )

        self.btn_send_packet = ttk.Button(frame, text="Send Packet", command=self.on_send_packet_click, state=tk.DISABLED)
        self.btn_send_packet.grid(row=4, column=0, columnspan=2, padx=8, pady=8, sticky=tk.EW)

        ttk.Separator(frame, orient=tk.HORIZONTAL).grid(row=5, column=0, columnspan=2, sticky="ew", padx=8, pady=5)

        ttk.Label(frame, text="Raw hex:").grid(row=6, column=0, padx=8, pady=5, sticky=tk.E)
        self.raw_hex_var = tk.StringVar()
        ttk.Entry(frame, textvariable=self.raw_hex_var, width=32).grid(row=6, column=1, padx=8, pady=5, sticky=tk.W)
        self.btn_send_raw = ttk.Button(frame, text="Send Raw Hex", command=self.on_send_raw_click, state=tk.DISABLED)
        self.btn_send_raw.grid(row=7, column=0, columnspan=2, padx=8, pady=(0, 8), sticky=tk.EW)

        if self.class_cb["values"]:
            self.class_cb.current(0)
            self.on_class_select(None)

    def _build_batch_sender(self, parent):
        frame = ttk.LabelFrame(parent, text="Batch Sender - .txt file, one hex packet per line")
        frame.pack(fill=tk.X, padx=10, pady=6)

        self.btn_load_file = ttk.Button(frame, text="Load .txt File...", command=self.on_load_file_click)
        self.btn_load_file.pack(side=tk.LEFT, padx=8, pady=8)

        self.batch_status_var = tk.StringVar(value="No file loaded.")
        ttk.Label(frame, textvariable=self.batch_status_var).pack(side=tk.LEFT, padx=8)

        self.btn_send_batch = ttk.Button(frame, text="Send All Lines", command=self.on_send_batch_click, state=tk.DISABLED)
        self.btn_send_batch.pack(side=tk.RIGHT, padx=8, pady=8)

    def _build_monitor(self, parent):
        frame = ttk.LabelFrame(parent, text="Monitor / Log")
        frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

        self.log_text = scrolledtext.ScrolledText(frame, height=16, font=("Consolas", 9), background="black")
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.log_text.tag_configure("ok", foreground="#3ddc63")
        self.log_text.tag_configure("error", foreground="#ff5c5c")

    # -------------------------------------------------------------- logging

    def log(self, msg):
        self.after(0, self._append_log, msg)

    def _append_log(self, msg):
        text = str(msg)
        lowered = text.lower()
        tag = "error" if any(k in lowered for k in ("error", "fail", "cannot")) else "ok"
        self.log_text.insert(tk.END, text + "\n", tag)
        self.log_text.see(tk.END)

    # -------------------------------------------------------- char selection

    def get_selected_info(self):
        if self.selected_cid is None:
            return None
        return self.ble.characteristics.get(self.selected_cid)

    def on_tree_select(self, _event):
        sel = self.tree.selection()
        if not sel:
            return
        self.selected_cid = int(self.tree.item(sel[0], "values")[0])
        info = self.get_selected_info()
        if info:
            self.target_label_var.set(f"Target: ID {info.cid} - {info.short_uuid} ({info.name})")
            self.btn_toggle_monitor.config(state=tk.NORMAL)
            self.btn_read.config(state=tk.NORMAL if "read" in info.properties else tk.DISABLED)
            self.btn_send_packet.config(state=tk.NORMAL)
            self.btn_send_raw.config(state=tk.NORMAL)
            self.btn_send_batch.config(state=tk.NORMAL if self.batch_lines else tk.DISABLED)

    def refresh_char_list(self):
        self.tree.delete(*self.tree.get_children())
        for info in self.ble.characteristics.values():
            monitor_str = "ON" if info.is_monitoring else "off"
            self.tree.insert("", tk.END, values=(info.cid, info.short_uuid, info.name, ", ".join(info.properties), monitor_str))

    def _update_monitor_column(self, cid: int):
        for row in self.tree.get_children():
            if int(self.tree.item(row, "values")[0]) == cid:
                info = self.ble.characteristics[cid]
                vals = list(self.tree.item(row, "values"))
                vals[4] = "ON" if info.is_monitoring else "off"
                self.tree.item(row, values=vals)

    # ---------------------------------------------------------- packet form

    def on_class_select(self, _event):
        idx = self.class_cb.current()
        if idx < 0:
            return
        class_val = list(dt.DecoderClass)[idx].value
        structs = self.packets_by_class.get(int(class_val), [])
        self.packet_cb["values"] = [f"0x{int(s._packet_header_):02X}  {s._action_name_}" for s in structs]
        self._current_class_structs = structs
        if self.packet_cb["values"]:
            self.packet_cb.current(0)
            self.on_packet_select(None)

    def on_packet_select(self, _event):
        for w in self.form_frame.winfo_children():
            w.destroy()
        self.field_vars.clear()

        idx = self.packet_cb.current()
        if idx < 0 or not getattr(self, "_current_class_structs", None):
            return
        struct_cls = self._current_class_structs[idx]
        self._current_struct_cls = struct_cls

        for i, (field_name, field_type) in enumerate(struct_cls._fields_):
            array_len = getattr(field_type, "_length_", None)
            label = f"{field_name} [{array_len}]:" if array_len else field_name + ":"
            ttk.Label(self.form_frame, text=label).grid(row=i, column=0, padx=6, pady=3, sticky=tk.E)
            if array_len:
                var = tk.StringVar(value=", ".join(["0"] * array_len))
                ttk.Entry(self.form_frame, textvariable=var, width=30).grid(row=i, column=1, padx=6, pady=3, sticky=tk.W)
            elif field_type is ct.c_bool:
                var = tk.StringVar(value="False")
                cb = ttk.Combobox(self.form_frame, textvariable=var, values=["False", "True"], state="readonly", width=28)
                cb.grid(row=i, column=1, padx=6, pady=3, sticky=tk.W)
            else:
                var = tk.StringVar(value="0")
                ttk.Entry(self.form_frame, textvariable=var, width=30).grid(row=i, column=1, padx=6, pady=3, sticky=tk.W)
            self.field_vars[field_name] = (var, field_type)

        self._update_preview()

    def _build_packet_bytes(self) -> bytes:
        struct_cls = self._current_struct_cls
        inst = struct_cls()
        for field_name, (var, field_type) in self.field_vars.items():
            val_str = var.get().strip()
            array_len = getattr(field_type, "_length_", None)
            if array_len:
                values = [int(v.strip(), 0) for v in val_str.split(",") if v.strip() != ""]
                if len(values) != array_len:
                    raise ValueError(f"'{field_name}' needs exactly {array_len} comma-separated values, got {len(values)}")
                getattr(inst, field_name)[:] = values
            elif field_type is ct.c_bool:
                setattr(inst, field_name, val_str == "True")
            else:
                setattr(inst, field_name, int(val_str, 0))
        return bytes([int(struct_cls._class_header_), int(struct_cls._packet_header_)]) + bytes(inst)

    def _update_preview(self):
        try:
            packet = self._build_packet_bytes()
            self.preview_var.set(f"Preview: {format_hex(packet)}")
        except Exception as e:
            self.preview_var.set(f"Preview: (invalid) {e}")

    def on_send_packet_click(self):
        try:
            packet = self._build_packet_bytes()
        except Exception as e:
            self.log(f"Error building packet: {e}")
            return
        self._send(packet)

    def on_send_raw_click(self):
        hex_str = self.raw_hex_var.get().strip()
        try:
            data = bytes.fromhex(hex_str.replace(" ", "").replace("0x", ""))
        except ValueError as e:
            self.log(f"Invalid hex string '{hex_str}': {e}")
            return
        self._send(data)

    def _send(self, data: bytes):
        info = self.get_selected_info()
        if not info:
            self.log("Error: no characteristic selected.")
            return
        asyncio.run_coroutine_threadsafe(self._send_task(info.cid, data), self.loop)

    async def _send_task(self, cid: int, data: bytes):
        try:
            await self.ble.send_bytes(cid, data)
            info = self.ble.characteristics[cid]
            self.log(f"[TX -> {info.short_uuid} ({info.name})] {format_text(data)}")
        except Exception as e:
            self.log(f"Send error: {e}")

    # ----------------------------------------------------------- batch file

    def on_load_file_click(self):
        path = filedialog.askopenfilename(title="Select hex packet file", filetypes=[("Text files", "*.txt"), ("All files", "*.*")])
        if not path:
            return
        try:
            raw_lines = Path(path).read_text(encoding="utf-8").splitlines()
        except Exception as e:
            self.log(f"Failed to read {path}: {e}")
            return

        self.batch_lines = [line.strip() for line in raw_lines if line.strip() and not line.strip().startswith("#")]
        self.batch_status_var.set(f"{Path(path).name}: {len(self.batch_lines)} packet(s) loaded")
        self.log(f"Loaded {len(self.batch_lines)} packet line(s) from {path}")
        if self.selected_cid is not None:
            self.btn_send_batch.config(state=tk.NORMAL if self.batch_lines else tk.DISABLED)

    def on_send_batch_click(self):
        info = self.get_selected_info()
        if not info:
            self.log("Error: no characteristic selected.")
            return
        if not self.batch_lines:
            self.log("Error: no batch file loaded.")
            return
        asyncio.run_coroutine_threadsafe(self._send_batch_task(info.cid, list(self.batch_lines)), self.loop)

    async def _send_batch_task(self, cid: int, lines: list[str]):
        info = self.ble.characteristics.get(cid)
        target_desc = f"{info.short_uuid} ({info.name})" if info else str(cid)
        self.log(f"Sending {len(lines)} packet(s) to {target_desc}...")
        sent, failed = 0, 0
        for i, line in enumerate(lines, start=1):
            try:
                data = bytes.fromhex(line.replace(" ", "").replace("0x", ""))
                await self.ble.send_bytes(cid, data)
                self.log(f"  [{i}/{len(lines)}] TX {format_text(data)}")
                sent += 1
            except Exception as e:
                self.log(f"  [{i}/{len(lines)}] FAILED '{line}': {e}")
                failed += 1
            await asyncio.sleep(0.05)
        self.log(f"Batch complete: {sent} sent, {failed} failed.")

    # -------------------------------------------------------------- monitor

    def on_toggle_monitor_click(self):
        info = self.get_selected_info()
        if not info:
            return
        if info.is_monitoring:
            asyncio.run_coroutine_threadsafe(self._stop_monitor_task(info.cid), self.loop)
        else:
            asyncio.run_coroutine_threadsafe(self._start_monitor_task(info.cid), self.loop)

    async def _start_monitor_task(self, cid: int):
        ok = await self.ble.start_notify(cid, self._on_notify)
        info = self.ble.characteristics[cid]
        if ok:
            self.log(f"Monitoring started on {info.short_uuid} ({info.name})")
        else:
            self.log(f"Cannot monitor {info.short_uuid} ({info.name}): no notify/indicate property")
        self.after(0, self._update_monitor_column, cid)

    async def _stop_monitor_task(self, cid: int):
        await self.ble.stop_notify(cid)
        info = self.ble.characteristics[cid]
        self.log(f"Monitoring stopped on {info.short_uuid} ({info.name})")
        self.after(0, self._update_monitor_column, cid)

    def _on_notify(self, cid: int, _char, data: bytes):
        info = self.ble.characteristics.get(cid)
        name = f"{info.short_uuid} ({info.name})" if info else str(cid)
        self.log(f"[RX <- {name}] {format_text(data)}")

    def on_read_click(self):
        info = self.get_selected_info()
        if not info:
            return
        asyncio.run_coroutine_threadsafe(self._read_task(info.cid), self.loop)

    async def _read_task(self, cid: int):
        try:
            data = await self.ble.read_char(cid)
            info = self.ble.characteristics[cid]
            self.log(f"[READ <- {info.short_uuid} ({info.name})] {format_text(data)}")
        except Exception as e:
            self.log(f"Read error: {e}")

    # --------------------------------------------------------- connect/loop

    def _start_async_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def on_connect_click(self):
        if self.ble.client and self.ble.client.is_connected:
            asyncio.run_coroutine_threadsafe(self._disconnect_task(), self.loop)
        else:
            self.btn_connect.config(state=tk.DISABLED, text="Connecting...")
            asyncio.run_coroutine_threadsafe(self._connect_task(), self.loop)

    def on_refresh_click(self):
        asyncio.run_coroutine_threadsafe(self._discover_task(), self.loop)

    async def _connect_task(self):
        self.log(f"Scanning for BLE device '{self.ble.device_name}'...")
        try:
            connected = await self.ble.connect()
            if not connected:
                self.log(f"Device '{self.ble.device_name}' not found.")
                self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Connect BLE"))
                return
            self.log("Connected. Discovering services...")
            await self._discover_task()
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Disconnect"))
            self.after(0, lambda: self.btn_refresh.config(state=tk.NORMAL))
        except Exception as e:
            self.log(f"Connection error: {e}")
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Connect BLE"))

    async def _discover_task(self):
        try:
            await self.ble.discover_services()
            self.log(f"Discovered {len(self.ble.characteristics)} characteristic(s).")
            self.after(0, self.refresh_char_list)
        except Exception as e:
            self.log(f"Discovery error: {e}")

    async def _disconnect_task(self):
        await self.ble.disconnect()
        self.log("Disconnected.")
        self.after(0, lambda: self.btn_connect.config(text="Connect BLE"))
        self.after(0, lambda: self.btn_refresh.config(state=tk.DISABLED))
        self.after(0, lambda: self.tree.delete(*self.tree.get_children()))


if __name__ == "__main__":
    app = RunITGUI()
    app.mainloop()
