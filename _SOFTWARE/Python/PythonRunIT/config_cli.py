import asyncio
import ctypes as ct
from bleak import BleakScanner, BleakClient

# Importowanie wszystkich typów z C
from ConfigTypes import (
    packet_header_t,
    cfg_pwr_packet_type_e, cfg_pwr_reg_en_t, cfg_pwr_reg_settings_t, cfg_pwr_reg_limits_t, 
    cfg_pwr_reg_behavior_t, cfg_pwr_supply_t, cfg_pwr_current_behavior_t, cfg_pwr_error_behavior_e,
    cfg_io_packet_type_e, cfg_io_gpio_mode_t, cfg_io_gpio_adc_alert_t, cfg_io_gpio_pwm_freq_t, cfg_io_gpio_reset_t,
    cfg_io_adc_window_mode_e, cfg_io_gpio_mode_e, cfg_gpio_intr_mode_e, cfg_gpio_intr_mode_t,
    cfg_sys_packet_type_e, cfg_log_t, cfg_log_level_e
)

# ============================================================================
# BLE KONFIGURACJA
# ============================================================================
DEVICE_NAME = "runit"
UUID_WRITE = "00000000-0000-0000-0000-000000000003"
UUID_READ  = "00000000-0000-0000-0000-000000000002"

# ============================================================================
# PARSOWANIE ENUMÓW
# ============================================================================
def parse_enum(enum_cls, val_str):
    try:
        return int(val_str)
    except ValueError:
        val_str_upper = val_str.upper()
        if val_str_upper in enum_cls.__members__:
            return enum_cls[val_str_upper].value
        for name, member in enum_cls.__members__.items():
            if name.endswith(val_str_upper) or name.endswith("_" + val_str_upper):
                return member.value
        raise ValueError(f"Nieznana wartość '{val_str}' dla enuma {enum_cls.__name__}")

# ============================================================================
# BUDOWANIE PAKIETU
# ============================================================================
def build_packet(major_header: int, minor_header: int, struct_payload: ct.Structure = None) -> bytes:
    payload_bytes = bytes(struct_payload) if struct_payload else b""
    return bytes([major_header, minor_header]) + payload_bytes

# ============================================================================
# PARSER KOMEND
# ============================================================================
def print_help():
    print("\n--- Dostępne Komendy ---")
    
    print("POWER (Zasilanie):")
    print("  pwr_en <reg_num> <en(0/1)>            - Włącz/Wyłącz regulator")
    print("  pwr_set <reg_num> <mV> <mA>           - Ustaw napięcie (mV) i limit prądu (mA)")
    print("  pwr_lim <w0> <c0> <w1> <c1>           - Ustaw limity mocy (mW) dla reg 0 i 1 (ostrzegawcze i krytyczne)")
    print("  pwr_reg_beh <ovp0> <ocp0> <scp0> <ovp1> <ocp1> <scp1> - Ustaw zachowanie błędów regulatorów (enum np. AUTOMATIC, STOP)")
    print("  pwr_cur_beh <w0> <c0> <w1> <c1> <wsys> <csys> - Ustaw zachowanie dla ostrzeżeń i błędów prądowych (enum np. AUTOMATIC, STOP)")
    print("  sup_set <v> <c> <wv> <wc> <nv> <nc>   - Skonfiguruj zasilanie główne (supply)")
    
    print("\nIO (Piny):")
    print("  io_mode <pin_id> <mode>               - Ustaw tryb pinu (enum np. ADC, INPUT_PULLUP, OUTPUT_PUSH_PULL)")
    print("  io_adc <pin_id> <up_mV> <down_mV> <hyst_mV> <count> <win_mode> - Konfiguruj alert ADC (enum OUTSIDE, INSIDE)")
    print("  io_intr <pin_id> <mode>               - Ustaw przerwanie na pinie (enum np. RISING_EDGE, BOTH_EDGES)")
    print("  io_pwm <pin_id> <freq_hz>             - Ustaw częstotliwość PWM pinu")
    print("  io_reset <pin_id>                     - Zresetuj pin")
    
    print("\nSYS (System):")
    print("  sys_log <str(0/1)> <mir(0/1)> <lvl>   - Skonfiguruj logi (Stream, Mirror, Level enum np. INFO, DEBUG)")
    print("  sys_def                               - Przywróć domyślne ustawienia urządzeń")
    print("  sys_vm_run                            - Uruchom maszynę wirtualną (demo)")
    print("  sys_vm_stop                           - Zatrzymaj maszynę wirtualną")
    
    print("\nMAKRA / DEMO:")
    print("  demo_adc <pin_id>                     - Sekwencja testowa dla ADC (10mV - 3000mV)")
    print("  demo_log                              - Włącza logi systemowe na poziom INFO")
    print("  demo_ina3221                          - Przesyła testową konfigurację dla układu zasilania")
    
    print("\nINNE:")
    print("  raw <hex_bytes>                       - Wyślij surowe bajty (np. 'raw 01 02 FF')")
    print("  help                                  - Wyświetla to menu")
    print("  q / quit / exit                       - Zakończ")
    print("--------------------------\n")

def parse_command(user_input: str) -> list:
    parts = user_input.strip().split()
    if not parts:
        return []
        
    cmd = parts[0].lower()
    packets = []

    try:
        # ================= POWER COMMANDS =================
        if cmd == "pwr_en":
            s = cfg_pwr_reg_en_t(regulator_num=int(parts[1]), enable=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_EN, s))
            
        elif cmd == "pwr_set":
            s = cfg_pwr_reg_settings_t(regulator_num=int(parts[1]), voltage_mv=int(parts[2]), current_limit_ma=int(parts[3]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_SETTINGS, s))

        elif cmd == "pwr_lim":
            s = cfg_pwr_reg_limits_t(power_warning_reg_0_mW=int(parts[1]), power_critical_reg_0_mW=int(parts[2]), 
                                     power_warning_reg_1_mW=int(parts[3]), power_critical_reg_1_mW=int(parts[4]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_LIMITS, s))

        elif cmd == "pwr_reg_beh":
            s = cfg_pwr_reg_behavior_t(
                behavior_reg0_ovp=parse_enum(cfg_pwr_error_behavior_e, parts[1]), behavior_reg0_ocp=parse_enum(cfg_pwr_error_behavior_e, parts[2]),
                behavior_reg0_scp=parse_enum(cfg_pwr_error_behavior_e, parts[3]), behavior_reg1_ovp=parse_enum(cfg_pwr_error_behavior_e, parts[4]),
                behavior_reg1_ocp=parse_enum(cfg_pwr_error_behavior_e, parts[5]), behavior_reg1_scp=parse_enum(cfg_pwr_error_behavior_e, parts[6])
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_BEHAVIOR, s))

        elif cmd == "sup_set":
            s = cfg_pwr_supply_t(provided_input_voltage_mv=int(parts[1]), provided_input_current_ma=int(parts[2]),
                                 input_voltage_warning_mV=int(parts[3]), input_current_warning_ma=int(parts[4]),
                                 input_voltage_to_negotiate_mv=int(parts[5]), input_current_to_negotiate_ma=int(parts[6]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_SUPPLY, s))

        elif cmd == "pwr_cur_beh":
            s = cfg_pwr_current_behavior_t(
                behavior_current_REG0_WARN=parse_enum(cfg_pwr_error_behavior_e, parts[1]), behavior_current_REG0_CRIT=parse_enum(cfg_pwr_error_behavior_e, parts[2]),
                behavior_current_REG1_WARN=parse_enum(cfg_pwr_error_behavior_e, parts[3]), behavior_current_REG1_CRIT=parse_enum(cfg_pwr_error_behavior_e, parts[4]),
                behavior_current_SYS_PWR_WARN=parse_enum(cfg_pwr_error_behavior_e, parts[5]), behavior_current_SYS_PWR_CRIT=parse_enum(cfg_pwr_error_behavior_e, parts[6])
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_CURRENT_BEHAVIOR, s))

        # ================= IO COMMANDS =================
        elif cmd == "io_mode":
            mode = parse_enum(cfg_io_gpio_mode_e, parts[2])
            s = cfg_io_gpio_mode_t(pin_id=int(parts[1]), mode=mode)
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_MODE, s))
            
        elif cmd == "io_adc":
            win_mode = parse_enum(cfg_io_adc_window_mode_e, parts[6])
            s = cfg_io_gpio_adc_alert_t(
                pin_id=int(parts[1]), 
                adc_threshold_up_mv=int(parts[2]),
                adc_threshold_down_mv=int(parts[3]), 
                adc_threshold_hysteresis_mv=int(parts[4]), 
                adc_event_counter_threshold=int(parts[5]),
                adc_window_mode=win_mode
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_ADC_ALERT, s))

        elif cmd == "io_intr":
            mode = parse_enum(cfg_gpio_intr_mode_e, parts[2])
            s = cfg_gpio_intr_mode_t(pin_id=int(parts[1]), cfg_gpio_intr_mode=mode)
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_INTERRUPT, s))

        elif cmd == "io_pwm":
            s = cfg_io_gpio_pwm_freq_t(pin_id=int(parts[1]), freq_hz=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_PWM_FREQ, s))

        elif cmd == "io_reset":
            s = cfg_io_gpio_reset_t(pin_id=int(parts[1]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_RESET, s))

        # ================= SYS COMMANDS =================
        elif cmd == "sys_log":
            lvl = parse_enum(cfg_log_level_e, parts[3])
            s = cfg_log_t(enable_stream=bool(int(parts[1])), mirror_on_serial=bool(int(parts[2])), esp_log_level=lvl)
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_LOG_CONFIG, s))

        elif cmd == "sys_def":
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_DEVICE_DEFAULT))

        elif cmd == "sys_vm_run":
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_VM_RUN))
            
        elif cmd == "sys_vm_stop":
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_VM_STOP))

        # ================= MAKRA / DEMO =================
        elif cmd == "demo_adc":
            pin = int(parts[1])
            print(f"[*] Przygotowanie pinu {pin} do trybu ADC...")
            s_mode = cfg_io_gpio_mode_t(pin_id=pin, mode=cfg_io_gpio_mode_e.CFG_GPIO_MODE_ADC)
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_MODE, s_mode))
            
            print(f"[*] Konfiguracja alertów ADC dla pinu {pin} (Outside Window 10mV - 3000mV)...")
            s_alert = cfg_io_gpio_adc_alert_t(
                pin_id=pin, 
                adc_threshold_up_mv=3000, 
                adc_threshold_down_mv=10, 
                adc_threshold_hysteresis_mv=50,
                adc_event_counter_threshold=1, 
                adc_window_mode=cfg_io_adc_window_mode_e.CFG_IO_ADC_WINDOW_OUTSIDE
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_ADC_ALERT, s_alert))

        elif cmd == "demo_log":
            print("[*] Konfiguracja logów systemowych (Stream: TAK, Mirror: TAK, Poziom: INFO)")
            s_log = cfg_log_t(enable_stream=True, mirror_on_serial=True, esp_log_level=cfg_log_level_e.CFG_ESP_LOG_INFO)
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_LOG_CONFIG, s_log))

        elif cmd == "demo_ina3221":
            print("[*] Konfiguracja układu zasilania i INA3221...")
            
            s_cur = cfg_pwr_current_behavior_t(
                behavior_current_REG0_WARN=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC, behavior_current_REG0_CRIT=cfg_pwr_error_behavior_e.CFG_CTRL_ENTER_EMERGENCY,
                behavior_current_REG1_WARN=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC, behavior_current_REG1_CRIT=cfg_pwr_error_behavior_e.CFG_CTRL_ENTER_EMERGENCY,
                behavior_current_SYS_PWR_WARN=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC, behavior_current_SYS_PWR_CRIT=cfg_pwr_error_behavior_e.CFG_CTRL_STOP
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_CURRENT_BEHAVIOR, s_cur))
            
            s_reg = cfg_pwr_reg_behavior_t(
                behavior_reg0_ovp=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC, behavior_reg0_ocp=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC,
                behavior_reg0_scp=cfg_pwr_error_behavior_e.CFG_CTRL_STOP, behavior_reg1_ovp=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC,
                behavior_reg1_ocp=cfg_pwr_error_behavior_e.CFG_CTRL_AUTOMATIC, behavior_reg1_scp=cfg_pwr_error_behavior_e.CFG_CTRL_STOP
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_BEHAVIOR, s_reg))
            
            s_lim = cfg_pwr_reg_limits_t(
                power_warning_reg_0_mW=1200, power_critical_reg_0_mW=1500,
                power_warning_reg_1_mW=1200, power_critical_reg_1_mW=1500
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_LIMITS, s_lim))

        # ================= INNE =================
        elif cmd == "raw":
            hexstr = "".join(parts[1:]).replace(' ', '')
            packets.append(bytes.fromhex(hexstr))
            
        elif cmd in ("help", "h"):
            print_help()
            
        else:
            print(f"Nieznana komenda: '{cmd}'. Wpisz 'help', aby zobaczyć opcje.")

    except IndexError:
        print(f"Błąd: Za mało argumentów dla komendy '{cmd}'. Sprawdź 'help'.")
    except ValueError as e:
        print(f"Błąd: Wprowadzono nieprawidłową wartość. ({e})")
    except Exception as e:
        print(f"Nieoczekiwany błąd podczas formatowania komendy: {e}")
        
    return packets

# ============================================================================
# HANDLERY BLE
# ============================================================================
async def rx_handler(sender, data: bytearray):
    print(f"\n[RX] {len(data)} bajtów: {data.hex().upper()}")
    try:
        print(f"[ASCII] {data.decode('utf-8', errors='replace')}")
    except:
        pass
    print("> ", end="", flush=True)

async def send_bytes(client, write_char, data: bytes):
    await client.write_gatt_char(write_char, data, response=True)
    print(f"[TX] Wysłano {len(data)} bajtów: {data.hex().upper()}")

async def main():
    print(f"Skanowanie w poszukiwaniu urządzenia '{DEVICE_NAME}'...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME or ad.local_name == DEVICE_NAME,
        timeout=10
    )

    if not device:
        print("Nie znaleziono urządzenia. Upewnij się, że jest włączone i rozgłasza BLE.")
        return

    print(f"Znaleziono: {device.name} ({device.address})")

    async with BleakClient(device) as client:
        print("Połączono!")

        read_char = client.services.get_characteristic(UUID_READ)
        write_char = client.services.get_characteristic(UUID_WRITE)
        
        if write_char is None or read_char is None:
            print("Brak wymaganych charakterystyk w urządzeniu.")
            return
            
        await client.start_notify(UUID_READ, rx_handler)
        print(f"Nasłuchiwanie uruchomione na: {UUID_READ}")

        print_help()
        loop = asyncio.get_running_loop()
        
        while True:
            try:
                user = await loop.run_in_executor(None, input, "> ")
                if not user or not user.strip():
                    continue
                    
                user = user.strip()
                if user.lower() in ('q', 'quit', 'exit'):
                    print("Rozłączanie...")
                    break
                    
                packets_to_send = parse_command(user)
                
                for pkt in packets_to_send:
                    await send_bytes(client, write_char, pkt)
                    await asyncio.sleep(0.05)
                    
            except KeyboardInterrupt:
                print("\nRozłączanie...")
                break

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Przerwano.")