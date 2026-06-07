import tkinter as tk
from tkinter import ttk, scrolledtext
import asyncio
import threading
import sys
import ctypes as ct
import inspect
from bleak import BleakScanner, BleakClient

import config_cli
import ConfigTypes

class StdoutRedirector:
    """Przechwytuje 'print()' z aplikacji i przekierowuje do GUI."""
    def __init__(self, log_func):
        self.log_func = log_func
        self.buffer = ""

    def write(self, string):
        self.buffer += string
        if '\n' in self.buffer:
            lines = self.buffer.split('\n')
            for line in lines[:-1]:
                if line:
                    self.log_func(line)
            self.buffer = lines[-1]

    def flush(self):
        pass

class ConfigGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("RunIT - Konfigurator Systemu")
        self.geometry("750x700")
        
        self.client = None
        self.write_char = None
        
        # Przygotowanie asynchronicznego wątku dla komunikacji BLE
        self.loop = asyncio.new_event_loop()
        
        self.build_ui()
        
        # Przekierowanie konsoli do okienka "Logi" na dole
        sys.stdout = StdoutRedirector(self.log)
        
        # Start BLE w tle
        threading.Thread(target=self.start_async_loop, daemon=True).start()

    def build_ui(self):
        # === GÓRA: Połączenie i Akcja ===
        top_frame = ttk.Frame(self)
        top_frame.pack(fill=tk.X, padx=10, pady=10)
        
        self.btn_connect = ttk.Button(top_frame, text="Połącz BLE", command=self.on_connect_click)
        self.btn_connect.pack(side=tk.LEFT, padx=5)
        
        self.btn_send = ttk.Button(top_frame, text="Wyślij Pakiet", command=self.on_send_click, state=tk.DISABLED)
        self.btn_send.pack(side=tk.RIGHT, padx=5)
        
        self.btn_stream = ttk.Button(top_frame, text="Włącz Logi ESP", command=self.on_stream_click, state=tk.DISABLED)
        self.btn_stream.pack(side=tk.RIGHT, padx=5)

        # === ŚRODEK: Konstruktor Pakietu ===
        packet_frame = ttk.LabelFrame(self, text="Konstruktor Pakietu (Automatyczny)")
        packet_frame.pack(fill=tk.X, padx=10, pady=5)
        
        ttk.Label(packet_frame, text="Moduł (Major):").grid(row=0, column=0, padx=10, pady=5, sticky=tk.E)
        self.major_var = tk.StringVar()
        self.major_cb = ttk.Combobox(packet_frame, textvariable=self.major_var, state="readonly", width=40)
        self.major_cb.grid(row=0, column=1, padx=10, pady=5, sticky=tk.W)
        self.major_cb.bind("<<ComboboxSelected>>", self.on_major_select)
        
        ttk.Label(packet_frame, text="Akcja (Minor):").grid(row=1, column=0, padx=10, pady=5, sticky=tk.E)
        self.minor_var = tk.StringVar()
        self.minor_cb = ttk.Combobox(packet_frame, textvariable=self.minor_var, state="readonly", width=40)
        self.minor_cb.grid(row=1, column=1, padx=10, pady=5, sticky=tk.W)
        self.minor_cb.bind("<<ComboboxSelected>>", self.on_minor_select)
        
        ttk.Label(packet_frame, text="Typ Payloadu:").grid(row=2, column=0, padx=10, pady=5, sticky=tk.E)
        self.struct_var = tk.StringVar()
        self.struct_entry = ttk.Entry(packet_frame, textvariable=self.struct_var, state="readonly", width=43)
        self.struct_entry.grid(row=2, column=1, padx=10, pady=5, sticky=tk.W)

        # === ŚRODEK 2: Formularz ze zmiennymi ===
        self.form_frame = ttk.LabelFrame(self, text="Dane Payloadu")
        self.form_frame.pack(fill=tk.X, padx=10, pady=5)
        self.field_vars = {}
        
        # === DÓŁ: Logi systemu i BLE ===
        log_frame = ttk.LabelFrame(self, text="Logi")
        log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, font=("Consolas", 9))
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Dynamiczne wypełnianie dostępnych typów z ConfigTypes.py
        self.major_enum = ConfigTypes.packet_header_t
        self.major_cb['values'] = [e.name for e in self.major_enum]
        
        self.all_structs = {
            name: cls for name, cls in inspect.getmembers(ConfigTypes, inspect.isclass)
            if issubclass(cls, ct.LittleEndianStructure) and cls is not ct.LittleEndianStructure
        }
        
        if self.major_cb['values']:
            self.major_cb.current(0)
            self.on_major_select(None)

    def log(self, msg):
        # Bezpieczne aktualizowanie interfejsu (Tkinter) z innych wątków
        self.after(0, self._append_log, msg)

    def _append_log(self, msg):
        self.log_text.insert(tk.END, str(msg) + "\n")
        self.log_text.see(tk.END)

    def get_minor_enum(self, major_name):
        if "PWR" in major_name: return ConfigTypes.cfg_pwr_packet_type_e
        if "IO" in major_name: return ConfigTypes.cfg_io_packet_type_e
        if "SYS" in major_name: return ConfigTypes.cfg_sys_packet_type_e
        if "TESTS" in major_name: return ConfigTypes.cfg_test_packet_type_e
        return None

    def on_major_select(self, event):
        major_name = self.major_var.get()
        minor_enum = self.get_minor_enum(major_name)
        if minor_enum:
            self.minor_cb['values'] = [e.name for e in minor_enum]
            if self.minor_cb['values']:
                self.minor_cb.current(0)
            self.on_minor_select(None)
        else:
            self.minor_cb['values'] = []
            self.minor_var.set("")

    def on_minor_select(self, event):
        minor_name = self.minor_var.get()
        if not minor_name: return
        best_match = "[Brak Payloadu]"
        
        # Nowe inteligentne dopasowanie po adnotacjach (hintach z C)
        for s_name, s_cls in self.all_structs.items():
            if hasattr(s_cls, "_packet_header_"):
                if s_cls._packet_header_.name == minor_name:
                    best_match = s_name
                    break
        
        # Fallback na stare zgadywanie jeśli brak adnotacji
        if best_match == "[Brak Payloadu]":
            base = minor_name.replace("_TYPE_", "_").lower()
            guess = base + "_t"
            if guess in self.all_structs:
                best_match = guess
            else:
                # Awaryjne przypisanie w razie odchylenia nazwy
                if "ADC_ALERT" in minor_name: best_match = "cfg_io_gpio_adc_alert_t"
                elif "INTERRUPT" in minor_name: best_match = "cfg_gpio_intr_mode_t"
                elif "LOG_CONFIG" in minor_name: best_match = "cfg_log_t"
                elif "SYSTEM_CTRL" in minor_name: best_match = "cfg_system_ctrl_t"
                elif "VM_" in minor_name or "DEFAULT" in minor_name: best_match = "[Brak Payloadu]"
        
        self.struct_var.set(best_match)
        self.on_struct_select(None)

    def on_struct_select(self, event):
        for w in self.form_frame.winfo_children():
            w.destroy()
        self.field_vars.clear()
        
        struct_name = self.struct_var.get()
        if struct_name == "[Brak Payloadu]" or struct_name not in self.all_structs:
            ttk.Label(self.form_frame, text="Pusty pakiet (Brak payloadu).").pack(padx=10, pady=10)
            return
            
        struct_cls = self.all_structs[struct_name]
        hints = getattr(struct_cls, "_hints_", {})
        
        for i, field_info in enumerate(struct_cls._fields_):
            field_name = field_info[0]
            field_type = field_info[1]
            
            ttk.Label(self.form_frame, text=field_name + ":").grid(row=i, column=0, padx=10, pady=4, sticky=tk.E)
            
            if "pin_id" in field_name:
                pin_frame = ttk.Frame(self.form_frame)
                pin_frame.grid(row=i, column=1, padx=10, pady=4, sticky=tk.W)

                port_var = tk.StringVar(value="0")
                pin_num_var = tk.StringVar(value="0")
                
                ttk.Label(pin_frame, text="Port:").pack(side=tk.LEFT)
                port_entry = ttk.Entry(pin_frame, textvariable=port_var, width=5)
                port_entry.pack(side=tk.LEFT, padx=(0, 5))

                ttk.Label(pin_frame, text="Pin:").pack(side=tk.LEFT)
                pin_num_entry = ttk.Entry(pin_frame, textvariable=pin_num_var, width=5)
                pin_num_entry.pack(side=tk.LEFT, padx=(0, 10))

                calc_label_var = tk.StringVar()
                ttk.Label(pin_frame, text="=").pack(side=tk.LEFT)
                calc_label = ttk.Label(pin_frame, textvariable=calc_label_var, font=("Consolas", 9), foreground="blue")
                calc_label.pack(side=tk.LEFT, padx=5)

                def update_pin_id(*args):
                    try:
                        port = int(port_var.get() or "0")
                        pin = int(pin_num_var.get() or "0")
                        pin_id = (port << 8) | pin
                        calc_label_var.set(f"{pin_id} (0x{pin_id:04X})")
                    except ValueError:
                        calc_label_var.set("Błąd")

                port_var.trace_add("write", update_pin_id)
                pin_num_var.trace_add("write", update_pin_id)
                update_pin_id()

                self.field_vars[field_name] = ((port_var, pin_num_var), field_type)
            
            elif field_name in hints:
                var = tk.StringVar()
                self.field_vars[field_name] = (var, field_type)
                enum_cls = hints[field_name]
                enum_names = [e.name for e in enum_cls]
                cb = ttk.Combobox(self.form_frame, textvariable=var, values=enum_names, state="readonly", width=42)
                if enum_names: cb.current(0)
                cb.grid(row=i, column=1, padx=10, pady=4, sticky=tk.W)
            elif field_type == ct.c_bool:
                var = tk.StringVar()
                self.field_vars[field_name] = (var, field_type)
                cb = ttk.Combobox(self.form_frame, textvariable=var, values=["False", "True"], state="readonly", width=42)
                cb.current(0)
                cb.grid(row=i, column=1, padx=10, pady=4, sticky=tk.W)
            else:
                var = tk.StringVar()
                self.field_vars[field_name] = (var, field_type)
                ent = ttk.Entry(self.form_frame, textvariable=var, width=45)
                ent.insert(0, "0")
                ent.grid(row=i, column=1, padx=10, pady=4, sticky=tk.W)

    def on_send_click(self):
        major_name = self.major_var.get()
        minor_name = self.minor_var.get()
        struct_name = self.struct_var.get()
        
        if not major_name or not minor_name:
            self.log("Błąd: Wybierz Major i Minor header.")
            return
            
        major_val = getattr(self.major_enum, major_name).value
        minor_enum = self.get_minor_enum(major_name)
        minor_val = getattr(minor_enum, minor_name).value
        
        payload_bytes = b""
        if struct_name != "[Brak Payloadu]" and struct_name in self.all_structs:
            struct_cls = self.all_structs[struct_name]
            hints = getattr(struct_cls, "_hints_", {})
            inst = struct_cls()
            
            for field_name, (var, f_type) in self.field_vars.items():
                # Specjalna obsługa kalkulatora pin_id
                if isinstance(var, tuple):
                    port_var, pin_num_var = var
                    try:
                        port = int(port_var.get() or "0")
                        pin = int(pin_num_var.get() or "0")
                        pin_id = (port << 8) | pin
                        setattr(inst, field_name, pin_id)
                        continue # Przejdź do następnego pola
                    except ValueError:
                        self.log(f"Błąd: Nieprawidłowy numer portu lub pinu dla '{field_name}'")
                        return

                val_str = var.get().strip()
                if not val_str:
                    self.log(f"Błąd: Wypełnij pole '{field_name}'")
                    return
                    
                try:
                    if field_name in hints:
                        enum_cls = hints[field_name]
                        if hasattr(enum_cls, val_str):
                            setattr(inst, field_name, getattr(enum_cls, val_str).value)
                        else:
                            setattr(inst, field_name, int(val_str, 0))
                    elif f_type == ct.c_bool:
                        setattr(inst, field_name, val_str == "True")
                    else:
                        setattr(inst, field_name, int(val_str, 0))
                except Exception as e:
                    self.log(f"Błąd parsowania pola '{field_name}': {e}")
                    return
                    
            payload_bytes = bytes(inst)
            
        packet = bytes([major_val, minor_val]) + payload_bytes
        self.log(f"Wysyłam pakiet: Major={major_name}, Minor={minor_name}")
        asyncio.run_coroutine_threadsafe(self.send_bytes_task(packet), self.loop)

    def on_stream_click(self):
        self.log("Wysyłam żądanie uruchomienia strumienia logów (Auto-Pakiet)...")
        major_val = ConfigTypes.packet_header_t.PACKET_H_CFG_SYS.value
        minor_val = ConfigTypes.cfg_sys_packet_type_e.CFG_SYS_TYPE_LOG_CONFIG.value
        inst = ConfigTypes.cfg_log_t()
        inst.enable_stream = True
        inst.mirror_on_serial = True
        inst.esp_log_level = ConfigTypes.cfg_log_level_e.CFG_ESP_LOG_INFO.value
        packet = bytes([major_val, minor_val]) + bytes(inst)
        asyncio.run_coroutine_threadsafe(self.send_bytes_task(packet), self.loop)

    def on_connect_click(self):
        if self.client and self.client.is_connected:
            asyncio.run_coroutine_threadsafe(self.disconnect_task(), self.loop)
        else:
            self.btn_connect.config(state=tk.DISABLED, text="Łączenie...")
            asyncio.run_coroutine_threadsafe(self.connect_task(), self.loop)

    def start_async_loop(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def rx_handler(self, sender, data: bytearray):
        try:
            ascii_data = data.decode('utf-8', errors='replace').strip()
            if ascii_data:
                self.log(f"[RX] {ascii_data}")
        except: pass

    async def connect_task(self):
        self.log(f"Szukam urządzenia '{config_cli.DEVICE_NAME}'...")
        try:
            device = await BleakScanner.find_device_by_filter(lambda d, ad: d.name == config_cli.DEVICE_NAME or ad.local_name == config_cli.DEVICE_NAME, timeout=5)
            if not device:
                self.log("Nie znaleziono urządzenia BLE.")
                self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Połącz BLE"))
                return

            self.log(f"Znaleziono {device.name}. Łączenie...")
            self.client = BleakClient(device)
            await self.client.connect()
            self.write_char = self.client.services.get_characteristic(config_cli.UUID_WRITE)
            await self.client.start_notify(config_cli.UUID_READ, self.rx_handler)
            
            self.log("Połączono pomyślnie z urządzeniem!")
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Rozłącz BLE"))
            self.after(0, lambda: self.btn_send.config(state=tk.NORMAL))
            self.after(0, lambda: self.btn_stream.config(state=tk.NORMAL))
            
        except Exception as e:
            self.log(f"Błąd BLE: {e}")
            self.after(0, lambda: self.btn_connect.config(state=tk.NORMAL, text="Połącz BLE"))

    async def disconnect_task(self):
        if self.client: await self.client.disconnect()
        self.log("Rozłączono.")
        self.after(0, lambda: self.btn_connect.config(text="Połącz BLE"))
        self.after(0, lambda: self.btn_send.config(state=tk.DISABLED))
        self.after(0, lambda: self.btn_stream.config(state=tk.DISABLED))

    async def send_bytes_task(self, packet: bytes):
        if not self.client or not self.client.is_connected: return
        try:
            await self.client.write_gatt_char(self.write_char, packet, response=True)
            self.log(f"[TX HEX] {packet.hex().upper()}")
            await asyncio.sleep(0.05)
        except Exception as e:
            self.log(f"Błąd wysyłania: {e}")

if __name__ == "__main__":
    app = ConfigGUI()
    app.mainloop()