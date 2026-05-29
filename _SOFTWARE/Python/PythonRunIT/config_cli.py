import asyncio
import ctypes as ct
from bleak import BleakScanner, BleakClient

# Importowanie wszystkich typów z C
from ConfigTypes import (
    packet_header_t,
    cfg_pwr_packet_type_e, cfg_pwr_reg_en_t, cfg_pwr_reg_settings_t, cfg_pwr_reg_limits_t, 
    cfg_pwr_reg_behavior_t, cfg_pwr_supply_t, cfg_pwr_supply_limits_t, cfg_pwr_supply_behavior_t,
    cfg_io_packet_type_e, cfg_io_gpio_mode_t, cfg_io_gpio_adc_alert_t, cfg_io_gpio_pwm_freq_t, cfg_io_gpio_reset_t,
    cfg_io_adc_window_mode_e, cfg_io_gpio_mode_e,
    cfg_sys_packet_type_e, cfg_log_t, cfg_system_ctrl_t, cfg_log_level_e
)

# ============================================================================
# BLE KONFIGURACJA
# ============================================================================
DEVICE_NAME = "runit"
UUID_WRITE = "00000000-0000-0000-0000-000000000003"
UUID_READ  = "00000000-0000-0000-0000-000000000002"

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
    print("  pwr_beh <reg> <w> <c> <scp> <ovp> <ocp> - Ustaw zachowanie regulatora (flagi 0/1)")
    print("  sup_set <v> <c> <wv> <wc> <nv> <nc>   - Skonfiguruj zasilanie główne (supply)")
    print("  sup_lim <w_tot> <c_tot>               - Ustaw całkowite limity mocy zasilania (mW)")
    print("  sup_beh <w> <c>                       - Ustaw zachowanie głównego zasilania (flagi 0/1)")
    
    print("\nIO (Piny):")
    print("  io_mode <pin_id> <mode>               - Ustaw tryb pinu (np. 6 dla ADC)")
    print("  io_adc <pin_id> <up_mV> <down_mV> <hyst_mV> <count> <win_mode(0/1)> - Konfiguruj alert ADC")
    print("  io_pwm <pin_id> <freq_hz>             - Ustaw częstotliwość PWM pinu")
    print("  io_reset <pin_id>                     - Zresetuj pin")
    
    print("\nSYS (System):")
    print("  sys_log <str(0/1)> <mir(0/1)> <lvl>   - Skonfiguruj logi (Stream, Mirror, Level 0-5)")
    print("  sys_ctrl <ovp0> <ocp0> <scp0> <ovp1> <ocp1> <scp1> <w0> <c0> <w1> <c1> <wsys> <csys> - Ustawienia kontroli INA3221")
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

        elif cmd == "pwr_beh":
            s = cfg_pwr_reg_behavior_t(reg_number=int(parts[1]), over_budget_warning=int(parts[2]), 
                                       over_budget_critical=int(parts[3]), off_on_short_circuit=int(parts[4]),
                                       off_on_over_voltage=int(parts[5]), off_on_over_current=int(parts[6]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_REG_BEHAVIOR, s))

        elif cmd == "sup_set":
            s = cfg_pwr_supply_t(provided_input_voltage_mv=int(parts[1]), provided_input_current_ma=int(parts[2]),
                                 input_voltage_warning_mV=int(parts[3]), input_current_warning_ma=int(parts[4]),
                                 input_voltage_to_negotiate_mv=int(parts[5]), input_current_to_negotiate_ma=int(parts[6]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_SUPPLY, s))

        elif cmd == "sup_lim":
            s = cfg_pwr_supply_limits_t(power_warning_total_mW=int(parts[1]), power_critical_total_mW=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_SUPPLY_LIMITS, s))

        elif cmd == "sup_beh":
            s = cfg_pwr_supply_behavior_t(over_budget_warning=int(parts[1]), over_budget_critical=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_PWR, cfg_pwr_packet_type_e.CFG_PWR_TYPE_SUPPLY_BEHAVIOR, s))

        # ================= IO COMMANDS =================
        elif cmd == "io_mode":
            s = cfg_io_gpio_mode_t(pin_id=int(parts[1]), mode=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_MODE, s))
            
        elif cmd == "io_adc":
            s = cfg_io_gpio_adc_alert_t(
                pin_id=int(parts[1]), 
                adc_threshold_up_mv=int(parts[2]),
                adc_threshold_down_mv=int(parts[3]), 
                adc_threshold_hysteresis_mv=int(parts[4]), 
                adc_event_counter_threshold=int(parts[5]),
                adc_window_mode=int(parts[6])
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_ADC_ALERT, s))

        elif cmd == "io_pwm":
            s = cfg_io_gpio_pwm_freq_t(pin_id=int(parts[1]), freq_hz=int(parts[2]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_PWM_FREQ, s))

        elif cmd == "io_reset":
            s = cfg_io_gpio_reset_t(pin_id=int(parts[1]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_IO, cfg_io_packet_type_e.CFG_IO_TYPE_GPIO_RESET, s))

        # ================= SYS COMMANDS =================
        elif cmd == "sys_log":
            s = cfg_log_t(enable_stream=bool(int(parts[1])), mirror_on_serial=bool(int(parts[2])), esp_log_level=int(parts[3]))
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_LOG_CONFIG, s))

        elif cmd == "sys_ctrl":
            s = cfg_system_ctrl_t(
                crt_reg0_ovp=int(parts[1]), crt_reg0_ocp=int(parts[2]), crt_reg0_scp=int(parts[3]),
                crt_reg1_ovp=int(parts[4]), crt_reg1_ocp=int(parts[5]), crt_reg1_scp=int(parts[6]),
                crt_current_REG0_WARN=int(parts[7]), crt_current_REG0_CRIT=int(parts[8]),
                crt_current_REG1_WARN=int(parts[9]), crt_current_REG1_CRIT=int(parts[10]),
                crt_current_SYS_PWR_WARN=int(parts[11]), crt_current_SYS_PWR_CRIT=int(parts[12])
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_SYSTEM_CTRL, s))

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
            s_ctrl = cfg_system_ctrl_t(
                crt_reg0_ovp=1, crt_reg0_ocp=1, crt_reg0_scp=1, crt_reg1_ovp=1, crt_reg1_ocp=1, crt_reg1_scp=1,
                crt_current_REG0_WARN=85, crt_current_REG0_CRIT=100, crt_current_REG1_WARN=85, crt_current_REG1_CRIT=100,
                crt_current_SYS_PWR_WARN=90, crt_current_SYS_PWR_CRIT=110
            )
            packets.append(build_packet(packet_header_t.PACKET_H_CFG_SYS, cfg_sys_packet_type_e.CFG_SYS_TYPE_SYSTEM_CTRL, s_ctrl))
            
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