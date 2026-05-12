## 1 Features

- Programmable power supply (PPS) support for USB power delivery (USB PD)
- -3.0-V to 30-V wide input voltage range
- -0.8-V to 22-V with 10-mV step programmable output voltage range
- -±1% reference voltage accuracy
- -Adjustable output voltage compensation for voltage droop over the cable
- -Programmable output current limit up to 6.35 A with 50-mA step
- -±5% accurate output current monitoring
- -I 2 C interface
- High efficiency over entire load range
- -96% efficiency at VIN  = 12 V, VOUT = 20 V, and IOUT = 3 A
- -Programmable PFM and FPWM mode at light load
- Avoid frequency interference and crosstalk
- -Optional clock synchronization
- -Programmable switching frequency from 200 kHz to 2.2 MHz
- EMI mitigation
- -Optional programmable spread spectrum
- -Lead-less package
- Rich protection features
- -Output overvoltage protection
- -Hiccup mode for output short-circuit protection

[TPS55289](https://www.ti.com/product/TPS55289)

SLVSGA9A - MARCH 2022 - REVISED AUGUST 2022

![Image](output_part1_artifacts\image_000000_5060f44b853965d0436ef421b41df4e647575e99f155946528e9c75335c635c8.png)

![Image](output_part1_artifacts\image_000001_6d0da1f1b517d86c8097f4f2f8eb9e83725447d5dd669f75e67e49cb079bbcfb.png)

## TPS55289 30-V, 8-A Buck-Boost Converter with I 2 C Interface

The TPS55289 has up to 30-V input voltage capability. Through the I 2 C interface, the output voltage  of  the  TPS55289  can  be  programmed  from 0.8 V to 22 V with 10-mV step. When working in boost mode,  the  device  can  deliver  60  W  from  12-V  input voltage. It is capable of delivering 45 W from 9-V input voltage.

The  TPS55289  employs  an  average  current-mode control scheme. The switching frequency is programmable  from  200  kHz  to  2.2  MHz  by  an external  resistor  and  can  be  synchronized  to  an external clock. The TPS55289 also provides optional spread spectrum to minimize peak EMI.

The  TPS55289  offers  output  overvoltage  protection, average  inductor  current  limit,  cycle-by-cycle  peak current limit, and output short circuit protection. The  TPS55289  also  makes  sure  it  safely  operates with  optional  output  current  limit  and  hiccup-mode protection in sustained overload conditions.

The TPS55289 can use a small inductor and small  capacitors  with  high  switching  frequency.  It  is available in a 3.0-mm × 5.0-mm QFN package.

## Device Information

| PART NUMBER   | PACKAGE (1)   | BODY SIZE       |
|---------------|---------------|-----------------|
| TPS55289      | VQFN-HR       | 3.0 mm × 5.0 mm |

- (1) For all available packages, see the orderable addendum at the end of the data sheet.
2. -Thermal shutdown protection
3. -8-A average inductor current limit
- Small solution size
5. -Maximum switching frequency up to 2.2 MHz
6. -3.0-mm × 5.0-mm HotRod ™  QFN package

## 2 Applications

- [Wireless charger](https://www.ti.com/solution/hev-ev-on-board-obc-wireless-charger?keyMatch=wireless%20charger&tisearch=Portal%20Search%20Application-&INTC=Portal%20Search%20Application-)
- [USB PD](https://www.ti.com/interface/usb/type-c-and-power-delivery/overview.html)
- [Docking station](https://www.ti.com/solution/docking-station)
- [Industrial PC](https://www.ti.com/solution/computer-on-module)
- [Power bank](https://www.ti.com/solution/power-bank)
- [Monitor](https://www.ti.com/solution/flat-panel-monitor)

## 3 Description

The TPS55289 is a synchronous buck-boost converter that is optimized  for converting battery voltage  or  adapter  voltage  into  power  supply  rails. The  TPS55289  integrates  four  MOSFET  switches, providing a compact solution for USB power delivery (USB PD) application.

![Image](output_part1_artifacts\image_000002_99a20f6d02bcd8152a1fbdbba737969fd6a6b5b4b13354e1b309a3f90fa04324.png)

![Image](output_part1_artifacts\image_000003_657fd75a39d8a4e8f55366fea66f3a3174085f9898fb0410c8e05701b56ce87c.png)

Typical Application Circuit

![Image](output_part1_artifacts\image_000004_8109a3cb8507c24f81188ec0d91f2722b67e87269ab915f9700654767a95f1ca.png)

![Image](output_part1_artifacts\image_000005_fc4241a5feaf55607c5b02a4bfb7eeed7e9a1f6726dd7c2100b21266522330ba.png)

![Image](output_part1_artifacts\image_000006_4b465568d5f80caca39eb067af3b084a22a011b50e1a9f490ad7e5df9fd46673.png)

## Table of Contents

| 1 Features ............................................................................1   | 7.5 Programming............................................................ 22          |
|--------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------|
| 2 Applications .....................................................................1      | 7.6 Register Maps...........................................................25          |
| 3 Description .......................................................................1     | 8 Application and Implementation ..................................33                   |
| 4 Revision History .............................................................. 2        | 8.1 Application Information............................................. 33             |
| 5 Pin Configuration and Functions ...................................3                     | 8.2 Typical Application....................................................33           |
| 6 Specifications ..................................................................5       | 9 Power Supply Recommendations ................................41                       |
| 6.1 Absolute Maximum Ratings........................................ 5                     | 10 Layout ...........................................................................41 |
| 6.2 ESD Ratings............................................................... 5           | 10.1 Layout Guidelines...................................................41             |
| 6.3 Recommended Operating Conditions.........................5                             | 10.2 Layout Example......................................................42             |
| 6.4 Thermal Information....................................................6               | 11 Device and Documentation Support ..........................43                        |
| 6.5 Electrical Characteristics.............................................6               | 11.1 Device Support........................................................43           |
| 6.6 I 2 C Timing Characteristics.......................................... 9               | 11.2 Receiving Notification of Documentation Updates..43                                |
| 6.7 Typical Characteristics..............................................10                | 11.3 Support Resources................................................. 43              |
| 7 Detailed Description ......................................................14            | 11.4 Trademarks.............................................................43          |
| 7.1 Overview...................................................................14          | 11.5 Electrostatic Discharge Caution..............................43                    |
| 7.2 Functional Block Diagram.........................................15                    | 11.6 Glossary..................................................................43       |
| 7.3 Feature Description...................................................15               | 12 Mechanical, Packaging, and Orderable                                                 |
| 7.4 Device Functional Modes..........................................21                    | Information .................................................................... 43     |
| 4 Revision History                                                                         | 4 Revision History                                                                      |
| Changes from Revision * (March 2022) to Revision A (August 2022)                           | Page                                                                                    |

![Image](output_part1_artifacts\image_000007_2c3de8d38d7127fc9191723744751ba19a2abda9341507814fc2a5026cf43b9b.png)

![Image](output_part1_artifacts\image_000008_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 5 Pin Configuration and Functions

Figure 5-1. 21-Pin VQFN-HR RYQ Package (Transparent Top View)

![Image](output_part1_artifacts\image_000009_269bc5ad5a38df8ce03e95c99d16d5a4cb550766ca5d3d5f4bb392b59a1ead87.png)

Table 5-1. Pin Functions

| Pin       | Pin   | I/O   | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|-----------|-------|-------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Name      | NO.   | I/O   | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| EN/UVLO   | 1     | I     | Enable logic input and programmable input voltage undervoltage lockout (UVLO) input. Logic high level enables the device. Logic low level disables the device and turns it into shutdown mode. After the voltage at the EN/UVLO pin is above the logic high voltage of 1.15 V, this pin acts as programmable UVLO input with 1.23-V internal reference.                                                                                                                                   |
| MODE      | 2     | I     | I 2 C target address selection. When it is connected to the logic high voltage, the I 2 C target address is 74H. When it is connected to the logic low voltage, the I 2 C target address is 75H.                                                                                                                                                                                                                                                                                          |
| SCL       | 3     | I     | Clock of I 2 C interface                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| SDA       | 4     | I/O   | Data of I 2 C interface                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| DITH/SYNC | 5     | I     | Dithering frequency setting and synchronous clock input. Use a capacitor between this pin and ground to set the dithering frequency. When this pin is short to ground or pulled above 1.2 V, there is no dithering function. An external clock can be applied at this pin to synchronize the switching frequency.                                                                                                                                                                         |
| FSW       | 6     | I     | The switching frequency is programmed by a resistor between this pin and the AGND pin.                                                                                                                                                                                                                                                                                                                                                                                                    |
| VIN       | 7     | PWR   | Input of the buck-boost converter                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| SW1       | 8     | PWR   | The switching node pin of the buck side. It is connected to the drain of the internal buck low-side power MOSFET and the source of internal buck high-side power MOSFET.                                                                                                                                                                                                                                                                                                                  |
| PGND      | 9     | PWR   | Power ground of the IC                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| SW2       | 10    | PWR   | The switching node pin of the boost side. It is connected to the drain of the internal boost low-side power MOSFET and the source of internal boost high-side power MOSFET.                                                                                                                                                                                                                                                                                                               |
| VOUT      | 11    | PWR   | Output of the buck-boost converter                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| ISP       | 12    | I     | Positive input of the current sense amplifier. An optional current sense resistor connected between the ISP pin and the ISN pin can limit the output current. If the sensed voltage reaches the current limit setting value in the register, a slow constant current control loop becomes active and starts to regulate the voltage between the ISP pin and the ISN pin. Connecting the ISP pin and the ISN pin together with the VOUT pin can disable the output current limit function. |

## Table 5-1. Pin Functions (continued)

| Pin    | Pin   | I/O   | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|--------|-------|-------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Name   | NO.   |       | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ISN    | 13    | I     | Negative input of the current sense amplifier. An optional current sense resistor connected between the ISP pin and the ISN pin can limit the output current. If the sensed voltage reaches the current limit setting value in the register, a slow constant current control loop becomes active and starts to regulate the voltage between the ISP pin and the ISN pin. Connecting the ISP pin and the ISN pin together with the VOUT pin can disable the output current limit function. |
| FB/INT | 14    | I/O   | When the device is set to use external output voltage feedback, connect to the center tap of a resistor divider to program the output voltage. When the device is set to use internal feedback, this pin is a fault indicator output. When there is an internal fault happening, this pin outputs logic low level.                                                                                                                                                                        |
| COMP   | 15    | O     | Output of the internal error amplifier. Connect the loop compensation network between this pin and the AGND pin.                                                                                                                                                                                                                                                                                                                                                                          |
| CDC    | 16    | O     | Voltage output proportional to the sensed voltage between the ISP pin and the ISN pin. Use a resistor between this pin and AGND to increase the output voltage to compensate voltage droop across the cable caused by the cable resistance.                                                                                                                                                                                                                                               |
| AGND   | 17    | -     | Signal ground of the IC                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| VCC    | 18    | O     | Output of the internal regulator. A ceramic capacitor of more than 4.7 μF is required between this pin and the AGND pin.                                                                                                                                                                                                                                                                                                                                                                  |
| BOOT2  | 19    | O     | Power supply for high-side MOSFET gate driver in boost side. A 0.1-µF ceramic capacitor must be connected between this pin and the SW2 pin.                                                                                                                                                                                                                                                                                                                                               |
| BOOT1  | 20    | O     | Power supply for high-side MOSFET gate driver in buck side. A 0.1-µF ceramic capacitor must be connected between this pin and the SW1 pin.                                                                                                                                                                                                                                                                                                                                                |
| EXTVCC | 21    | I     | Select the internal LDO or external 5 V for VCC. When it is connected to logic high voltage, select the internal LDO. When it is connected to logic low voltage, select the external 5 V for VCC.                                                                                                                                                                                                                                                                                         |

![Image](output_part1_artifacts\image_000010_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

![Image](output_part1_artifacts\image_000011_5cdb3ece6e156068c26945670e4028df3d1b7094651763fbf6e88cd107454cdc.png)

## 6 Specifications

## 6.1 Absolute Maximum Ratings

over operating junction temperature range (unless otherwise noted) (1)

|                           |                                                                | MIN       | MAX       | UNIT   |
|---------------------------|----------------------------------------------------------------|-----------|-----------|--------|
| Voltage range at pins (2) | VIN, SW1                                                       | -0.3      | 35        | V      |
| Voltage range at pins (2) | BOOT1                                                          | SW1 - 0.3 | SW1 + 6   | V      |
| Voltage range at pins (2) | VCC, SCL, SDA, FSW, COMP, FB/INT, MODE, CDC, DITH/SYNC, EXTVCC | -0.3      | 6         | V      |
| Voltage range at pins (2) | VOUT, SW2, ISP, ISN                                            | -0.3      | 25        | V      |
| Voltage range at pins (2) | EN/UVLO                                                        | -0.3      | 20        | V      |
| Voltage range at pins (2) | BOOT2                                                          | SW2 - 0.3 | SW2 + 6   | V      |
| Voltage range at pins (2) | SCL, SDA, FSW, COMP, FB/INT, MODE, CDC, DITH/SYNC, EXTVCC      | -0.3      | VCC + 0.3 | V      |
| T J                       | Junction temperature, T J (3)                                  | -40       | 150       | °C     |
| T stg                     | Storage temperature                                            | -65       | 150       | °C     |

## 6.2 ESD Ratings

|         |                                                                                                                   | VALUE   | UNIT   |
|---------|-------------------------------------------------------------------------------------------------------------------|---------|--------|
| V (ESD) | Human body model (HBM), per ANSI/ESDA/JEDEC JS-001 (1) Charged device model (CDM), per ANSI/ESDA/JEDEC JS-002 (2) | ±2000   | V      |
| V (ESD) |                                                                                                                   | ±500    | V      |

## 6.3 Recommended Operating Conditions

over operating junction temperature range (unless otherwise noted)

|       |                                    |   MIN |   NOM |   MAX | UNIT   |
|-------|------------------------------------|-------|-------|-------|--------|
| V IN  | Input voltage range                |   3.0 |       |    30 | V      |
| V OUT | Output voltage range               |   0.8 |       |    22 | V      |
| L     | Effective inductance range         |     1 |   4.7 |    10 | µH     |
| C IN  | Effective input capacitance range  |   4.7 |    22 |       | µF     |
| C OUT | Effective output capacitance range |    10 |   100 |  1000 | µF     |
| T J   | Operating junction temperature     |   -40 |       |   125 | °C     |

## 6.4 Thermal Information

| THERMAL METRIC (1)   | THERMAL METRIC (1)                           | RYQ (VQFN) 26 PINS Standard   | RYQ (VQFN) 26 PINS EVM (2)   | UNIT   |
|----------------------|----------------------------------------------|-------------------------------|------------------------------|--------|
| R θJA                | Junction-to-ambient thermal resistance       |                               | 27.5                         | °C/W   |
| R                    |                                              | 43.4                          |                              |        |
| θJC(top)             | Junction-to-case (top) thermal resistance    | 22.3                          | N/A                          | °C/W   |
| R θJB                | Junction-to-board thermal resistance         | 7.4                           | N/A                          | °C/W   |
| Ψ JT                 | Junction-to-top characterization parameter   | 0.7                           | 0.7                          | °C/W   |
| Y JB                 | Junction-to-board characterization parameter | 7.2                           | 11.1                         | °C/W   |
| R θJC(bot)           | Junction-to-case (bottom) thermal resistance | N/A                           | N/A                          | °C/W   |

## 6.5 Electrical Characteristics

TJ = -40°C to 125°C, VIN = 12 V and VOUT = 20 V. Typical values are at TJ = 25°C, unless otherwise noted.

| PARAMETER               | PARAMETER                                | TEST CONDITIONS                                                                                                               | MIN                    | TYP                    | MAX                    | UNIT                   |
|-------------------------|------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------|------------------------|------------------------|------------------------|------------------------|
| POWER SUPPLY            | POWER SUPPLY                             | POWER SUPPLY                                                                                                                  | POWER SUPPLY           | POWER SUPPLY           | POWER SUPPLY           | POWER SUPPLY           |
| V IN                    | Input voltage range                      |                                                                                                                               | 3.0                    |                        | 30                     | V                      |
| V                       | Under voltage lockout threshold          | V IN rising                                                                                                                   | 2.8                    | 2.9                    | 3.0                    | V                      |
| VIN_UVLO                |                                          | V IN falling                                                                                                                  | 2.6                    | 2.65                   | 2.7                    | V                      |
|                         | Quiescent current into VIN pin           | IC enabled, no load, no switching. V IN = 3.0 V to 24 V, V OUT = 0.8 V, V FB = V REF + 0.1 V, R FSW = 100 kΩ, T J up to 125°C |                        | 760                    | 860                    | µA                     |
| I Q                     | Quiescent current into VOUT pin          | IC enabled, no load, no switching, V IN = 3.0 V, V OUT = 3 V to 20 V, V FB = V REF + 0.1 V, R FSW = 100 kΩ, T J up to 125°C   |                        | 760                    | 860                    | µA                     |
| I SD                    | Shutdown current into VIN pin            | IC disabled, V IN = 3.0 V to 14 V, T J up to 125°C                                                                            |                        | 0.8                    | 3                      | µA                     |
| V CC                    | Internal regulator output                | I VCC = 50 mA, V IN = 8 V, V OUT = 20 V                                                                                       | 5.0                    | 5.2                    | 5.4                    | V                      |
| EN/UVLO                 | EN/UVLO                                  | EN/UVLO                                                                                                                       | EN/UVLO                | EN/UVLO                | EN/UVLO                | EN/UVLO                |
| V EN_H                  | EN logic high threshold                  | V CC = 3.0 V to 5.5 V                                                                                                         |                        |                        | 1.15                   | V                      |
| V EN_L                  | EN logic low threshold                   | V CC = 3.0 V to 5.5 V                                                                                                         | 0.4                    |                        |                        | V                      |
| V EN_HYS                | Enable threshold hysteresis              | V CC = 3.0 V to 5.5 V                                                                                                         | 0.04                   |                        |                        | V                      |
| V UVLO                  | UVLO rising threshold at the EN/UVLO pin | V CC = 3.0 V to 5.5 V                                                                                                         | 1.20                   | 1.23                   | 1.26                   | V                      |
| V UVLO_HYS              | UVLO threshold hysteresis                | V CC = 3.0 V to 5.5 V                                                                                                         |                        | 10                     |                        | mV                     |
| I UVLO                  | Sourcing current at the EN/UVLO pin      | V EN/UVLO = 1.3 V                                                                                                             | 4.5                    | 5                      | 5.5                    | µA                     |
| OUTPUT                  | OUTPUT                                   | OUTPUT                                                                                                                        | OUTPUT                 | OUTPUT                 | OUTPUT                 | OUTPUT                 |
| V OUT                   | Output voltage range                     |                                                                                                                               | 0.8                    |                        | 22                     | V                      |
| V OVP                   | Output overvoltage protection threshold  |                                                                                                                               | 22.5                   | 23.5                   | 24.5                   | V                      |
| V OVP_HYS               | Overvoltage protection hysteresis        |                                                                                                                               |                        | 1                      |                        | V                      |
| I FB_LKG                | Leakage current at FB pin                | Tj up to 125°C                                                                                                                |                        |                        | 100                    | nA                     |
| I VOUT_LKG              | Leakage current into VOUT pin            | IC disabled, V OUT = 20 V, V SW2 = 0 V, T J up to 125°C                                                                       |                        | 1                      | 20                     | µA                     |
| I DISCHG                | Output discharge current                 | V OUT = 20 V, V CC = 5.2 V                                                                                                    | 40                     | 100                    | 170                    | mA                     |
| INTERNAL REFERENCE DAC  | INTERNAL REFERENCE DAC                   | INTERNAL REFERENCE DAC                                                                                                        | INTERNAL REFERENCE DAC | INTERNAL REFERENCE DAC | INTERNAL REFERENCE DAC | INTERNAL REFERENCE DAC |
| Resolution of reference | voltage DAC                              |                                                                                                                               |                        | 11                     |                        | bits                   |

![Image](output_part1_artifacts\image_000012_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

![Image](output_part1_artifacts\image_000013_ee12c98fff45daf84ff5c9e9d915ee404e5616d07242502095da0dba013cc8c5.png)

[www.ti.com](https://www.ti.com/)

## 6.5 Electrical Characteristics (continued)

TJ = -40°C to 125°C, VIN = 12 V and VOUT = 20 V. Typical values are at TJ = 25°C, unless otherwise noted.

| PARAMETER                       | PARAMETER                                                        | TEST CONDITIONS                                      | MIN                             | TYP                             | MAX                             | UNIT                            |
|---------------------------------|------------------------------------------------------------------|------------------------------------------------------|---------------------------------|---------------------------------|---------------------------------|---------------------------------|
| V OUT_FULL                      | Output voltage when V REF is set to 1.129 V                      | VOUT_FS=03h, REF=0780h, V REF = 1.129 V              | 19.7                            | 20                              | 20.3                            | V                               |
| V OUT_FULL                      |                                                                  | VOUT_FS=02h, REF=0780h, V REF = 1.129 V              | 14.78                           | 15                              | 15.22                           | V                               |
| V OUT_FULL                      |                                                                  | VOUT_FS=01h, REF=0780h, V REF = 1.129 V              | 9.85                            | 10                              | 10.15                           | V                               |
| V OUT_FULL                      |                                                                  | VOUT_FS=00h, REF=0780h, V REF = 1.129 V              | 4.93                            | 5                               | 5.07                            | V                               |
| V OUT_ZERO                      | Output voltage when V REF is set to 45 mV                        | VOUT_FS=03h, REF=0000h, V REF = 45 mV                | 0.74                            | 0.8                             | 0.86                            | V                               |
| V OUT_ZERO                      | Output voltage when V REF is set to 45 mV                        | VOUT_FS=02h, REF=0000h, V REF = 45 mV                | 0.55                            | 0.6                             | 0.65                            | V                               |
| V OUT_ZERO                      | Output voltage when V REF is set to 45 mV                        | VOUT_FS=01h, REF=0000h, V REF = 45 mV                | 0.36                            | 0.4                             | 0.44                            | V                               |
| V OUT_ZERO                      | Output voltage when V REF is set to 45 mV                        | VOUT_FS=00h, REF=0000h, V REF = 45 mV                | 0.18                            | 0.2                             | 0.22                            | V                               |
| REFERENCE VOLTAGE               | REFERENCE VOLTAGE                                                | REFERENCE VOLTAGE                                    | REFERENCE VOLTAGE               | REFERENCE VOLTAGE               | REFERENCE VOLTAGE               | REFERENCE VOLTAGE               |
| V REF                           | Reference voltage at the FB/INT pin when using external feedback | External feedback with REF=0780H                     | 1.117                           | 1.129                           | 1.141                           | V                               |
| V REF                           |                                                                  | External feedback with REF=058CH                     | 0.837                           | 0.846                           | 0.855                           | V                               |
| V REF                           |                                                                  | External feedback with REF=0334H                     | 0.502                           | 0.508                           | 0.514                           | V                               |
| V REF                           |                                                                  | External feedback with REF=01A4H                     | 0.276                           | 0.282                           | 0.288                           | V                               |
| POWER SWITCH                    | POWER SWITCH                                                     | POWER SWITCH                                         | POWER SWITCH                    | POWER SWITCH                    | POWER SWITCH                    | POWER SWITCH                    |
| R DS(on)                        | Low-side MOSFET on resistance on buck side                       | V OUT = 20 V, V CC = 5.2 V                           |                                 | 22                              |                                 | mΩ                              |
| R DS(on)                        | High-side MOSFET on resistance on buck side                      | V OUT = 20 V, V CC = 5.2 V                           |                                 | 14                              |                                 | mΩ                              |
| R DS(on)                        | Low-side MOSFET on resistance on boost side                      | V OUT = 20 V, V CC = 5.2 V                           |                                 | 11                              |                                 | mΩ                              |
| R DS(on)                        | High-side MOSFET on resistance on boost side                     | V OUT = 20 V, V CC = 5.2 V                           |                                 | 11                              |                                 | mΩ                              |
| INTERNAL CLOCK                  | INTERNAL CLOCK                                                   | INTERNAL CLOCK                                       | INTERNAL CLOCK                  | INTERNAL CLOCK                  | INTERNAL CLOCK                  | INTERNAL CLOCK                  |
| f                               | Switching frequency                                              | R FSW = 100 k                                        | 180                             | 200                             | 220                             | kHz                             |
| SW                              |                                                                  | R FSW = 8.4 k                                        | 2000                            | 2200                            | 2400                            | kHz                             |
| t OFF_min                       | Minimum off time                                                 | Boost mode                                           |                                 | 90                              | 145                             | ns                              |
| t ON_min                        | Minimum on time                                                  | Buck mode                                            |                                 | 90                              | 130                             | ns                              |
| V SW                            | Voltage at the FSW pin                                           |                                                      |                                 | 1                               |                                 | V                               |
| CURRENT LIMIT                   | CURRENT LIMIT                                                    | CURRENT LIMIT                                        | CURRENT LIMIT                   | CURRENT LIMIT                   | CURRENT LIMIT                   | CURRENT LIMIT                   |
| I LIM_AVG                       | Average inductor current limit                                   | V IN = 8 V, V OUT = 20 V, f SW = 400 kHz, FPWM       | 6.7                             | 8                               |                                 | A                               |
| I LIM_AVG                       |                                                                  | V IN = 8 V, V OUT = 20 V, f SW = 400 kHz, PFM        | 6.7                             | 8                               |                                 | A                               |
| I LIM_PK                        | Peak inductor current limit at boost                             | V IN = 8 V, V OUT = 20 V, f SW = 400 kHz, FPWM       |                                 | 13                              |                                 | A                               |
| I LIM_PK                        | high side                                                        | V IN = 8 V, V OUT = 20 V, f SW = 400 kHz, PFM        |                                 | 13                              |                                 | A                               |
| V SNS                           | Current loop regulation voltage between ISP and ISN pin          | V ISN = 2 V to 21 V, IOUT_LIMIT register = 10111100b | 28.5                            | 30                              | 31.5                            | mV                              |
| V SNS                           |                                                                  | V ISN = 2 V to 21 V, IOUT_LIMIT register = 11100100b | 48                              | 50                              | 52                              | mV                              |
| CABLE VOLTAGE DROP COMPENSATION | CABLE VOLTAGE DROP COMPENSATION                                  | CABLE VOLTAGE DROP COMPENSATION                      | CABLE VOLTAGE DROP COMPENSATION | CABLE VOLTAGE DROP COMPENSATION | CABLE VOLTAGE DROP COMPENSATION | CABLE VOLTAGE DROP COMPENSATION |

## 6.5 Electrical Characteristics (continued)

TJ = -40°C to 125°C, VIN = 12 V and VOUT = 20 V. Typical values are at TJ = 25°C, unless otherwise noted.

| PARAMETER         | PARAMETER                          | TEST CONDITIONS                                                   | MIN               | TYP               | MAX               | UNIT              |
|-------------------|------------------------------------|-------------------------------------------------------------------|-------------------|-------------------|-------------------|-------------------|
| V CDC             | Voltage at the CDC pin             | R CDC = 20 kΩ or floating, V ISP - V ISN = 50 mV                  | 0.93              | 1                 | 1.05              | V                 |
|                   |                                    | R CDC = 20 kΩ or floating, V ISP - V ISN = 2 mV                   |                   | 40                | 75                | mV                |
| V OUT_CDC         |                                    | Internal output feedback, CDC[2:0]=111, V ISP - V ISN = 50 mV     | 650               | 700               | 750               | mV                |
|                   | VOUT increase for cable drop       | Internal output feedback, CDC[2:0]=111, V ISP - V ISN = 2 mV      |                   | 30                | 60                | mV                |
|                   | compensation                       | Internal output feedback, CDC[2:0]=001, V ISP - V ISN = 50 mV     | 70                | 100               | 130               | mV                |
|                   |                                    | Internal output feedback, CDC[2:0]=001, V ISP - V ISN = 10 mV     |                   | 20                | 40                | mV                |
| I FB_CDC          | FB/INT pin sinking current         | External output feedback, R CDC = 20 kΩ, V ISP - V ISN = 50 mV    | 7.23              | 7.5               | 7.87              | µA                |
|                   |                                    | External output feedback, R CDC = 20 kΩ, V ISP - V ISN = 0 mV     |                   | 0                 | 0.3               | µA                |
|                   |                                    | External output feedback, R CDC = floating, V ISP - V ISN = 50 mV |                   | 0                 | 0.3               | µA                |
| ERROR AMPLIFIER   | ERROR AMPLIFIER                    | ERROR AMPLIFIER                                                   | ERROR AMPLIFIER   | ERROR AMPLIFIER   | ERROR AMPLIFIER   | ERROR AMPLIFIER   |
| I SINK            | COMP pin sink current              | V FB = V REF + 400 mV, V COMP = 1.1 V, V CC = 5 V                 |                   | 20                |                   | µA                |
| I SOURCE          | COMP pin source current            | V FB = V REF - 400 mV, V COMP = 1.1 V, V CC = 5 V                 |                   | 60                |                   | µA                |
| V CCLPH           | High clamp voltage at the COMP pin |                                                                   |                   | 1.2               |                   | V                 |
| V CCLPL           | Low clamp voltage at the COMP pin  |                                                                   |                   | 0.7               |                   | V                 |
| G EA              | Error amplifier transconductance   |                                                                   |                   | 190               |                   | µA/V              |
| SOFT START        | SOFT START                         | SOFT START                                                        | SOFT START        | SOFT START        | SOFT START        | SOFT START        |
| t SS              | Soft-start time                    |                                                                   | 2.5               | 3.6               | 5                 | ms                |
| SPREAD SPECTRUM   | SPREAD SPECTRUM                    | SPREAD SPECTRUM                                                   | SPREAD SPECTRUM   | SPREAD SPECTRUM   | SPREAD SPECTRUM   | SPREAD SPECTRUM   |
| I DITH_CHG        | Dithering charge current           | V DITH/SYNC = 1.0 V; R FSW = 49.9 kΩ; voltage rising from 0.9 V   |                   | 2                 |                   | µA                |
| I DITH_DIS        | Dithering discharge current        | V DITH/SYNC = 1.0 V; R FSW = 49.9 kΩ; voltage falling from 1.1 V  |                   | 2                 |                   | µA                |
| V DITH_H          | Dither high threshold              |                                                                   |                   | 1.07              |                   | V                 |
| V DITH_L          | Dither low threshold               |                                                                   |                   | 0.93              |                   | V                 |
| SYNCHRONOUS CLOCK | SYNCHRONOUS CLOCK                  | SYNCHRONOUS CLOCK                                                 | SYNCHRONOUS CLOCK | SYNCHRONOUS CLOCK | SYNCHRONOUS CLOCK | SYNCHRONOUS CLOCK |
| V SNYC_H          | Sync clock high voltage threshold  |                                                                   |                   |                   | 1.2               | V                 |
| V SYNC_L          | Sync clock low voltage threshold   |                                                                   | 0.4               |                   |                   | V                 |
| t SYNC_MIN        | Minimum sync clock pulse width     |                                                                   | 50                |                   |                   | ns                |
| HICCUP            | HICCUP                             | HICCUP                                                            | HICCUP            | HICCUP            | HICCUP            | HICCUP            |
| t HICCUP          | Hiccup off time                    |                                                                   |                   | 76                |                   | ms                |
| MODE              | MODE                               | MODE                                                              | MODE              | MODE              | MODE              | MODE              |
| V MODE_H          | MODE logic high threshold          | V CC = 3.0 V to 5.5 V                                             |                   |                   | 1.2               | V                 |
| V MODE_L          | MODE logic low threshold           | V CC = 3.0 V to 5.5 V                                             | 0.4               |                   |                   | V                 |
| EXTVCC            | EXTVCC                             | EXTVCC                                                            | EXTVCC            | EXTVCC            | EXTVCC            | EXTVCC            |
| V EXTVCC_H        | EXTVCC logic high threshold        | V CC = 3.0 V to 5.5 V                                             |                   |                   | 1.2               | V                 |
| V EXTVCC_L        | EXTVCC logic low threshold         | V CC = 3.0 V to 5.5 V                                             | 0.4               |                   |                   | V                 |
| LOGIC INTERFACE   | LOGIC INTERFACE                    | LOGIC INTERFACE                                                   | LOGIC INTERFACE   | LOGIC INTERFACE   | LOGIC INTERFACE   | LOGIC INTERFACE   |

![Image](output_part1_artifacts\image_000014_ed093ec6c2ab71c6635d5d6f87931a5bb77489e419023e31a0ce2c7a7ed6572b.png)

![Image](output_part1_artifacts\image_000015_b7e4588988faf64b5c9044e963b62758a82e8eb75f32c9ce19803ec6d094040b.png)

## 6.5 Electrical Characteristics (continued)

TJ = -40°C to 125°C, VIN = 12 V and VOUT = 20 V. Typical values are at TJ = 25°C, unless otherwise noted.

| PARAMETER   | PARAMETER                                                      | TEST CONDITIONS        | MIN        | TYP        | MAX        | UNIT       |
|-------------|----------------------------------------------------------------|------------------------|------------|------------|------------|------------|
| V I2C_IO    | IO voltage range for I 2 C                                     |                        | 1.7        |            | 5.5        | V          |
| V I2C_H     | I 2 C input high threshold                                     | V CC = 3.0 V to 5.5 V  |            |            | 1.2        | V          |
| V I2C_L     | I 2 C input low threshold                                      | V CC = 3.0 V to 5.5 V  | 0.4        |            |            | V          |
| I FB/INT_H  | Leakage current into FB/INT pin when outputting high impedance | V FB/INT = 5 V         |            |            | 100        | nA         |
| V FB/INT_L  | Output low voltage range of the FB/ INT pin                    | Sinking 4-mA current   |            | 0.03       | 0.1        | V          |
| PROTECTION  | PROTECTION                                                     | PROTECTION             | PROTECTION | PROTECTION | PROTECTION | PROTECTION |
| T SD        | Thermal shutdown threshold                                     | T J rising             |            | 175        |            | °C         |
| T SD_HYS    | Thermal shutdown hysteresis                                    | T J falling below T SD |            | 20         |            | °C         |

## 6.6 I 2 C Timing Characteristics

TJ = -40°C to 125°C, VIN = 12 V and VOUT = 20 V. Typical values are at TJ  = 25°C, unless otherwise noted.

| PARAMETER   | PARAMETER                                                                     | TEST CONDITIONS   | MIN        | MAX        | UNIT       |            |
|-------------|-------------------------------------------------------------------------------|-------------------|------------|------------|------------|------------|
| I2C TIMING  | I2C TIMING                                                                    | I2C TIMING        | I2C TIMING | I2C TIMING | I2C TIMING | I2C TIMING |
| f SCL       | SCL clock frequency                                                           |                   | 100        | 1000       | kHz        |            |
| t BUF       | Bus free time between a STOP and START condition                              | Fast mode plus    | 0.5        |            | µs         |            |
| t HD(STA)   | Hold time (repeated) START condition                                          |                   | 260        |            | ns         |            |
| t LOW       | Low period of the SCL clock                                                   |                   | 0.5        |            | µs         |            |
| t HIGH      | High period of the SCL clock                                                  |                   | 260        |            | ns         |            |
| t SU(STA)   | Setup time for a repeated START condition                                     |                   | 260        |            | ns         |            |
| t SU(DAT)   | Data setup time                                                               |                   | 50         |            | ns         |            |
| t HD(DAT)   | Data hold time                                                                |                   | 0          |            | µs         |            |
| t RCL       | Rise time of SCL signal                                                       |                   |            | 120        | ns         |            |
| t RCL1      | Rise time of SCL signal after a repeated START condition and after an ACK bit |                   |            | 120        | ns         |            |
| t FCL       | Fall time of SCL signal                                                       |                   |            | 120        | ns         |            |
| t RDA       | Rise time of SDA signal                                                       |                   |            | 120        | ns         |            |
| t FDA       | Fall time of SDA signal                                                       |                   |            | 120        | ns         |            |
| t SU(STO)   | Setup time of STOP condition                                                  |                   | 260        |            | ns         |            |
| C B         | Capacitive load for SDA and SCL                                               |                   |            | 200        | pF         |            |

## 6.7 Typical Characteristics

VIN = 12 V, T A = 25°C, fSW = 400 kHz, unless otherwise noted

![Image](output_part1_artifacts\image_000016_51133fccd0793f68b2fc288307be94477edb7073536f92b2e741fac727b6da01.png)

![Image](output_part1_artifacts\image_000017_2003953cd80da6143d48fbfabfbe8874e19d013e5deb5982caf95575947d8c4f.png)

![Image](output_part1_artifacts\image_000018_e621311c01e9e12d685cdf97caf32d359f82d770d35fa9417af29cb75af1cb60.png)

## 6.7 Typical Characteristics (continued)

![Image](output_part1_artifacts\image_000019_b3165751c44cb558a9041fa32d92d5a9fca5f589cb0eb614bd302c2beb994f90.png)

![Image](output_part1_artifacts\image_000020_4361a09dfee51443e226fae8c4c2ddee42548470667b045bb18cc73bc2840869.png)

## 6.7 Typical Characteristics (continued)

![Image](output_part1_artifacts\image_000021_e5b40da4fc5e32ff3c611e8f3db94525166b1896d554466e81f8f7e2de7d1306.png)

![Image](output_part1_artifacts\image_000022_335309de30b7244b510ca8db4b87748f85fa45750ec08367de6aab160e1f5892.png)

![Image](output_part1_artifacts\image_000023_3d76b52e5ef66a9d7bafa5f5938135029f59dcf41c40db1e205eb60bb5ff6355.png)

## 6.7 Typical Characteristics (continued)

![Image](output_part1_artifacts\image_000024_3800473ecf8009d95c7a50bbbe8724d1c4bb6f74b74d5d86f5094cd01534062a.png)

Figure 6-19. Switching Frequency vs Temperature

![Image](output_part1_artifacts\image_000025_5d1dc6f6f2ea631ac95e40626c6fc27041df78d60b6c3c859827a33450d0b9e9.png)

## 7 Detailed Description

## 7.1 Overview

The TPS55289 is a 8-A buck-boost DC-to-DC converter with the four integrated MOSFETs. The TPS55289 can operate  over  a  wide  range  of  3.0-V  to  30-V  input  voltage  and  0.8-V  to  22-V  output  voltage.  The  device  can smoothly transition amongst buck mode, buck-boost mode, and boost mode according to the input voltage and the set output voltage. The TPS55289 operates in buck mode when the input voltage is greater than the output voltage and in boost mode when the input voltage is less than the output voltage. When the input voltage is close to the output voltage, the TPS55289 alternates between one-cycle buck mode and one-cycle boost mode.

The TPS55289 uses an average current mode control scheme. Current mode control provides simplified loop compensation,  rapid  response  to  the  load  transients,  and  inherent  line  voltage  rejection.  An  error  amplifier compares the feedback voltage with the internal reference voltage. The output of the error amplifier determines the average inductor current.

An internal oscillator can be configured to operate over a wide range of frequency from 200 kHz to 2.2 MHz. The internal oscillator can also synchronize to an external clock applied to the DITH/SYNC pin. To minimize EMI, the TPS55289 can dither the switching frequency at ±7% of the set frequency.

The TPS55289 works in fixed-frequency PWM mode at moderate to heavy load currents. In light load condition, the TPS55289 can be configured to automatically transition to PFM mode or be forced in PWM mode by setting the corresponding bit in an internal register.

The  output  voltage  of  the  TPS55289  is  adjustable  by  setting  the  internal  register  through  I 2 C  interface.  An internal 11-bit DAC adjusts the reference voltage related to the value written into the REF register. The device can  also  limit  the  output  current  by  placing  a  current  sense  resistor  in  the  output  path.  These  two  functions support the programmable power supply (PPS) feature of the USB PD.

The TPS55289 provides average inductor current limit of 8 A typically. In addition, the device provides cycle-bycycle peak inductor current limit during transient to protect the device against overcurrent condition beyond the capability of the device.

A precision voltage threshold of 1.23 V with 5-µA sourcing current at the EN/UVLO pin supports programmable input undervoltage lockout (UVLO) with hysteresis. The output overvoltage protection (OVP) feature turns off the high-side FETs to prevent damage to the devices powered by the TPS55289.

The device provides a hiccup mode option to reduce the heating in the power components when the output short  circuit  happens.  When  the  hiccup  mode  is  enabled,  the  TPS55289  turns  off  for  76  ms  and  restarts  at soft-start-up.

![Image](output_part1_artifacts\image_000026_3df2d927883b16963ccefed27555ab3626c52d428a67dbbd6d2dda3455fa761a.png)

![Image](output_part1_artifacts\image_000027_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 7.2 Functional Block Diagram

![Image](output_part1_artifacts\image_000028_5c56c6b82eaf60a80764ee01ac2e3121d904a864d5f410e31980bd55812e830f.png)

## 7.3 Feature Description

## 7.3.1 VCC Power Supply

An internal  LDO  to  supply  the  TPS55289  outputs  regulated  5.2-V  voltage  at  the  VCC  pin.  When  V IN is  less than VOUT, the internal LDO selects the power supply source by comparing VIN to a rising threshold of 6.2 V with 0.3-V hysteresis. When VIN is higher than 6.2 V, the supply for LDO is VIN. When VIN is lower than 5.9 V, the supply for LDO is VOUT. When VOUT is less than VIN, the internal LDO selects the power supply source by comparing VOUT to a rising threshold of 6.2 V with 0.3-V hysteresis. When VOUT is higher than 6.2 V, the supply for LDO is VOUT. When VOUT is lower than 5.9 V, the supply for LDO is VIN. Table 7-1 shows the supply source selection for the internal LDO.

Table 7-1. VCC Power Supply Logic

| V IN         | V OUT         | Input for V CC LDO   |
|--------------|---------------|----------------------|
| V IN > 6.2 V | V OUT > V IN  | V IN                 |
| V IN < 5.9 V | V OUT > V IN  | V OUT                |
| V IN > V OUT | V OUT > 6.2 V | V OUT                |
| V IN > V OUT | V OUT < 5.9 V | V IN                 |

## 7.3.2 EXTVCC Power Supply

To minimize the power dissipation of the internal LDO when both input voltage and output voltage are high, an external  5-V  power  source  can  be  applied  at  the  VCC  pin  to  supply  the  TPS55289.  The  external  5-V  power supply must have at least 100-mA output current capability and must be within the 4.75-V to 5.5-V regulation range. When the EXTVCC pin is connected to logic low, the device selects the external power supply to supply the device through VCC pin. When the EXTVCC pin is connected to logic high, the device selects internal LDO.

## 7.3.3 Operation Mode Setting

By configuring the MODE pin logic status, the TPS55289 selects two different I 2 C addresses. Table 7-2 shows the I 2 C target address setting.

Table 7-2. I 2 C Target Address Setting

| MODE Pin   | I 2 C Target Address   |
|------------|------------------------|
| Low        | 75h                    |
| High       | 74h                    |

## 7.3.4 Input Undervoltage Lockout

When the input voltage is below 2.6 V, the TPS55289 is disabled. When the input voltage is above 3 V, the TPS55289 can be enabled by pulling the EN pin to a high voltage above 1.3 V.

## 7.3.5 Enable and Programmable UVLO

The TPS55289 has a dual function enable and undervoltage lockout (UVLO) circuit. When the input voltage at the VIN pin is above the input UVLO rising threshold of 3 V and the EN/UVLO pin is pulled above 1.15 V but less than the enable UVLO threshold of 1.23 V, the TPS55289 is enabled but still in standby mode. The TPS55289 starts to detect the MODE pin logic status and select the I 2 C target address.

The EN/UVLO pin has an accurate UVLO voltage threshold to support programmable input undervoltage lockout with hysteresis. When the EN/UVLO pin voltage is greater than the UVLO threshold of 1.23 V, the TPS55289 is  enabled  for  I 2 C  communication  and  switching  operation.  A  hysteresis  current,  I UVLO\_HYS, is  sourced  out  of the  EN/UVLO pin to provide hysteresis that prevents on/off chattering in the presence of noise with a slowly changing input voltage.

By using resistor divider as shown in Figure 7-1, the turn-on threshold is calculated using Equation 1.

<!-- formula-not-decoded -->

## where

- VUVLO is the UVLO threshold of 1.23 V at the EN/UVLO pin.

The hysteresis between the UVLO turn-on threshold and turn-off threshold is set by the upper resistor in the EN/UVLO resistor divider and is given by Equation 2.

<!-- formula-not-decoded -->

## where

- IUVLO\_HYS is the sourcing current from the EN/UVLO pin when the voltage at the EN/UVLO pin is above VUVLO.

![Image](output_part1_artifacts\image_000029_335309de30b7244b510ca8db4b87748f85fa45750ec08367de6aab160e1f5892.png)

![Image](output_part1_artifacts\image_000030_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

Figure 7-1. Programmable UVLO With Resistor Divider at the EN/UVLO Pin

![Image](output_part1_artifacts\image_000031_ac970516a1f411bc72f5f1f90d474a0c4c1dfc758865904ad1d9e46c55a099e9.png)

Using an NMOSFET together with a resistor divider can implement both logic enable and programmable UVLO as  shown  in  Figure  7-2.  The  EN  logic  high  level  must  be  greater  than  the  enable  threshold  plus  the  V th of the NMOSFET Q1. The Q1 also eliminates the leakage current from VIN to ground through the UVLO resistor divider during shutdown mode.

Figure 7-2. Logic Enable and Programmable UVLO

![Image](output_part1_artifacts\image_000032_afa1d52110e45eb95f8c63a372a06906987201f50369820c46f631ae0c30a163.png)

## 7.3.6 Soft Start

When the input voltage is above the UVLO threshold and the voltage at the EN/UVLO pin is above the enable UVLO threshold, the TPS55289 is ready to accept the command from the I 2 C controller device. An I 2 C controller device can configure the internal registers of the TPS55289 before setting the OE bit of the register 06h. Once an I 2 C controller device sets the OE bit to 1, the TPS55289 starts to ramp up the output voltage by ramping an internal reference voltage from 0 V to a voltage set in the internal registers 00h and 01h within 3.6 ms (typical).

## 7.3.7 Shutdown and Load Discharge

When the EN/UVLO pin voltage is pulled below 0.4 V, the TPS55289 is in shutdown mode, and all functions are disabled. All internal registers are reset to default values.

When the  EN/UVLO pin  is  at  a  high  logic  level  and  the  OE  bit  is  cleared  to  0,  the  TPS55289  turns  off  the switching operation but keeps the I 2 C interface active. Simultaneously, if the DISCHG bit in the register 06h is set to 1, the TPS55289 discharges the output voltage below 0.8 V by an internal constant current.

![Image](output_part1_artifacts\image_000033_4361a09dfee51443e226fae8c4c2ddee42548470667b045bb18cc73bc2840869.png)

## 7.3.8 Switching Frequency

The TPS55289 uses a fixed frequency average current control scheme. The switching frequency is between 200 kHz and 2.2 MHz set by placing a resistor at the FSW pin. An internal amplifier holds this pin at a fixed voltage of 1 V. The setting resistance is between maximum of 100 kΩ and minimum of 8.4 kΩ. Use Equation 3 to calculate the resistance by a given switching frequency.

where

<!-- formula-not-decoded -->

- RFSW is the resistance at the FSW pin.

For noise-sensitive applications, the TPS55289 can be synchronized to an external clock signal applied to the DITH/SYNC pin. The duty cycle of the external clock is recommended in the range of 30% to 70%. A resistor also must be connected to the FSW pin when the TPS55289 is switching by the external clock. The external clock frequency at the DITH/SYNC pin must have lower than 0.4-V low level voltage and must be within ±30% of the corresponding frequency set by the resistor. Figure 7-3 is a recommended configuration.

Figure 7-3. External Clock Configuration

![Image](output_part1_artifacts\image_000034_2a11ce55877f8fb2a2e9a8386164bf55af05f3bf4a3ac35e460ab630034dd422.png)

## 7.3.9 Switching Frequency Dithering

The  TPS55289 provides  an  optional  switching  frequency  dithering  that  is  enabled  by  connecting  a  capacitor from the DITH/SYNC pin to ground. Figure 7-4 illustrates the dithering circuit. By charging and discharging the capacitor, a triangular waveform centered at 1 V is generated at the DITH/SYNC pin. The triangular waveform modulates the oscillator frequency by ±7% of the nominal frequency set by the resistance at the FSW pin. The capacitance at the DITH/SYNC pin sets the modulation frequency. A small capacitance modulates the oscillator frequency at a faster rate than a large capacitance. For the dithering circuit to effectively reduce peak EMI, the modulation rate normally is below 1 kHz. Equation 4 calculates the capacitance required to set the modulation frequency, FMOD.

<!-- formula-not-decoded -->

## where

- RFSW is the switching frequency setting resistance (Ω) at the FSW pin.
- FMOD is the modulation frequency (Hz) of the dithering.

Connecting the DITH/SYNC pin below 0.4 V or above 1.2 V disables switching frequency dithering. The dithering function also is disabled when an external synchronous clock is used.

![Image](output_part1_artifacts\image_000035_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

![Image](output_part1_artifacts\image_000036_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

Figure 7-4. Switching Frequency Dithering

![Image](output_part1_artifacts\image_000037_8cd4eb92f28ff9e3c4cc2f496817e40a60d8327d7d419319c01739bf5ba24424.png)

## 7.3.10 Inductor Current Limit

The TPS55289 implements both peak current and average inductor current limit.  The  average  current  mode control loop uses the current sense information at the high-side MOSFET of the boost leg to clamp the maximum average inductor current to 8 A (typical).

Besides the average current limit, a peak current limit protection is implemented during transient to protect the device against overcurrent conditions beyond the capability of the device.

## 7.3.11 Internal Charge Path

Each  of  the  two  high-side  MOSFET  drivers  is  biased  from  its  floating  bootstrap  capacitor,  which  is  normally re-charged  by  VCC  through  internal  bootstrap  diodes  when  the  low-side  MOSFET  is  turned  on.  When  the TPS55289 operates exclusively in the buck or boost regions, one of the high-side MOSFETs is constantly on. An internal charge path, from VOUT and BOOT2 to BOOT1 or from VIN and BOOT1 to BOOT2, charges the bootstrap capacitor to V CC  so that the high-side MOSFET remains on.

## 7.3.12 Output Voltage Setting

There are two ways to set the output voltage: changing the feedback ratio and changing the reference voltage. The TPS55289 has a 11-bit DAC to program the reference voltage from 45 mV to 1.2 V. The TPS55289 can also select an internal feedback resistor divider or an external resistor divider by setting the FB bit in register 04h. When the FB bit is set to 0, the output voltage feedback ratio is set in internal register 04h. When the FB bit is set to 1, the output voltage feedback ratio is set by an external resistor divider.

When using internal  output  voltage  feedback  settings,  use  Equation  8  to  calculate  the  output  voltage.  There are  four  feedback  ratios  programmable  by  writing  the  INTFB[1:0]  bits  of  register  04h.  With  this  function,  the TPS55289 can limit the maximum output voltage to different values. In addition, the minimum step of the output voltage change is also programmed to 10 mV, 7.5 mV, 5 mV, and 2.5 mV, accordingly.

When using  an  external  output  voltage  feedback  resistor  divider  as  shown  in  Figure  7-5,  use  Equation  5  to calculate the output voltage with the reference voltage at the FB/INT pin.

<!-- formula-not-decoded -->

![Image](output_part1_artifacts\image_000038_4361a09dfee51443e226fae8c4c2ddee42548470667b045bb18cc73bc2840869.png)

Figure 7-5. Output Voltage Setting by External Resistor Divider

![Image](output_part1_artifacts\image_000039_2f091428524137bced431fffb0957454e0e5fb8c63013917bbf48b62282febf2.png)

TI  recommends  using  100  kΩ  for  the  up  resistor,  R FB\_UP.  The  reference  voltage,  V REF,  at  the  FB/INT  pin  is programmable from 45 mV to 1.2 V by writing 11-bit data into registers 00h and 01h.

## 7.3.13 Output Current Monitoring and Cable Voltage Droop Compensation

The TPS55289 outputs a voltage at the CDC pin proportional to the sensed voltage across an output current sensing resistor between the ISP pin and the ISN pin. Equation 6 shows the exact voltage at the CDC pin related to the sensed output current.

<!-- formula-not-decoded -->

To compensate the voltage droop across a cable from the output of the USB port to its powered device, the TPS55289 can lift its output voltage in proportion to the load current. There are two methods in the TPS55289 to implement the compensation: by setting internal register 05h or by placing a resistor between the CDC pin and AGND pin.

When using internal output voltage feedback, use the internal compensation setting. When using an external resistor divider at the FB/INT pin to set the output voltage, use the external compensation setting by placing a resistor at the CDC pin.

By  default,  the  internal  cable  voltage  droop  compensation  function  is  enabled  with  0  V  added  to  the  output voltage. Write the value into the bit CDC [2:0] in register 05h to get the desired voltage compensation.

When using external output voltage feedback, external compensation is better than the internal register for its high  accuracy.  The  output  voltage  rises  in  proportion  to  the  current  sourcing  from  the  CDC  pin  through  the resistor at the CDC pin. Use 100-kΩ resistance for the up resistor of the feedback resistor divider. Equation 7 shows the output voltage rise related to the sensed output current, the resistance at the CDC pin, and the up resistor of the output voltage feedback resistor divider.

<!-- formula-not-decoded -->

## where

- RFB\_UP is the up resistor of the resistor divider between the output and the FB/INT pin.
- RCDC is the resistor at the CDC pin.

Figure 7-6 shows the output voltage rise versus the sensed output current and the resistor at the CDC pin when RFB\_UP is 100 kΩ.

![Image](output_part1_artifacts\image_000040_684bd80a73d1a8be8487d9df68bb9acdf5ce43539f094f29cf23dea110a0705b.png)

![Image](output_part1_artifacts\image_000041_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

[www.ti.com](https://www.ti.com/)

Figure 7-6. Output Voltage Rise Versus Output Current

![Image](output_part1_artifacts\image_000042_340f6fc2272d3210c24094a44352c108d276ba84939b4176cc227c30da66db5e.png)

## 7.3.14 Output Current Limit

The  output  current  limit  is  programmable  from  0  A  to  6.35  A  by  placing  a  10-mΩ  current  sensing  resistor between the ISP pin and the ISN pin. Smaller resistance results in a higher current limit and larger resistance results in a lower current limit. An internal register sets the current sense voltage across the ISP pin and the ISN pin. The programmable voltage step between the ISP pin and the ISN pin is 0.5 mV.

Connecting the ISP and the ISN pin together to the  VOUT pin disables  the  output  current  limit  because  the sensed voltage is always 0. The output current limit can also be disabled by resetting the Current\_Limit\_EN bit in the Current\_Limit register to 0.

## 7.3.15 Overvoltage Protection

The TPS55289 has output overvoltage protection. When the output voltage at the VOUT pin is detected above 23.5  V  (typical),  the  TPS55289  turns  off  two  high-side  FETs  and  turns  on  two  low-side  FETs  until  its  output voltage drops the hysteresis value lower than the output overvoltage protection threshold. This function prevents overvoltage on the output and secures the circuits connected to the output from excessive overvoltage.

## 7.3.16 Output Short Circuit Protection

In  addition  to  the  average  inductor  current  limit,  the  TPS55289  implements  output  short-circuit  protection  by entering  hiccup  mode.  To  enable  hiccup  mode,  the  HICCUP  bit  in  register  06h  must  be  set.  After  a  3.6-ms soft-start-up  time,  the  TPS55289  monitors  the  average  inductor  current  and  output  voltage.  Whenever  the output short circuit happens, causing the average inductor current to reach the set limit and the output voltage is below 0.8 V, the TPS55289 shuts down the switching for 76 ms (typical) and then repeats the soft start for 3.6 ms. The hiccup mode helps reduce the total power dissipation on the TPS55289 in output short-circuit or overcurrent condition.

## 7.3.17 Thermal Shutdown

The TPS55289 is protected by a thermal shutdown circuit that shuts down the device when the internal junction temperature  exceeds  175°C  (typical).  The  internal  soft-start  circuit  is  reset  but  all  internal  registers  values remain unchanged when thermal shutdown is triggered. The converter automatically restarts when the junction temperature drops below the thermal shutdown hysteresis of 20°C below the thermal shutdown threshold.

## 7.4 Device Functional Modes

In  light  load  condition,  the  TPS55289  can  work  in  PFM  or  forced  PWM  mode  to  meet  different  application requirements. PFM mode decreases switching frequency to reduce the switching loss, thus it gets high efficiency at light load condition. FPWM mode keeps the switching frequency unchanged to avoid undesired low switching frequency, but the efficiency becomes lower than that of PFM mode.

By default, the TPS55289 works in PFM mode. To set the device in forced PWM mode, write the FPWM bit in the MODE register to 1.

## 7.4.1 PWM Mode

In FPWM mode, the TPS55289 keeps the switching frequency unchanged in light load condition. When the load current decreases, the output of the internal error amplifier decreases as well to reduce the average inductor current down to deliver less power from input to output. When the output current further reduces, the current through the inductor decreases to zero during the switch-off time. The high-side N-MOSFET is not turned off even if the current through the MOSFET is zero. Thus, the inductor current changes its direction after it runs to zero. The power flow is from the output side to input side. The efficiency is low in light load condition. However, with  the  fixed  switching  frequency,  there  is  no  audible  noise  or  other  problems  that  can  be  caused  by  low switching frequency in light load condition.

## 7.4.2 Power Save Mode

The TPS55289 improves the efficiency at light load condition with PFM mode. By enabling the PFM function in the internal register, the TPS55289 can work in PFM mode at light load condition. When the TPS55289 operates at  light  load  condition,  the  output  of  the  internal  error  amplifier  decreases  to  make  the  inductor  peak  current down to deliver less power to the load. When the output current further reduces, the current through the inductor will  decrease to zero during the switch-off time. When the TPS55289 works in buck mode, once the inductor current  becomes  zero,  the  low-side  switch  of  the  buck  side  is  turned  off  to  prevent  the  reverse  current  from output to ground. When the TPS55289 works in boost mode, once the inductor current becomes zero, the high side-switch of the boost side is turned off to prevent the reverse current from output to input. The TPS55289 resumes switching until the output voltage drops, so PFM mode reduces switching cycles and eliminates the power loss by the reverse inductor current to get high efficiency in light load condition.

## 7.5 Programming

The TPS55289 uses I 2 C interface for flexible converter parameter programming. I 2 C is a bi-directional 2-wire serial  interface.  Only  two  bus  lines  are  required:  a  serial  data  line  (SDA)  and  a  serial  clock  line  (SCL).  I 2 C devices can be considered as controllers or targets when performing data transfers. A controller is the device that initiates a data transfer on the bus and generates the clock signals to permit that transfer. At that time, any device addressed is considered a target.

The TPS55289 operates as a target device with address 74h and 75h set by the MODE pin. Receiving control inputs from the controller device, like a microcontroller or a digital signal processor, reads and writes the internal registers 00h through 07h. The I 2 C interface of the TPS55289 supports both standard mode (up to 100 kbit/s) and fast mode plus (up to 1000 kbit/s). Both SDA and SCL must be connected to the positive supply voltage through current sources or pullup resistors. When the bus is free, both lines are in high voltage.

## 7.5.1 Data Validity

The data on the SDA line must be stable during the high level period of the clock. The high level or low level state of the data line can only change when the clock signal on the SCL line is low level. One clock pulse is generated for each data bit transferred.

Figure 7-7. I 2 C Data Validity

![Image](output_part1_artifacts\image_000043_4332ada88a7824e91d6f7f45b5f51600d29ebbdf7031bf935fb9fa8edba7b348.png)

![Image](output_part1_artifacts\image_000044_d8fd26cf46a963637eb4377ab3e0070c61c3b0b9150c5784cd39392c9daf8471.png)

![Image](output_part1_artifacts\image_000045_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 7.5.2 START and STOP Conditions

All transactions begin with a START (S) and can be terminated by a STOP (P). A high level to low level transition on the SDA line while SCL is at high level defines a START condition. A low level to high level transition on the SDA line when the SCL is at high level defines a STOP condition.

START  and  STOP  conditions  are  always  generated  by  the  controller.  The  bus  is  considered  busy  after  the START condition, and free after the STOP condition.

Figure 7-8. I 2 C START and STOP Conditions

![Image](output_part1_artifacts\image_000046_e1460ceb33d1d0e197be2db762a70905f0a0493f503abe01a2f5af330a77efd7.png)

## 7.5.3 Byte Format

Every  byte  on  the  SDA  line  must  be  eight  bits  long.  The  number  of  bytes  to  be  transmitted  per  transfer  is unrestricted. Each byte has to be followed by an acknowledge bit. Data is transferred with the most significant bit (MSB) first. If a target cannot receive or transmit another complete byte of data until it has performed some other function, it can hold the clock line SCL low to force the controller into a wait state (clock stretching). Data transfer then continues when the target is ready for another byte of data and release the clock line SCL.

Figure 7-9. Byte Format

![Image](output_part1_artifacts\image_000047_e086c3cd9a8c23be3f7fee13cc2fdff59cf5c6a3bc9df3befd193d7b206dbc77.png)

## 7.5.4 Acknowledge (ACK) and Not Acknowledge (NACK)

The acknowledge takes place after every byte. The acknowledge bit allows the receiver to signal the transmitter that  the  byte  was  successfully  received  and  another  byte  can  be  sent.  All  clock  pulses,  including  the acknowledge ninth clock pulse, are generated by the controller.

The transmitter releases the SDA line during the acknowledge clock pulse so the receiver can pull the SDA line to low level and it remains stable low level during the high level period of this clock pulse.

The Not Acknowledge signal is when SDA remains high level during the ninth clock pulse. The controller can then generate either a STOP to abort the transfer or a repeated START to start a new transfer.

## 7.5.5 Target Address and Data Direction Bit

After the START, a target address is sent. This address is seven bits long followed by the eighth bit as a data direction bit (bit R/W). A zero indicates a transmission (WRITE) and a one indicates a request for data (READ).

![Image](output_part1_artifacts\image_000048_3af320d5b29cb2c0219ab4d9edb18fb2fa8d372e491e6a99002d8f837c8cea8f.png)

![Image](output_part1_artifacts\image_000049_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

Figure 7-10. Target Address and Data Direction

![Image](output_part1_artifacts\image_000050_c9a0916c5b353fbb9b675e833fa8c5082bd947439b1a6bf18d622e61e799dd34.png)

## 7.5.6 Single Read and Write

Figure 7-12. Single-Byte Read

![Image](output_part1_artifacts\image_000051_e4c3673d3ccc4a27f46da0d310088d57f0c05d42f08f4b71cfeab3a6e1948524.png)

If the register address is not defined, the TPS55289 sends back NACK and goes back to the idle state.

## 7.5.7 Multiread and Multiwrite

The TPS55289 supports multiread and multiwrite.

Figure 7-13. Multibyte Write

![Image](output_part1_artifacts\image_000052_9f3a96787ef79ee1b003a64a9db73eaae5aca99c461caca8ac7777b21609ee78.png)

![Image](output_part1_artifacts\image_000053_13e688530a30bcdced0a800501fb9067847d9be4fe61b4347c8e82efbc135372.png)

Figure 7-14. Multibyte Read

![Image](output_part1_artifacts\image_000054_958721ad0ff24eee27cfbf8c2afd04a20c547b9512fc0d3ce372d85e23da6ab5.png)

## 7.6 Register Maps

Table 7-3 lists the memory-mapped registers for the device registers. All register offset addresses not listed in Table 7-3 should be considered as reserved locations, and the register contents should not be modified.

Table 7-3. Device Registers

| Address   | Acronym    | Register Name         | Section   |
|-----------|------------|-----------------------|-----------|
| 0h, 1h    | REF        | Reference Voltage     | Go        |
| 2h        | IOUT_LIMIT | Current Limit Setting | Go        |
| 3h        | VOUT_SR    | Slew Rate             | Go        |
| 4h        | VOUT_FS    | Feedback Selection    | Go        |
| 5h        | CDC        | Cable Compensation    | Go        |
| 6h        | MODE       | Mode Control          | Go        |
| 7h        | STATUS     | Operating Status      | Go        |

## 7.6.1 REF Register (Address = 0h, 1h)

REF is shown in Figure 7-15 and Figure 7-16 and described in Table 7-4.

## Return to Summary Table.

REF sets the internal reference voltage of the TPS55289. The 01h register is the high byte and the 00h register is  the  low  byte.  One  LSB  of  register  00h  stands  for  0.5645  mV  of  the  internal  reference  voltage.  When  the register  value  is  00000000 00000000b, the reference voltage is 45 mV. When the register value is 00000111 10000000b, the reference voltage is 1.129 V. The output voltage of the TPS55289 also depends on the output feedback ratio, which is either set by INTFB bit in register 04h or set by an external resistor divider. The default REF = 282 mV.

When using internal output voltage feedback, the output voltage VOUT is calculated by Equation 8.

The REF register can be configured by an I 2 C controller before setting the OE bit in register 06h. For 5-V output voltage,  set  the  REF  register  value  to  00000001  10100100b.  To  set  the  internal  reference  voltage,  write  the register 00h first, then write the register 01h.

<!-- formula-not-decoded -->

## Figure 7-15. REF\_LSB

| 7    | 6    | 5    | 4    | 3    | 2    | 1    | 0    |
|------|------|------|------|------|------|------|------|
| VREF | VREF | VREF | VREF | VREF | VREF | VREF | VREF |

## Figure 7-16. REF\_MSB

| 15   | 14   | 13       | 12   | 11   | 10   | 9    | 8   |
|------|------|----------|------|------|------|------|-----|
|      |      | Reserved |      |      |      | VREF |     |

Table 7-4. REF Register Field Descriptions

| Bit   | Field    | Type   | Reset         | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
|-------|----------|--------|---------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 15-11 | Reserved | R/W    | 000000b       | Reserved                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| 10-0  | VREF     | R/W    | 001 10100100b | Sets the internal reference voltage 000 00000000b = 45-mV reference voltage 000 00000001b = 45.5645-mV reference voltage 000 00000010b = 46.129-mV reference voltage ...... = ...... 001 10100100b = 282-mV reference voltage ...... = ...... 011 00110100b = 508-mV reference voltage ...... = ...... 101 10001100b = 846-mV reference voltage ...... = ...... 111 10000000b = 1129-mV reference voltage ...... = ...... 111 11111110b = 1200-mV reference voltage |

![Image](output_part1_artifacts\image_000055_5076bd63da5574cbe1643cdf53cac2b97d40ee06d3006ff5596f423a6628f9c8.png)

![Image](output_part1_artifacts\image_000056_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 7.6.2 IOUT\_LIMIT Register (Address = 2h) [reset = 11100100h]

IOUT\_LIMIT is shown in Figure 7-17 and described in Table 7-5.

Return to Summary Table.

IOUT\_LIMIT sets the current limit target voltage between the ISP pin and the ISN pin. The default value in the current limit register is 11100100b standing for 50 mV. One LSB stands for 0.5 mV. The bit7 enables the current limit or disables the current limit.

## Figure 7-17. IOUT\_LIMIT Register

| 7                | 6                     | 5                     | 4                     | 3                     | 2                     | 1                     | 0                     |
|------------------|-----------------------|-----------------------|-----------------------|-----------------------|-----------------------|-----------------------|-----------------------|
| Current_Limit_EN | Current_Limit_Setting | Current_Limit_Setting | Current_Limit_Setting | Current_Limit_Setting | Current_Limit_Setting | Current_Limit_Setting | Current_Limit_Setting |
| R/W-1b           | R/W-1100100b          | R/W-1100100b          | R/W-1100100b          | R/W-1100100b          | R/W-1100100b          | R/W-1100100b          | R/W-1100100b          |

## Table 7-5. IOUT\_LIMIT Register Field Descriptions

| Bit   | Field                 | Type   | Reset    | Description                                                                                                                                                                                                                                                                                                                                                            |
|-------|-----------------------|--------|----------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7     | Current_Limit_EN      | R/W    | 1b       | Enable or disable current limit. 0b = Current limit disabled 1b = Current limit enabled (Default)                                                                                                                                                                                                                                                                      |
| 6-0   | Current_Limit_Setting | R/W    | 1100100b | Sets the current limit target voltage between the ISP pin and the ISN pin 0000000b = V ISP -V ISN = 0 (mV) 0000001b = V ISP -V ISN = 0.5 (mV) 0000010b = V ISP -V ISN = 1 (mV) 0000011b = V ISP -V ISN = 1.5 (mV) 0000100b = V ISP -V ISN = 2.0 (mV) ...... = ...... 1100100b = V ISP -V ISN = 50.0 (mV) (Default) ...... = ...... 1111111b = V ISP -V ISN = 63.5 (mV) |

## 7.6.3 VOUT\_SR Register (Address = 3h) [reset = 00000001h]

VOUT\_SR is shown in Figure 7-18 and described in Table 7-6.

Return to Summary Table.

Register 03h sets the slew rate of the output voltage change and the response delay time after the output current exceeds the setting output current limit.

The OCP\_DELAY [1:0] bits set the response time of the TPS55289 when the output overcurrent limit is hit. This allows the TPS55289 to output high current in a relative short duration time. The default setting is 128 µs so that the TPS55289 immediately limits the output current.

The SR [1:0] bits set 1.25 mV/μs, 2.5 mV/μs, 5 mV/μs, and 10 mV/μs slew rate for output voltage change.

## Figure 7-18. VOUT\_SR Register

| 7        | 5         | 3         | 2        | 1       |
|----------|-----------|-----------|----------|---------|
| RESERVED | OCP_DELAY | OCP_DELAY | RESERVED | SR      |
| R/W-0b   | R/W-00b   | R/W-00b   | R/W-00b  | R/W-01b |

## Table 7-6. VOUT\_SR Register Field Descriptions

| Bit   | Field     | Type   | Reset   | Description                                                                                                                                                                                                      |
|-------|-----------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-6   | RESERVED  | R/W    | 00b     | Reserved                                                                                                                                                                                                         |
| 5-4   | OCP_DELAY | R/W    | 00b     | Sets the response time of the device when the output overcurrent limit is reached 00b = 128 µs (Default) 01b = Delay 1.024 × 3 ms 10b = Delay 1.024 × 6 ms 11b = Delay 1.024 × 12 ms                             |
| 3-2   | RESERVED  | R/W    | 00b     | Reserved                                                                                                                                                                                                         |
| 1-0   | SR        | R/W    | 01b     | Sets slew rate for output voltage change 00b = 1.25-mV/µs output change slew rate 01b = 2.5-mV/µs output change slew rate (Default) 10b = 5-mV/µs output change slew rate 11b = 10-mV/µs output change slew rate |

![Image](output_part1_artifacts\image_000057_2c3de8d38d7127fc9191723744751ba19a2abda9341507814fc2a5026cf43b9b.png)

![Image](output_part1_artifacts\image_000058_bd2491b88b99b5dcb830a1b1acd2fc504fa535f4b616e1c77734cbb2e40d31e0.png)

## 7.6.4 VOUT\_FS Register (Address = 4h) [reset = 00000011h]

VOUT\_FS is shown in Figure 7-19 and described in Table 7-7.

Return to Summary Table.

Register 04h sets the selection for the output feedback voltage, either by an internal resistor divider or external resistor divider, and sets the internal feedback ratio when using internal feedback resistor divider.

## Figure 7-19. VOUT\_FS Register

| 7      | 6          | 5          | 4          | 3          | 2          | 1       | 0       |
|--------|------------|------------|------------|------------|------------|---------|---------|
| FB     | RESERVED   | RESERVED   | RESERVED   | RESERVED   | RESERVED   | INTFB   | INTFB   |
| R/W-0b | R/W-00000b | R/W-00000b | R/W-00000b | R/W-00000b | R/W-00000b | R/W-11b | R/W-11b |

## Table 7-7. VOUT\_FS Register Field Descriptions

| Bit   | Field    | Type   | Reset   | Description                                                                                                                                                                                                                                                                                          |
|-------|----------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7     | FB       | R/W    | 0b      | Output feedback voltage 0b = Use internal output voltage feedback. The FB/INT pin is the indicator for output short circuit protection, overcurrent status, and overvoltage status (Default). 1b = Use external output voltage feedback. The FB/INT pin is the feedback input of the output voltage. |
| 6-2   | RESERVED | R      | 00000b  | Reserved                                                                                                                                                                                                                                                                                             |
| 1-0   | INTFB    | R/W    | 11b     | Internal feedback ratio 00b = Set internal feedback ratio to 0.2256 01b = Set internal feedback ratio to 0.1128 10b = Set internal feedback ratio to 0.0752 11b = Set internal feedback ratio to 0.0564 (Default)                                                                                    |

## Table 7-8. Output Voltage vs Internal Reference

|   INTFB1 |   INTFB0 | REF=0000h   | REF=001Ah   | REF=0050h   | REF=00F0h   | REF=0780h   | Output Voltage Step   |
|----------|----------|-------------|-------------|-------------|-------------|-------------|-----------------------|
|        0 |        0 |             |             |             | 0.8 V       | 5 V         | 2.5 mV                |
|        0 |        1 |             |             | 0.8 V       |             | 10 V        | 5 mV                  |
|        1 |        0 |             | 0.8 V       |             |             | 15 V        | 7.5 mV                |
|        1 |        1 | 0.8 V       |             |             |             | 20 V        | 10 mV                 |

## 7.6.5 CDC Register (Address = 5h) [reset = 11100000h]

CDC is shown in Figure 7-20 and described in Table 7-9.

Return to Summary Table.

Register  05h  sets  masks  for  SC  bit,  OCP  bit,  and  OVP  bit  in  register  07h.  In  addition,  register  05h  sets  the voltage rise added to the setting output voltage with respect to the sensed differential voltage between the ISP pin and the ISN pin.

## Figure 7-20. CDC Register

| 7       | 6        | 5        | 4        | 3          | 2 1      |
|---------|----------|----------|----------|------------|----------|
| SC_MASK | OCP_MASK | OVP_MASK | RESERVED | CDC_OPTION | CDC      |
| R/W-1b  | R/W-1b   | R/W-1b   | R/W-0b   | R/W-0b     | R/W-000b |

## Table 7-9. CDC Register Field Descriptions

| Bit   | Field      | Type   | Reset   | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
|-------|------------|--------|---------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7     | SC_MASK    | R/W    | 1b      | Short circuit mask 0b = Disabled SC indication 1b = Enable SC indication (Default)                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| 6     | OCP_MASK   | R/W    | 1b      | Over current mask 0b = Disabled OCP indication 1b = Enable OCP indication (Default)                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| 5     | OVP_MASK   | R/W    | 1b      | Over voltage mask 0b = Disabled OVP indication 1b = Enable OVP indication (Default)                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| 4     | RESERVED   | R/W    | 0b      | Reserved                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| 3     | CDC_OPTION | R/W    | 0b      | Select the cable voltage droop compensation approach. 0b = Internal CDC compensation by the register 05H (Default) 1b = External CDC compensation by a resistor at the CDC pin                                                                                                                                                                                                                                                                                                                                                                                |
| 2-0   | CDC        | R/W    | 000b    | Compensation for voltage droop over the cable 000b = 0-V output voltage rise with 50 mV at V ISP - V ISN (Default) 001b = 0.1-V output voltage rise with 50 mV at V ISP - V ISN 010b = 0.2-V output voltage rise with 50 mV at V ISP - V ISN 011b = 0.3-V output voltage rise with 50 mV at V ISP - V ISN 100b = 0.4-V output voltage rise with 50 mV at V ISP - V ISN 101b = 0.5-V output voltage rise with 50 mV at V ISP - V ISN 110b = 0.6-V output voltage rise with 50 mV at V ISP - V ISN 111b = 0.7-V output voltage rise with 50 mV at V ISP - V ISN |

![Image](output_part1_artifacts\image_000059_2c3de8d38d7127fc9191723744751ba19a2abda9341507814fc2a5026cf43b9b.png)