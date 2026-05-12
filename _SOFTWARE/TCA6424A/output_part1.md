## 1 Features

- Operating power-supply voltage range of 1.65 V to 5.5 V
- Allows bidirectional voltage-level translation and GPIO expansion between:
- -1.8-V SCL/SDA and

1.8-V, 2.5-V, 3.3-V, or 5-V P Port

- -2.5-V SCL/SDA and 1.8-V, 2.5-V, 3.3-V, or 5-V P Port
- -3.3-V SCL/SDA and 1.8-V, 2.5-V, 3.3-V, or 5-V P Port
- -5-V SCL/SDA and 1.8-V, 2.5-V, 3.3-V, or 5-V P Port
- I 2 C to Parallel port expander
- Low standby current consumption of 1 μA
- Schmitt-Trigger action allows slow input transition and better switching noise immunity at the SCL and SDA inputs
- -Vhys = 0.18 V Typ at 1.8 V
- -Vhys = 0.25 V Typ at 2.5 V
- -Vhys = 0.33 V Typ at 3.3 V
- -Vhys = 0.5 V Typ at 5 V
- 5-V Tolerant I/O ports
- Active-low reset input ( RESET)
- Open-drain active-low interrupt output ( INT)
- 400-kHz Fast I 2 C Bus
- Input/output configuration register
- Polarity inversion register
- Internal power-on reset
- Power up with all channels configured as inputs
- No glitch on power up
- Noise filter on SCL/SDA inputs
- Latched outputs with high-current drive maximum capability for directly driving LEDs
- Latch-up performance exceeds 100 mA per JESD 78, class II
- ESD protection exceeds JESD 22
- -2000-V Human-body model (A114-A)
- -200-V Machine model (A115-A)
- -1000-V Charged-device model (C101)

![Image](output_part1_artifacts\image_000000_6302137ebd088186bebc8d9515fd560267b3cf3090d58b584f0077b90a000b37.png)

![Image](output_part1_artifacts\image_000001_5060f44b853965d0436ef421b41df4e647575e99f155946528e9c75335c635c8.png)

![Image](output_part1_artifacts\image_000002_6d0da1f1b517d86c8097f4f2f8eb9e83725447d5dd669f75e67e49cb079bbcfb.png)

[TCA6424A](https://www.ti.com/product/TCA6424A)

SCPS193D - JULY 2010 - REVISED JANUARY 2023

## TCA6424A Low-Voltage 24-Bit I 2 C and SMBus I/O Expander With Interrupt Output, Reset, and Configuration Registers

## 2 Description

This 24-bit I/O expander for the two-line bidirectional bus  (I 2 C)  is  designed  to  provide  general-purpose remote I/O expansion for most microcontroller families via the I 2 C interface [serial clock (SCL) and serial data (SDA)].

The major benefit of this device is its wide V CC  range. It can operate from 1.65 V to 5.5 V on the P-port side and on the SDA/SCL side. This allows the TCA6424A to interface with next-generation microprocessors and  microcontrollers  on  the  SDA/SCL  side,  where supply  levels  are  dropping  down  to  conserve  power. In contrast to the dropping power supplies of microprocessors and microcontrollers,  some  PCB components,  such  as  LEDs,  remain  at  a  5-V  power supply.

## Package Information

| DEVICE NAME   | PACKAGE (1)   | BODY SIZE         |
|---------------|---------------|-------------------|
| TCA6424A      | UQFN (32)     | 5.00 mm × 5.00 mm |

- (1) For all available packages, see the orderable addendum at the end of the datasheet.

![Image](output_part1_artifacts\image_000003_4cfa911f7575fde0eca3dab7397aef6485b6b55c1f28747d84f7daa83957cdd8.png)

If used, the exposed center pad must be connected as a secondary ground or left electrically open.

RGJ Package (Bottom View)

![Image](output_part1_artifacts\image_000004_8109a3cb8507c24f81188ec0d91f2722b67e87269ab915f9700654767a95f1ca.png)

![Image](output_part1_artifacts\image_000005_fc4241a5feaf55607c5b02a4bfb7eeed7e9a1f6726dd7c2100b21266522330ba.png)

![Image](output_part1_artifacts\image_000006_4b465568d5f80caca39eb067af3b084a22a011b50e1a9f490ad7e5df9fd46673.png)

![Image](output_part1_artifacts\image_000007_0d3a71d8563c30bad7b0928926c569ef707775bce319b8a773748fe7deb6708b.png)

## Table of Contents

| 1 Features ............................................................................1                                                                  |                                                                                                                                                           | 8 Detailed Description ......................................................17                                                                           |                    |
|-----------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------|
| 2 Description .......................................................................1                                                                    |                                                                                                                                                           | 8.1 Overview...................................................................17                                                                         |                    |
| 3 Revision History ..............................................................                                                                         | 2                                                                                                                                                         | 8.2 Functional Block Diagram.........................................17                                                                                   |                    |
| 4 Description (continued) ..................................................                                                                              | 3                                                                                                                                                         | 8.3 Feature Description...................................................18                                                                              |                    |
| 5 Pin Configuration and Functions ...................................4                                                                                    |                                                                                                                                                           | 8.4 Device Functional Modes..........................................20                                                                                   |                    |
| 6 Specifications                                                                                                                                          | ..................................................................6                                                                                       | 8.5 Programming............................................................                                                                               | 20                 |
| 6.1 Absolute Maximum Ratings (1) ....................................6                                                                                    |                                                                                                                                                           | 8.6 Register Maps...........................................................23                                                                            |                    |
| 6.2 ESD Ratings...............................................................                                                                            | 6                                                                                                                                                         | 9 Application and Implementation ..................................27                                                                                     |                    |
| 6.3 Recommended Operating Conditions.........................6                                                                                            |                                                                                                                                                           | 9.1 Typical Application....................................................27                                                                             |                    |
| 6.4 Thermal Information....................................................7                                                                              |                                                                                                                                                           | 9.2 Power Supply Recommendation...............................28                                                                                          |                    |
| 6.5 Electrical Characteristics.............................................7                                                                              |                                                                                                                                                           | 10 Device and Documentation Support ..........................31                                                                                          |                    |
| 6.6 I 2 C Interface Timing Requirements.............................8                                                                                     |                                                                                                                                                           | 10.1 Receiving Notification of Documentation Updates..31                                                                                                  |                    |
| 6.7 Reset Timing Requirements........................................9                                                                                    |                                                                                                                                                           | 10.2 Support Resources.................................................31                                                                                 |                    |
| 6.8 Switching Characteristics............................................9                                                                                |                                                                                                                                                           | 10.3 Trademarks.............................................................31                                                                            |                    |
| 6.9 Typical Characteristics..............................................10                                                                               |                                                                                                                                                           | 10.4 Electrostatic Discharge Caution..............................31                                                                                      |                    |
| 7 Parameter Measurement Information ..........................13                                                                                          |                                                                                                                                                           | 10.5 Glossary..................................................................31                                                                         |                    |
| 3 Revision History                                                                                                                                        | 3 Revision History                                                                                                                                        | 3 Revision History                                                                                                                                        | 3 Revision History |
| Changes from Revision C (April 2014) to Revision D (January 2023)                                                                                         | Changes from Revision C (April 2014) to Revision D (January 2023)                                                                                         | Changes from Revision C (April 2014) to Revision D (January 2023)                                                                                         | Page               |
| • Deleted all references to the RSM package ......................................................................................................        | • Deleted all references to the RSM package ......................................................................................................        | • Deleted all references to the RSM package ......................................................................................................        | 1                  |
| • Changed all instances of legacy terminology to controller and target where I 2 C is mentioned..........................1                                | • Changed all instances of legacy terminology to controller and target where I 2 C is mentioned..........................1                                | • Changed all instances of legacy terminology to controller and target where I 2 C is mentioned..........................1                                |                    |
| • Deleted Package thermal impedance from the Absolute Maximum Ratings table.............................................6                                 | • Deleted Package thermal impedance from the Absolute Maximum Ratings table.............................................6                                 | • Deleted Package thermal impedance from the Absolute Maximum Ratings table.............................................6                                 |                    |
| • Added Storage temperature range to the Absolute Maximum Ratings table......................................................6                            | • Added Storage temperature range to the Absolute Maximum Ratings table......................................................6                            | • Added Storage temperature range to the Absolute Maximum Ratings table......................................................6                            |                    |
| Changed Handling Ratings To: ESD Ratings ....................................................................................................             | Changed Handling Ratings To: ESD Ratings ....................................................................................................             | Changed Handling Ratings To: ESD Ratings ....................................................................................................             | 6                  |
| • • Added the Thermal Informatio n table................................................................................................................. | • • Added the Thermal Informatio n table................................................................................................................. | • • Added the Thermal Informatio n table................................................................................................................. | 7                  |
| • Added the Application and Implementation NOTE...........................................................................................                | • Added the Application and Implementation NOTE...........................................................................................                | • Added the Application and Implementation NOTE...........................................................................................                | 27                 |
| • Added the Detailed Design Procedure section.................................................................................................27          | • Added the Detailed Design Procedure section.................................................................................................27          | • Added the Detailed Design Procedure section.................................................................................................27          |                    |
| • Added paragraph: "Ramping up the device V CCP " to Power Supply Recommendations ................................                                        | • Added paragraph: "Ramping up the device V CCP " to Power Supply Recommendations ................................                                        | • Added paragraph: "Ramping up the device V CCP " to Power Supply Recommendations ................................                                        | 28                 |
| Changes from Revision B (September 2010) to Revision C (April 2014)                                                                                       | Changes from Revision B (September 2010) to Revision C (April 2014)                                                                                       | Changes from Revision B (September 2010) to Revision C (April 2014)                                                                                       | Page               |
| • Removed hard coded ordering information table. ..............................................................................................1          | • Removed hard coded ordering information table. ..............................................................................................1          | • Removed hard coded ordering information table. ..............................................................................................1          |                    |
| • Updated document formatting. .......................................................................................................................... | • Updated document formatting. .......................................................................................................................... | • Updated document formatting. .......................................................................................................................... | 1                  |
| Changes from Revision A (August 2010) to Revision B (September 2010)                                                                                      | Changes from Revision A (August 2010) to Revision B (September 2010)                                                                                      | Changes from Revision A (August 2010) to Revision B (September 2010)                                                                                      | Page               |
| • Revised document to updated document status from preview to production data.............................................                                | • Revised document to updated document status from preview to production data.............................................                                | • Revised document to updated document status from preview to production data.............................................                                | 1                  |
| Changes from Revision * (July 2010) to Revision A (August 2010)                                                                                           | Changes from Revision * (July 2010) to Revision A (August 2010)                                                                                           | Changes from Revision * (July 2010) to Revision A (August 2010)                                                                                           | Page               |
| • Changed Recommended Supply Sequencing and Rates Table.......................................................................28                          | • Changed Recommended Supply Sequencing and Rates Table.......................................................................28                          | • Changed Recommended Supply Sequencing and Rates Table.......................................................................28                          |                    |

![Image](output_part1_artifacts\image_000008_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

![Image](output_part1_artifacts\image_000009_53a9a1b091f5515cf505f42c2bad81189ceb7c780621d19c94eb739c76566cca.png)

[www.ti.com](https://www.ti.com/)

## 4 Description (continued)

The bidirectional voltage level translation in the TCA6424A is provided through VCCI. VCCI should be connected to  the  VCC of the external SCL/SDA lines. This indicates the VCC level of the I 2 C bus to the TCA6424A. The voltage level on the P-port of the TCA6424A is determined by the VCCP.

The  TCA6424A  consists  of  three  8-bit  Configuration  (input  or  output  selection),  Input,  Output,  and  Polarity Inversion (active high) registers. At power on, the I/Os are configured as inputs. However, the system controller can enable the I/Os as either inputs or outputs by writing to the I/O configuration bits. The data for each input or output is kept in the corresponding input or output register. The polarity of the Input Port register can be inverted with the Polarity Inversion register. All registers can be read by the system controller.

The system controller can reset the TCA6424A in the event of a timeout or other improper operation by asserting a  low  in  the  RESET  input.  The  power-on  reset  puts  the  registers  in  their  default  state  and  initializes  the  I 2 C/ SMBus state machine. The RESET pin causes the same reset/initialization to occur without depowering the part.

The TCA6424A open-drain interrupt ( INT) output is activated when any input state differs from its corresponding Input Port register state and is used to indicate to the system controller that an input state has changed.

INT can be connected to the interrupt input of a microcontroller. By sending an interrupt signal on this line, the remote I/O can inform the microcontroller if there is incoming data on its ports without having to communicate via the I 2 C bus. Thus, the TCA6424A can remain a simple target device.

The  device  P-port  outputs  have  high-current  sink  capabilities  for  directly  driving  LEDs  while  consuming  low device current.

One hardware pin (ADDR) can be used to program and vary the fixed I 2 C address and allow up to two devices to share the same I 2 C bus or SMBus.

## 5 Pin Configuration and Functions

![Image](output_part1_artifacts\image_000010_b022863477444e2bcc277788494f98bc2def0210fbaf0fbe686c167209412218.png)

If used, the exposed center pad must be connected as a secondary ground or left electrically open.

## Figure 5-1. RGJ Package (Bottom View)

## Table 5-1. Pin Functions

| PIN     | PIN   | DESCRIPTION                                                                                          |
|---------|-------|------------------------------------------------------------------------------------------------------|
| PIN NO. | NAME  | DESCRIPTION                                                                                          |
| 1       | P00   | P-port input/output (push-pull design structure). At power on, P00 is configured as an input.        |
| 2       | P01   | P-port input/output (push-pull design structure). At power on, P01 is configured as an input.        |
| 3       | P02   | P-port input/output (push-pull design structure). At power on, P02 is configured as an input.        |
| 4       | P03   | P-port input/output (push-pull design structure). At power on, P03 is configured as an input.        |
| 5       | P04   | P-port input/output (push-pull design structure). At power on, P04 is configured as an input.        |
| 6       | P05   | P-port input/output (push-pull design structure). At power on, P05 is configured as an input.        |
| 7       | P06   | P-port input/output (push-pull design structure). At power on, P06 is configured as an input.        |
| 8       | P07   | P-port input/output (push-pull design structure). At power on, P07 is configured as an input.        |
| 9       | P10   | P-port input/output (push-pull design structure). At power on, P10 is configured as an input.        |
| 10      | P11   | P-port input/output (push-pull design structure). At power on, P11 is configured as an input.        |
| 11      | P12   | P-port input/output (push-pull design structure). At power on, P12 is configured as an input.        |
| 12      | P13   | P-port input/output (push-pull design structure). At power on, P13 is configured as an input.        |
| 13      | P14   | P-port input/output (push-pull design structure). At power on, P14 is configured as an input.        |
| 14      | P15   | P-port input/output (push-pull design structure). At power on, P15 is configured as an input.        |
| 15      | P16   | P-port input/output (push-pull design structure). At power on, P16 is configured as an input.        |
| 16      | P17   | P-port input/output (push-pull design structure). At power on, P17 is configured as an input.        |
| 17      | P20   | P-port input/output (push-pull design structure). At power on, P20 is configured as an input.        |
| 18      | P21   | P-port input/output (push-pull design structure). At power on, P21 is configured as an input.        |
| 19      | P22   | P-port input/output (push-pull design structure). At power on, P22 is configured as an input.        |
| 20      | P23   | P-port input/output (push-pull design structure). At power on, P23 is configured as an input.        |
| 21      | P24   | P-port input/output (push-pull design structure). At power on, P24 is configured as an input.        |
| 22      | P25   | P-port input/output (push-pull design structure). At power on, P25 is configured as an input.        |
| 23      | P26   | P-port input/output (push-pull design structure). At power on, P26 is configured as an input.        |
| 24      | P27   | P-port input/output (push-pull design structure). At power on, P27 is configured as an input.        |
| 25      | GND   | Ground                                                                                               |
| 26      | ADDR  | Address input. Connect directly to V CCP or ground.                                                  |
| 27      | V CCP | Supply voltage of TCA6424A for P port                                                                |
| 28      | RESET | Active-low reset input. Connect to V CCI through a pullup resistor, if no active connection is used. |
| 29      | SCL   | Serial clock bus. Connect to V CCI through a pullup resistor.                                        |
| 30      | SDA   | Serial data bus. Connect to V CCI through a pullup resistor.                                         |

![Image](output_part1_artifacts\image_000011_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

![Image](output_part1_artifacts\image_000012_e621311c01e9e12d685cdf97caf32d359f82d770d35fa9417af29cb75af1cb60.png)

## Table 5-1. Pin Functions (continued)

| PIN     | PIN   | DESCRIPTION                                                                                                                     |
|---------|-------|---------------------------------------------------------------------------------------------------------------------------------|
| PIN NO. | NAME  |                                                                                                                                 |
| 31      | V CCI | Supply voltage of I 2 C bus. Connect directly to the V CC of the external I 2 C controller. Provides voltage-level translation. |
| 32      | INT   | Interrupt output. Connect to V CCI through a pullup resistor.                                                                   |

## 6 Specifications

## 6.1 Absolute Maximum Ratings (1)

over operating free-air temperature range (unless otherwise noted)

|       |                                  |                                  |                                  |   MIN | MAX   | UNIT   |
|-------|----------------------------------|----------------------------------|----------------------------------|-------|-------|--------|
| V CCI | Supply voltage range             | Supply voltage range             | Supply voltage range             |  -0.5 | 6.5   | V      |
| V CCP | Supply voltage range             | Supply voltage range             | Supply voltage range             |  -0.5 | 6.5   | V      |
| V I   | Input voltage range (2)          | Input voltage range (2)          | Input voltage range (2)          |  -0.5 | 6.5   | V      |
| V O   | Output voltage range (2)         | Output voltage range (2)         | Output voltage range (2)         |  -0.5 | 6.5   | V      |
| I IK  | Input clamp current              | ADDR, RESET, SCL                 | V I < 0                          |       | ±20   | mA     |
| I OK  | Output clamp current             | INT                              | V O < 0                          |       | ±20   | mA     |
| I IOK | Input/output clamp current       | P port                           | V O < 0 or V O > V CCP           |       | ±20   | mA     |
| I IOK | Input/output clamp current       | SDA                              | V O < 0 or V O > V CCI           |       | ±20   | mA     |
| I OL  | Continuous output low current    | P port                           | V O = 0 to V CCP                 |       | 25    | mA     |
| I OL  | Continuous output low current    | SDA, INT                         | V O = 0 to V CCI                 |       | 15    | mA     |
| I OH  | Continuous output high current   | P port                           | V O = 0 to V CCP                 |       | 25    | mA     |
| I CC  | Continuous current through GND   | Continuous current through GND   | Continuous current through GND   |       | 200   | mA     |
| I CC  | Continuous current through V CCP | Continuous current through V CCP | Continuous current through V CCP |       | 160   | mA     |
| I CC  | Continuous current through V CCI | Continuous current through V CCI | Continuous current through V CCI |       | 10    | mA     |
| T stg | Storage temperature range        | Storage temperature range        | Storage temperature range        |   -65 | 150   | °C     |

## 6.2 ESD Ratings

|         |                         |                                                                                |   MIN |   MAX | UNIT   |
|---------|-------------------------|--------------------------------------------------------------------------------|-------|-------|--------|
| V (ESD) | Electrostatic discharge | Human body model (HBM), per ANSI/ESDA/JEDEC JS-001, all pins (1)               |     0 |     2 | kV     |
| V (ESD) |                         | Charged device model (CDM), per JEDEC specification JESD22- C101, all pins (2) |     0 |    01 | kV     |

## 6.3 Recommended Operating Conditions

|       |                                |                                | MIN         | MAX         | UNIT   |
|-------|--------------------------------|--------------------------------|-------------|-------------|--------|
| V CCI | Supply voltage                 | Supply voltage                 | 1.65        | 5.5         | V      |
| V CCP | Supply voltage                 | Supply voltage                 | 1.65        | 5.5         | V      |
| V IH  | High-level input voltage       | SCL, SDA                       | 0.7 × V CCI | VCCI        | V      |
| V IH  |                                | RESET                          | 0.7 × V CCI | 5.5         | V      |
| V IH  |                                | ADDR, P27-P00                  | 0.7 × V CCP | 5.5         | V      |
| V IL  | Low-level input voltage        | SCL, SDA, RESET                | -0.5        | 0.3 × V CCI | V      |
| V IL  |                                | ADDR, P27-P00                  | -0.5        | 0.3 × V CCP | V      |
| I OH  | High-level output current      | P27-P00                        |             | 10          | mA     |
| I OL  | Low-level output current       | P27-P00                        |             | 25          | mA     |
| T A   | Operating free-air temperature | Operating free-air temperature | -40         | 85          | °C     |

![Image](output_part1_artifacts\image_000013_3b8c1dab5d2a4c1db405cf0b813bbd6902f3f9eed97b8b13ee5347ff8246437a.png)

![Image](output_part1_artifacts\image_000014_c68b513ba19c9e357aae2223b5bd6fe04157828d7b65abc76e00d983f207d477.png)

## 6.4 Thermal Information

| THERMAL METRIC (1)   |                                              |   TCA6424A RGJ (UQFN) 32 PINS | UNIT   |
|----------------------|----------------------------------------------|-------------------------------|--------|
| R θJA                | Junction-to-ambient thermal resistance       |                          44.9 | °C/W   |
| R θJC(top)           | Junction-to-case (top) thermal resistance    |                          14.3 | °C/W   |
| R θJB                | Junction-to-board thermal resistance         |                          17.7 | °C/W   |
| ψ JT                 | Junction-to-top characterization parameter   |                           0.3 | °C/W   |
| ψ JB                 | Junction-to-board characterization parameter |                          17.7 | °C/W   |
| R θJC(bottom)        | Junction-to-case (bottom) thermal resistance |                           9.1 | °C/W   |

## 6.5 Electrical Characteristics

over recommended operating free-air temperature range, VCCI = 1.65 V to 5.5 V (unless otherwise noted)

| PARAMETER   | PARAMETER                 | TEST CONDITIONS             | V CCP           |   MIN |   TYP (1) | MAX   | UNIT   |
|-------------|---------------------------|-----------------------------|-----------------|-------|-----------|-------|--------|
| V IK        | Input diode clamp voltage | I I = -18 mA                | 1.65 V to 5.5 V |  -1.2 |           |       | V      |
| V POR       | Power-on reset voltage    | V I = V CCP or GND, I O = 0 | 1.65 V to 5.5 V |       |         1 | 1.4   | V      |
| V OH        |                           | I OH = -8 mA                | 1.65 V          |   1.2 |           |       |        |
| V OH        |                           | I OH = -8 mA                | 2.3 V           |   1.8 |           |       |        |
| V OH        |                           | I OH = -8 mA                | 3 V             |   2.6 |           |       |        |
| V OH        | P-port high-level output  | I OH = -8 mA                | 4.5 V           |   4.1 |           |       | V      |
| V OH        | voltage                   | I = -10 mA                  | 1.65 V          |     1 |           |       |        |
| V OH        |                           | I = -10 mA                  | 2.3 V           |   1.7 |           |       |        |
| V OH        |                           | OH                          | 3 V             |   2.5 |           |       |        |
| V OH        |                           | I = -10 mA                  | 4.5 V           |   4.0 |           |       |        |
| V OL        |                           |                             | 1.65 V          |       |           | 0.45  |        |
| V OL        |                           | I = 8mA                     | 2.3 V           |       |           | 0.25  |        |
| V OL        |                           | OL                          | 3 V             |       |           | 0.25  |        |
| V OL        | P-port low-level output   |                             | 4.5 V           |       |           | 0.23  |        |
| V OL        | voltage                   |                             | 1.65 V          |       |           | 0.6   | V      |
| V OL        |                           | I = 10 mA                   | 2.3 V           |       |           | 0.3   |        |
| V OL        |                           | OL                          | 3 V             |       |           | 0.25  |        |
|             |                           |                             | 4.5 V           |       |           | 0.24  |        |
| I           | SDA                       | V OL = 0.4 V                | 1.65 V to 5.5 V |     3 |           |       | mA     |
| OL          | INT                       | V OL = 0.4 V                | 1.65 V to 5.5 V |     3 |        15 |       |        |
| I           | SCL, SDA, RESET           | V I = V CCI or GND          | 1.65 V to 5.5 V |       |           | ±0.1  | μA     |
| I           | ADDR                      | V I = V CCP or GND          | 1.65 V to 5.5 V |       |           | ±0.1  | μA     |
| I IH        | P port                    | V I = V CCP                 | 1.65 V to 5.5 V |       |           | 1     | μA     |
| I IL        | P port                    | V I = GND                   | 1.65 V to 5.5 V |       |           | 1     | μA     |

## 6.5 Electrical Characteristics (continued)

over recommended operating free-air temperature range, VCCI = 1.65 V to 5.5 V (unless otherwise noted)

| PARAMETER             | PARAMETER                          | PARAMETER                     | TEST CONDITIONS                                                                                             | V CCP           | MIN   |   TYP (1) |   MAX | UNIT   |
|-----------------------|------------------------------------|-------------------------------|-------------------------------------------------------------------------------------------------------------|-----------------|-------|-----------|-------|--------|
| I CC (I CCP + I CCI ) | Operating mode                     | SDA, P port, ADDR, RESET      | V I on SDA and RESET= V CCI or GND, V I on P port and ADDR = V CCP , I O = 0, I/O = inputs, f SCL = 400 kHz | 1.65 V to 5.5 V |       |         8 |    30 | μA     |
| I CC (I CCP + I CCI ) | Operating mode                     | SDA, P port, ADDR, RESET      | V I on SDA and RESET= V CCI or GND, V I on P port and ADDR = V CCP , I O = 0, I/O = inputs, f SCL = 100 kHz | 1.65 V to 5.5 V |       |       1.7 |    10 | μA     |
| I CC (I CCP + I CCI ) | Standby mode                       | SCL, SDA, P port, ADDR, RESET | V I on SCL, SDA and RESET = V CCI or GND, V I on P port and ADDR = V CCP , I O = 0, I/O = inputs, f SCL = 0 | 1.65 V to 5.5 V |       |       0.1 |     3 | μA     |
| ΔI CCI                | Additional current in Standby mode | SCL,SDA RESET                 | One input at V CCI - 0.6 V, Other inputs at V CCI or GND                                                    | 1.65 V to 5.5 V |       |           |    25 | μA     |
| ΔI CCP                | Additional current in Standby mode | P port, ADDR,                 | One input at V CCP - 0.6 V, Other inputs at V CCP or GND                                                    | 1.65 V to 5.5 V |       |           |    60 | μA     |
| C I                   | SCL                                |                               | V I = V CCI or GND                                                                                          | 1.65 V to 5.5 V |       |         6 |     7 | pF     |
| C io                  | SDA                                |                               | V IO = V CCI or GND                                                                                         | 1.65 V to 5.5 V |       |         7 |     8 | pF     |
| C io                  | P port                             |                               | V IO = V CCP or GND                                                                                         | 1.65 V to 5.5 V |       |       7.5 |   8.5 | pF     |

## 6.6 I 2 C Interface Timing Requirements

over recommended operating free-air temperature range (unless otherwise noted) (see Figure 7-1)

|            |                                                                            | STANDARD MODE I 2 C BUS   | STANDARD MODE I 2 C BUS   | FAST MODE I 2 C BUS   | FAST MODE I 2 C BUS   | UNIT   |
|------------|----------------------------------------------------------------------------|---------------------------|---------------------------|-----------------------|-----------------------|--------|
|            |                                                                            | MIN                       | MAX                       | MIN                   | MAX                   | UNIT   |
| f scl      | I 2 C clock frequency                                                      | 0                         | 100                       | 0                     | 400                   | kHz    |
| t sch      | I 2 C clock high time                                                      | 4                         |                           | 0.6                   |                       | μs     |
| t scl      | I 2 C clock low time                                                       | 4.7                       |                           | 1.3                   |                       | μs     |
| t sp       | I 2 C spike time                                                           | 0                         | 50                        | 0                     | 50                    | ns     |
| t sds      | I 2 C serial data setup time                                               | 250                       |                           | 100                   |                       | ns     |
| t sdh      | I 2 C serial data hold time                                                | 0                         |                           | 0                     |                       | ns     |
| t icr      | I 2 C input rise time                                                      |                           | 1000                      | 20 + 0.1C b (1)       | 300                   | ns     |
| t icf      | I 2 C input fall time                                                      |                           | 300                       | 20 + 0.1C b (1)       | 300                   | ns     |
| t ocf      | I 2 C output fall time; 10 pF to 400 pF bus                                |                           | 300                       | 20 + 0.1C b (1)       | 300                   | μs     |
| t buf      | I 2 C bus free time between Stop and Start                                 | 4.7                       |                           | 1.3                   |                       | μs     |
| t sts      | I 2 C Start or repeater Start condition setup time                         | 4.7                       |                           | 0.6                   |                       | μs     |
| t sth      | I 2 C Start or repeater Start condition hold time                          | 4                         |                           | 0.6                   |                       | μs     |
| t sps      | I 2 C Stop condition setup time                                            | 4                         |                           | 0.6                   |                       | μs     |
| t vd(data) | Valid data time; SCL low to SDA output valid                               |                           | 1                         |                       | 1                     | μs     |
| t vd(ack)  | Valid data time of ACK condition; ACK signal from SCL low to SDA (out) low |                           | 1                         |                       | 1                     | μs     |

![Image](output_part1_artifacts\image_000015_45b5422b067b6161eae3ec86fb0e8ec847665948c2088a594733c941d30222b0.png)

![Image](output_part1_artifacts\image_000016_e621311c01e9e12d685cdf97caf32d359f82d770d35fa9417af29cb75af1cb60.png)

## 6.7 Reset Timing Requirements

over recommended operating free-air temperature range (unless otherwise noted) (see Figure 7-4)

|         |                      | STANDARD MODE I 2 C BUS   | STANDARD MODE I 2 C BUS   | FAST MODE I 2 C BUS   | FAST MODE I 2 C BUS   | UNIT   |
|---------|----------------------|---------------------------|---------------------------|-----------------------|-----------------------|--------|
|         |                      | MIN                       | MAX                       | MIN                   | MAX                   |        |
| t W     | Reset pulse duration | 4                         |                           | 4                     |                       | ns     |
| t REC   | Reset recovery time  | 0                         |                           | 0                     |                       | ns     |
| t RESET | Time to reset (1)    | 600                       |                           | 600                   |                       | ns     |

## 6.8 Switching Characteristics

over recommended operating free-air temperature range, CL ≤ 100 pF (unless otherwise noted) (see Figure 7-1)

| PARAMETER   | PARAMETER                  | FROM   | TO      | STANDARD MODE I 2 C BUS   | STANDARD MODE I 2 C BUS   | FAST MODE I 2 C BUS   | FAST MODE I 2 C BUS   | UNIT   |
|-------------|----------------------------|--------|---------|---------------------------|---------------------------|-----------------------|-----------------------|--------|
|             |                            |        |         | MIN                       | MAX                       | MIN                   | MAX                   |        |
| t IV        | Interrupt valid time       | P port | INT     |                           | 4                         |                       | 4                     | μs     |
| t IR        | Interrupt reset delay time | SCL    | INT     |                           | 4                         |                       | 4                     | μs     |
| t PV        | Output data valid          | SCL    | P27-P00 |                           | 400                       |                       | 400                   | ns     |
| t PS        | Input data setup time      | P port | SCL     | 0                         |                           | 0                     |                       | ns     |
| t PH        | Input data hold time       | P port | SCL     | 300                       |                           | 300                   |                       | ns     |

![Image](output_part1_artifacts\image_000017_0d3a71d8563c30bad7b0928926c569ef707775bce319b8a773748fe7deb6708b.png)

## 6.9 Typical Characteristics

TA = 25°C (unless otherwise noted)

![Image](output_part1_artifacts\image_000018_934841d09eace8422952e15408f3ab1f597a1e316b3720ec218826ef58906550.png)

![Image](output_part1_artifacts\image_000019_e8a928f3e69d29dd95faf683c6e0d29655d5e4208fdea9f325d1601567e0f460.png)

![Image](output_part1_artifacts\image_000020_e621311c01e9e12d685cdf97caf32d359f82d770d35fa9417af29cb75af1cb60.png)

## 6.9 Typical Characteristics (continued)

TA = 25°C (unless otherwise noted)

![Image](output_part1_artifacts\image_000021_0df069fd9f01bc26f8a17608f61d0c04acc11bacb73140e13718265ae8d9ccec.png)

![Image](output_part1_artifacts\image_000022_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000023_0d3a71d8563c30bad7b0928926c569ef707775bce319b8a773748fe7deb6708b.png)

## 6.9 Typical Characteristics (continued)

TA = 25°C (unless otherwise noted)

![Image](output_part1_artifacts\image_000024_892c91f2e2da5b9b3fe3de7d67f7ff9faadd27d61c5a23399445d5fe07420aa2.png)

![Image](output_part1_artifacts\image_000025_45b5422b067b6161eae3ec86fb0e8ec847665948c2088a594733c941d30222b0.png)

![Image](output_part1_artifacts\image_000026_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

## 7 Parameter Measurement Information

![Image](output_part1_artifacts\image_000027_9f613676ba4948e1a597f04317845602f1ea26683dd530d7f57d03bd87a631b3.png)

SDA LOAD/.notdefCONFIGURATION

![Image](output_part1_artifacts\image_000028_b712488dbd47df24a9a76a428935bb83d2116d73fe76e62695c4f54cf609f222.png)

VOLTAGE/.notdefWAVEFORMS

|   BYTE | DESCRIPTION                                   |
|--------|-----------------------------------------------|
|      1 | I C/.notdefaddress 2                          |
|      2 | Input/.notdefregister/.notdefport/.notdefdata |

- A. CL includes probe and jig capacitance. tocf is measured with CL of 10 pF or 400 pF.
- B. All inputs are supplied by generators having the following characteristics: PRR ≤ 10 MHz, Z O  = 50 Ω, t r /t f ≤ 30 ns.
- C. All parameters and waveforms are not applicable to all devices.

Figure 7-1. I 2 C Interface Load Circuit and Voltage Waveforms

![Image](output_part1_artifacts\image_000029_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000030_74434699e7c917c01fccaf59c9afb3d1183ec03f6f5a7999b770f7eb5c2eb6a9.png)

[TCA6424A](https://www.ti.com/product/TCA6424A)

![Image](output_part1_artifacts\image_000031_45b5422b067b6161eae3ec86fb0e8ec847665948c2088a594733c941d30222b0.png)

![Image](output_part1_artifacts\image_000032_29db4512dd9456b2d15bb17b1264aee86d1cc5f60663b5d3e152232cf63f3eed.png)

- A. CL includes probe and jig capacitance.
- B. All inputs are supplied by generators having the following characteristics: PRR ≤ 10 MHz, Z O  = 50 Ω, t r /t f ≤ 30 ns.
- C. All parameters and waveforms are not applicable to all devices.

Figure 7-2. Interrupt Load Circuit and Voltage Waveforms

![Image](output_part1_artifacts\image_000033_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

![Image](output_part1_artifacts\image_000034_bc4986b02a9da4e0b373c140f3b87f2e492be52e32591c15de30b5a7bb4a22c0.png)

P PORT LOAD CONFIGURATION

![Image](output_part1_artifacts\image_000035_9e5b42ddc320035fe3449f8763db52d28a65c9308c0bb45e59d05a07b4aa233c.png)

WRITE MODE (R/ W = 0)

![Image](output_part1_artifacts\image_000036_d0401820dd8a75d32976bb0286ce1c7902172208069a4afa5e0c6f52ac6d419e.png)

READ MODE (R/

- A. CL includes probe and jig capacitance.
- B. tpv is measured from 0.7 × VCC on SCL to 50% I/O (Pn) output.
- C. All inputs are supplied by generators having the following characteristics: PRR ≤ 10 MHz, Z O  = 50 Ω, t r /t f ≤ 30 ns.
- D. The outputs are measured one at a time, with one transition per measurement.
- E. All parameters and waveforms are not applicable to all devices.

Figure 7-3. P-Port Load Circuit and Timing Waveforms

W = 1)

![Image](output_part1_artifacts\image_000037_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000038_74434699e7c917c01fccaf59c9afb3d1183ec03f6f5a7999b770f7eb5c2eb6a9.png)

![Image](output_part1_artifacts\image_000039_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

![Image](output_part1_artifacts\image_000040_21e3c1f03ed7e820ccd0892d2f7bab69341a5d3f3f5726b4d84b4d16ae5c0ce8.png)

![Image](output_part1_artifacts\image_000041_770d7d054d2f09ab4310b8620919ff609122d1d4f21bf0ccc5355651ea6e0ea1.png)

- A. CL includes probe and jig capacitance.
- B. All inputs are supplied by generators having the following characteristics: PRR ≤ 10 MHz, Z O  = 50 Ω, t r /t f ≤ 30 ns.
- C. The outputs are measured one at a time, with one transition per measurement.
- D. I/Os are configured as inputs.
- E. All parameters and waveforms are not applicable to all devices.

Figure 7-4. Reset Load Circuits and Voltage Waveforms

![Image](output_part1_artifacts\image_000042_53a9a1b091f5515cf505f42c2bad81189ceb7c780621d19c94eb739c76566cca.png)

## 8 Detailed Description

## 8.1 Overview

## 8.1.1 Voltage Translation

Table 8-1 shows how to set up VCC levels for the necessary voltage translation between the I 2 C bus and the TCA6424A.

Table 8-1. Voltage Translation

|   V CCI (SDA AND SCL OF I 2 C CONTROLLER) (V) |   V CCP (P PORT) (V) |
|-----------------------------------------------|----------------------|
|                                           1.8 |                  1.8 |
|                                           1.8 |                  2.5 |
|                                           1.8 |                  3.3 |
|                                           1.8 |                    5 |
|                                           2.5 |                  1.8 |
|                                           2.5 |                  2.5 |
|                                           2.5 |                  3.3 |
|                                           2.5 |                    5 |
|                                           3.3 |                  1.8 |
|                                           3.3 |                  2.5 |
|                                           3.3 |                  3.3 |
|                                           3.3 |                    5 |
|                                             5 |                  1.8 |
|                                             5 |                  2.5 |
|                                             5 |                  3.3 |
|                                             5 |                    5 |

## 8.2 Functional Block Diagram

![Image](output_part1_artifacts\image_000043_f4b04b08092ed169bafba1d44eb449800435f77ba00b99bc0982ebebd708f7af.png)

- A. All I/Os are set to inputs at reset.

![Image](output_part1_artifacts\image_000044_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000045_5286d665f44103310840c7585f249eeed34770d073935add89cc9fd5a3dbb931.png)

- B. Pin numbers shown are for the RGJ package.

## 8.3 Feature Description

## 8.3.1 I/O Port

When an I/O is configured as an input, FETs Q1 and Q2 are off, which creates a high-impedance input. The input voltage may be raised above VCC to a maximum of 5.5 V.

If  the  I/O  is  configured as an output, Q1 or Q2 is enabled, depending on the state of the output port register. In this case, there are low-impedance paths between the I/O pin and either VCC or GND. The external voltage applied to this I/O pin should not exceed the recommended levels for proper operation.

## 8.3.2 I 2 C Interface

The  bidirectional  I 2 C  bus  consists  of  the  serial  clock  (SCL)  and  serial  data  (SDA)  lines.  Both  lines  must  be connected to a positive supply through a pullup resistor when connected to the output stages of a device. Data transfer may be initiated only when the bus is not busy.

I 2 C communication with this device is initiated by a controller sending a Start condition, a high-to-low transition on  the  SDA  input/output,  while  the  SCL  input  is  high  (see  Figure  8-3).  After  the  Start  condition,  the  device address byte is sent, most significant bit (MSB) first, including the data direction bit (R/ W).

After  receiving  the  valid  address  byte,  this  device  responds  with  an  acknowledge  (ACK),  a  low  on  the  SDA input/output during the high of the ACK-related clock pulse. The address (ADDR) input of the target device must not be changed between the Start and the Stop conditions.

On the I 2 C bus, only one data bit is transferred during each clock pulse. The data on the SDA line must remain stable during the high pulse of the clock period, as changes in the data line at this time are interpreted as control commands (Start or Stop) (see Figure 8-4).

A Stop condition, a low-to-high transition on the SDA input/output while the SCL input is high, is sent by the controller (see Figure 8-3).

Figure 8-1. Positive Logic

![Image](output_part1_artifacts\image_000046_a1260272cd01f2c669307101691a0c2bd51afaf8224c5b4419a020aea25293e1.png)

- A. On power up or reset, all registers return to default values.

Figure 8-2. Simplified Schematic of P00 to P27

![Image](output_part1_artifacts\image_000047_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

![Image](output_part1_artifacts\image_000048_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

Any number of data bytes can be transferred from the transmitter to receiver between the Start and the Stop conditions. Each byte of eight bits is followed by one ACK bit. The transmitter must release the SDA line before the receiver can send an ACK bit. The device that acknowledges must pull down the SDA line during the ACK clock pulse, so that the SDA line is stable low during the high pulse of the ACK-related clock period (see Figure 8-5). When a target receiver is addressed, it must generate an ACK after each byte is received. Similarly, the controller must generate an ACK after each byte that it receives from the target transmitter. Setup and hold times must be met to ensure proper operation.

A controller receiver signals an end of data to the target transmitter by not generating an acknowledge (NACK) after the last byte has been clocked out of the target. This is done by the controller receiver by holding the SDA line  high.  In  this  event,  the  transmitter  must  release  the  data  line  to  enable  the  controller  to  generate  a  Stop condition.

![Image](output_part1_artifacts\image_000049_d2fa1650098786ed3581c74b5fdfa3958d35e5fe7e431d25952b7e4669cf0b31.png)

Figure 8-3. Definition of Start and Stop Conditions

Figure 8-4. Bit Transfer

![Image](output_part1_artifacts\image_000050_0fa4edc53fa37d0c7a6cd38d9b54f79d2f77c27a08fcf6a1ed786ba2cbac5f1a.png)

Figure 8-5. Acknowledgment on the I 2 C Bus

![Image](output_part1_artifacts\image_000051_196ce80c09f83d5f98c007302b1c59a0a9013dcd80d07a51fda5afd377eeffe4.png)

Table 8-2. Interface Definition

|                      | BIT     | BIT   | BIT   | BIT   | BIT   | BIT   | BIT   | BIT     |
|----------------------|---------|-------|-------|-------|-------|-------|-------|---------|
| BYTE                 | 7 (MSB) | 6     | 5     | 4     | 3     | 2     | 1     | 0 (LSB) |
| I 2 C target address | L       | H     | L     | L     | L     | H     | ADDR  | R/W     |

![Image](output_part1_artifacts\image_000052_0d3a71d8563c30bad7b0928926c569ef707775bce319b8a773748fe7deb6708b.png)

## Table 8-2. Interface Definition (continued)

| I/O data bus   | P07   | P06   | P05   | P04   | P03   | P02   | P01   | P00   |
|----------------|-------|-------|-------|-------|-------|-------|-------|-------|
| I/O data bus   | P17   | P16   | P15   | P14   | P13   | P12   | P11   | P10   |
| I/O data bus   | P27   | P26   | P25   | P24   | P23   | P22   | P21   | P20   |

## 8.4 Device Functional Modes

## 8.4.1 Device Address

The address of the TCA6424A is shown in Figure 8-6.

## Target Address

Figure 8-6. TCA6424A Address

![Image](output_part1_artifacts\image_000053_b28407e548ec52be38b5e96ec2541b58e80ac398dda04bf5653216702c28c745.png)

Table 8-3. Address Reference

| ADDR   | I 2 C BUS TARGET ADDRESS       |
|--------|--------------------------------|
| L      | 34 (decimal), 22 (hexadecimal) |
| H      | 35 (decimal), 23 (hexadecimal) |

The last bit of the target address defines the operation (read or write) to be performed. A high (1) selects a read operation, while a low (0) selects a write operation.

## 8.5 Programming

## 8.5.1 Power-On Reset

When power (from 0 V) is applied to VCCP, an internal power-on reset holds the TCA6424A in a reset condition until  VCCP has reached VPOR. At that time, the reset condition is released, and the TCA6424A registers and I 2 C/SMBus state machine initializes to their default states. After that, VCCP must be lowered to below 0.2 V and back up to the operating voltage for a power-reset cycle.

## 8.5.2 Reset Input ( RESET)

The  RESET  input  can  be  asserted  to  initialize  the  system  while  keeping  the  V CCP  at  its  operating  level.  A reset can be accomplished by holding the RESET pin low for a minimum of tW. The TCA6424A registers and I 2 C/SMBus state machine are changed to their default state once RESET is low (0). When RESET is high (1), the  I/O  levels  at  the  P  port  can  be  changed  externally  or  through  the  controller.  This  input  requires  a  pullup resistor to V CCI , if no active connection is used.

## 8.5.3 Interrupt Output ( INT)

An interrupt is generated by any rising or falling edge of the port inputs in the input mode. After time t iv , the signal INT is valid. Resetting the interrupt circuit is achieved when data on the port is changed to the original setting or when data is read from the port that generated the interrupt. Resetting occurs in the read mode at the acknowledge (ACK) or not acknowledge (NACK) bit after the rising edge of the SCL signal. Interrupts that occur during the ACK or NACK clock pulse can be lost (or be very short) due to the resetting of the interrupt during this pulse. Each change of the I/Os after resetting is detected and is transmitted as INT.

Reading from or writing to another device does not affect the interrupt circuit, and a pin configured as an output cannot cause an interrupt. Changing an I/O from an output to an input may cause a false interrupt to occur, if the state of the pin does not match the contents of the Input Port register.

The  INT  output  has  an  open-drain  structure  and  requires  pullup  resistor  to  V CCP  or  V CCI   depending  on  the application. If the INT signal is connected back to the processor that provides the SCL signal to the TCA6424A then the INT pin has to be connected to VCCI. If not, the INT pin can be connected to V CCP .

![Image](output_part1_artifacts\image_000054_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

![Image](output_part1_artifacts\image_000055_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

## 8.5.4 Bus Transactions

Data is exchanged between the controller and TCA6424A through write and read commands.

## 8.5.4.1 Writes

Data is transmitted to the TCA6424A by sending the device address and setting the least-significant bit (LSB) to a logic 0 (see Figure 8-6 for device address). The command byte is sent after the address and determines which register receives the data that follows the command byte. There is no limitation on the number of data bytes sent in one write transmission.

The twelve registers within the TCA6424A are grouped into four different sets. The four sets of registers are input  ports,  output  ports,  polarity  inversion  ports  and  configuration  ports.  After  sending  data  to  one  register, the next data byte is sent to the next register in the group of 3 registers (see Figure 8-7 and Figure 8-8). For example, if the first byte is send to Output Port 2 (register 6), the next byte is stored in Output Port 0 (register 4).

There is no limitation on the number of data bytes sent in one write transmission. In this way, each 8-bit register may be updated independently of the other registers.

Figure 8-7. Write to Output Port Register

![Image](output_part1_artifacts\image_000056_170378fc6200ac7c01d08a4fde84dc30b4a1b6e7e23ba206ea2f918118ac3a95.png)

&lt;br/&gt;

Figure 8-8. Write to Configuration or Polarity Inversion Registers

![Image](output_part1_artifacts\image_000057_2770b2ce063480a925b6630edc6a344f73d16b9712e8cb7aac29ca626466aba9.png)

## 8.5.4.2 Reads

The bus controller first must send the TCA6424A address with the LSB set to a logic 0 (see Figure 8-6 for device address). The command byte is sent after the address and determines which register is accessed.

After a restart, the device address is sent again but, this time, the LSB is set to a logic 1. Data from the register defined by the command byte then is sent by the TCA6424A (see Figure 8-9 and Figure 8-10).

After a restart, the value of the register defined by the command byte matches the register being accessed when the restart occurred. For example, if the command byte references Input Port 1 before the restart, and the restart occurs when Input Port 0 is being read, the stored command byte changes to reference Input Port 0. The original command byte is forgotten. If a subsequent restart occurs, Input Port 0 is read first. Data is clocked into the register on the rising edge of the ACK clock pulse. After the first byte is read, additional bytes may be read, but

![Image](output_part1_artifacts\image_000058_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000059_74434699e7c917c01fccaf59c9afb3d1183ec03f6f5a7999b770f7eb5c2eb6a9.png)

![Image](output_part1_artifacts\image_000060_3bc5075fc7f32c86da09b10c171ad312d7c5d8e915950b95b95182d6a53931b6.png)

the data now reflects the information in the other register in the pair. For example, if Input Port 1 is read, the next byte read is Input Port 0.

Data is clocked into the register on the rising edge of the ACK clock pulse. There is no limitation on the number of data bytes received in one read transmission, but when the final byte is received, the bus controller must not acknowledge the data.

Figure 8-9. Read From Register

![Image](output_part1_artifacts\image_000061_f18950700d9eabfd39a917053b2b283d1dec23afc25fdfb37b9010fc88b78b53.png)

&lt;br/&gt;

![Image](output_part1_artifacts\image_000062_47174e4938fd6c830c724cecb53f201e498b7f5269629ed55a3132dfe8d628e5.png)

- A. Transfer of data can be stopped at any time by a Stop condition. When this occurs, data present at the latest acknowledge phase is valid (output mode). It is assumed that the command byte previously has been set to 00 (read Input Port register).
- B. This figure eliminates the command byte transfer, a restart, and target address call between the initial target address call and actual data transfer from P port (see Figure 8-9).
- C. Auto-increment mode is enabled.

Figure 8-10. Read Input Port Register

![Image](output_part1_artifacts\image_000063_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

## 8.6 Register Maps

## 8.6.1 Control Register and Command Byte

Following the successful acknowledgment of the address byte, the bus controller sends a command byte, which is stored in the control register in the TCA6424A. Four bits of this data byte state the operation (read or write) and  the  internal  registers  (input,  output,  polarity  inversion,  or  configuration)  that  will  be  affected.  The  control register can be written or read through the I 2 C bus. The command byte is sent only during a write transmission.

The control register includes an Auto-Increment (AI) bit which is the most significant bit (bit 7) of the command byte. At power-up, the control register defaults to 00 (hex), with the AI bit set to logic 1, and the lowest 7 bits set to logic 0.

If  AI  is  1,  the  2  least  significant  bits  are  automatically  incremented  after  a  read  or  write.  This  allows  the  user to  program and/or read the 3 register banks sequentially. If more than 3 bytes of data are written when AI is 1, previous data in the selected registers will be overwritten. Reserved registers are skipped and not accessed (refer to Table 5).

If AI is 0, the 2 least significant bits are not incremented after data is read or written. During a read operation, the same register bank is read each time. During a write operation, data is written to the same register bank each time.

Reserved command codes and command byte outside the range stated in the Command Byte table must not be accessed for proper device functionality.

Figure 8-11. Control Register Bits

![Image](output_part1_artifacts\image_000064_c05939193f2e4e9311ab5563799eb00a34ffd41c6d5e20f278a90f0f2cddf1cb.png)

## Table 8-4. Command Byte

| CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | CONTROL REGISTER BITS   | COMMAND BYTE (HEX)   |                           |                 |               |
|-------------------------|-------------------------|-------------------------|-------------------------|-------------------------|-------------------------|-------------------------|-------------------------|----------------------|---------------------------|-----------------|---------------|
| AI                      | B6                      | B5                      | B4                      | B3                      | B2                      | B1                      | INCREMENT STATE B0      | COMMAND BYTE (HEX)   |                           |                 |               |
| 0                       | 0                       | 0                       | 0                       | 0                       | 0                       | 0                       | 0 Disable               | 00                   | Input Port 0              | Read byte       | xxxx xxxx (1) |
| 1                       | 0                       | 0                       | 0                       | 0                       | 0                       | 0 0                     | Enable                  | 80                   | Input Port 0              | Read byte       | xxxx xxxx (1) |
| 0                       | 0                       | 0                       | 0                       | 0                       | 0                       | 0 1                     | Disable                 | 01                   | Input Port 1              | Read byte       | xxxx xxxx (1) |
| 1                       | 0                       | 0                       | 0                       | 0                       | 0                       | 0 1                     | Enable                  | 81                   | Input Port 1              | Read byte       | xxxx xxxx (1) |
| 0                       | 0                       | 0                       | 0                       | 0                       | 0                       | 1 0                     | Disable                 | 02                   | Input Port 2              | Read byte       | xxxx xxxx (1) |
| 1                       | 0                       | 0                       | 0                       | 0                       | 0                       | 1 0                     | Enable                  | 82                   | Input Port 2              | Read byte       | xxxx xxxx (1) |
| 0                       | 0                       | 0                       | 0                       | 0                       | 0                       | 1 1                     | Disable                 | 03                   | Reserved                  | Reserved        | Reserved      |
| 1                       | 0                       | 0                       | 0                       | 0                       | 0                       | 1 1                     | Enable                  | 83                   | Reserved                  | Reserved        | Reserved      |
| 0                       | 0                       | 0                       | 0                       | 0                       | 1                       | 0 0                     | Disable                 | 04                   | Output Port 0             | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 0                       | 1                       | 0 0                     | Enable                  | 84                   | Output Port 0             | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 0                       | 1                       | 0 1                     | Disable                 | 05                   | Output Port 1             | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 0                       | 1                       | 0 1                     | Enable                  | 85                   | Output Port 1             | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 0                       | 1                       | 1 0                     | Disable                 | 06                   | Output Port 2             | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 0                       | 1                       | 1 0                     | Enable                  | 86                   | Output Port 2             | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 0                       | 1                       | 1 1                     | Disable                 | 07                   | Reserved                  | Reserved        | Reserved      |
| 1                       | 0                       | 0                       | 0                       | 0                       | 1                       | 1 1                     | Enable                  | 87                   | Reserved                  | Reserved        | Reserved      |
| 0                       | 0                       | 0                       | 0                       | 1                       | 0                       | 0 0                     | Disable                 | 08                   | Polarity Inversion Port 0 | Read/write byte | 0000 0000     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 0                       | 0 0                     | Enable                  | 88                   | Polarity Inversion Port 0 | Read/write byte | 0000 0000     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 0                       | 0 1                     | Disable                 | 09                   | Polarity Inversion Port 1 | Read/write byte | 0000 0000     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 0                       | 0 1                     | Enable                  | 89                   | Polarity Inversion Port 1 | Read/write byte | 0000 0000     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 0                       | 1 0                     | Disable                 | 0A                   | Polarity Inversion Port 2 | Read/write byte | 0000 0000     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 0                       | 1 0                     | Enable                  | 8A                   | Polarity Inversion Port 2 | Read/write byte | 0000 0000     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 0                       | 1 1                     | Disable                 | 0B                   | Reserved                  | Reserved        | Reserved      |
| 1                       | 0                       | 0                       | 0                       | 1                       | 0                       | 1 1                     | Enable                  | 8B                   | Reserved                  | Reserved        | Reserved      |
| 0                       | 0                       | 0                       | 0                       | 1                       | 1                       | 0 0                     | Disable                 | 0C                   | Configuration Port 0      | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 1                       | 0 0                     | Enable                  | 8C                   | Configuration Port 0      | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 1                       | 0 1                     | Disable                 | 0D                   | Configuration Port 1      | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 1                       | 0 1                     | Enable                  | 8D                   | Configuration Port 1      | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 1                       | 1 0                     | Disable                 | 0E                   | Configuration Port 2      | Read/write byte | 1111 1111     |
| 1                       | 0                       | 0                       | 0                       | 1                       | 1                       | 1 0                     | Enable                  | 8E                   | Configuration Port 2      | Read/write byte | 1111 1111     |
| 0                       | 0                       | 0                       | 0                       | 1                       | 1                       | 1 1                     | Disable                 | 0F                   | Reserved                  | Reserved        | Reserved      |
| 1                       | 0                       | 0                       | 0                       | 1                       | 1                       | 1 1                     | Enable                  | 8F                   | Reserved                  | Reserved        | Reserved      |

- (1) Undefined

![Image](output_part1_artifacts\image_000065_f520a8f863c9c6a3e24a52937a4d5dd2c8fceeb9d3212285d05667aa43ed230f.png)

![Image](output_part1_artifacts\image_000066_2a597eeb10cf684e742e1ebff59c8be47ff9aea3a299d7081bc62c996945ee12.png)

## 8.6.2 Register Descriptions

The Input Port registers (registers 0, 1 and 2) reflect the incoming logic levels of the pins, regardless of whether the pin is defined as an input or an output by the Configuration register. They act only on read operation. Writes to these registers have no effect. The default value (X) is determined by the externally applied logic level. Before a read operation, a write transmission is sent with the command byte to indicate to the I 2 C device that the Input Port register will be accessed next.

Table 8-5. Registers 0, 1 and 2 (Input Port Registers)

| BIT       | I-07   | I-06   | I-05   | I-04   | I-03      | I-02   | I-01   | I-00   |
|-----------|--------|--------|--------|--------|-----------|--------|--------|--------|
| DEFAULT X | X      | X      | X      | X      | X         | X      |        | X      |
| BIT I-17  | I-16   | I-15   |        | I-14   | I-13 I-12 |        | I-11   | I-10   |
| DEFAULT X | X      | X      | X      | X      | X         | X      |        | X      |
| BIT I-27  | I-26   | I-25   | I-24   | I-23   | I-22      | I-21   | I-20   |        |
| DEFAULT X | X      | X      | X      | X      | X         |        | X      | X      |

The Output Port registers (registers 4, 5 and 6) shows the outgoing logic levels of the pins defined as outputs by the Configuration register. Bit values in these registers have no effect on pins defined as inputs. In turn, reads from these registers reflect the value that is in the flip-flop controlling the output selection, NOT the actual pin value.

Table 8-6. Registers 4, 5 and 6 (Output Port Registers)

| BIT   | O-07   | O-06   | O-05   | O-04   | O-03   | O-02   | O-01   | O-00    |
|-------|--------|--------|--------|--------|--------|--------|--------|---------|
| 1     | 1      | 1      | 1      | 1      | 1      | 1      | 1      | DEFAULT |
| O-17  | O-16   | O-15   | O-14   | O-13   | O-12   | O-11   | O-10   | BIT     |
| 1     | 1      | 1      | 1      | 1      | 1      | 1      | 1      | DEFAULT |
| O-27  | O-26   | O-25   | O-24   | O-23   | O-22   | O-21   | O-20   | BIT     |
| 1     | 1      | 1      | 1      | 1      | 1      | 1      | 1      | DEFAULT |

The Polarity Inversion registers (registers 8, 9 and 10) allow polarity inversion of pins defined as inputs by the Configuration register. If a bit in these registers is set (written with 1), the corresponding port pin's polarity is inverted. If a bit in these registers is cleared (written with a 0), the corresponding port pin's original polarity is retained.

Table 8-7. Registers 8, 9 and 10 (Polarity Inversion Registers)

| BIT   | P-07   | P-06   | P-05   | P-04   | P-03   | P-02   | P-01   | P-00    |
|-------|--------|--------|--------|--------|--------|--------|--------|---------|
| 0     | 0      | 0      | 0      | 0      | 0      | 0      | 0      | DEFAULT |
| P-17  | P-16   | P-15   | P-14   | P-13   | P-12   | P-11   | P-10   | BIT     |
| 0     | 0      | 0      | 0      | 0      | 0      | 0      | 0      | DEFAULT |
| P-27  | P-26   | P-25   | P-24   | P-23   | P-22   | P-21   | P-20   | BIT     |
| 0     | 0      | 0      | 0      | 0      | 0      | 0      | 0      | DEFAULT |

The Configuration  registers  (registers  12,  13  and  14)  configure  the  direction  of  the  I/O  pins.  If  a  bit  in  these registers is set to 1, the corresponding port pin is enabled as an input with a high-impedance output driver. If a bit in these registers is cleared to 0, the corresponding port pin is enabled as an output.

Table 8-8. Registers 12, 13 and 14 (Configuration Registers)

| BIT     | C-07   | C-06   | C-05   | C-04   | C-03   | C-02   | C-01   | C-00   |
|---------|--------|--------|--------|--------|--------|--------|--------|--------|
| DEFAULT | 1      | 1      | 1      | 1      | 1      | 1      | 1      | 1      |
| BIT     | C-17   | C-16   | C-15   | C-14   | C-13   | C-12   | C-11   | C-10   |
| DEFAULT | 1      | 1      | 1      | 1      | 1      | 1      | 1      | 1      |
| BIT     | C-27   | C-26   | C-25   | C-24   | C-23   | C-22   | C-21   | C-20   |

![Image](output_part1_artifacts\image_000067_45b5422b067b6161eae3ec86fb0e8ec847665948c2088a594733c941d30222b0.png)

## Table 8-8. Registers 12, 13 and 14 (Configuration Registers) (continued)

| DEFAULT   | 1   | 1   | 1   | 1   | 1   | 1   | 1   | 1   |
|-----------|-----|-----|-----|-----|-----|-----|-----|-----|

![Image](output_part1_artifacts\image_000068_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

## 9 Application and Implementation

## Note

Information in  the  following  applications  sections  is  not  part  of  the  TI  component  specification,  and TI  does  not  warrant  its  accuracy  or  completeness.  TI's  customers  are  responsible  for  determining suitability  of  components  for  their  purposes.  Customers  should  validate  and  test  their  design implementation to confirm system functionality.

## 9.1 Typical Application

Figure 9-1 shows an application in which the TCA6424A can be used.

V CCI

![Image](output_part1_artifacts\image_000069_f5af8d47507b3825f8c828aa567fecb08c5b6fefacc7b9ac5e2ab1852ea26009.png)

- A. Device address configured as 0100000 for this example.
- B. P00 and P02-P10 are configured as inputs.
- C. P01, P11-P17, and P20-P27 are configured as outputs.
- D. Resistors are required for inputs (on P port) that may float. If a driver to an input will not let the input float, a resistor is not needed. Outputs (in the P port) do not need pullup resistors.

Figure 9-1. Typical Application

## 9.1.1 Detailed Design Procedure

## 9.1.1.1 Minimizing ICC When I/Os Control LEDs

When the I/Os are used to control LEDs, normally they are connected to VCC through a resistor as shown in Figure 9-1. The LED acts as a diode so, when the LED is off, the I/O VIN is about 1.2 V less than VCC. The ΔICC parameter in Electrical Characteristics shows how ICC increases as VIN becomes lower than VCC. Designs that must minimize current consumption, such as battery power applications, should consider maintaining the I/O pins greater than or equal to V CC  when the LED is off.

![Image](output_part1_artifacts\image_000070_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

![Image](output_part1_artifacts\image_000071_62cb271be7f7718573c53085c7e50f4471a137f69d64a98f590f8e8ac3489cc4.png)

Figure 9-2 shows a high-value resistor in parallel with the LED. Figure 9-3 shows VCC less than the LED supply voltage by at least 1.2 V. Both of these methods maintain the I/O VIN at or above VCC and prevent additional supply current consumption when the LED is off.

Figure 9-2. High-Value Resistor in Parallel With the LED

![Image](output_part1_artifacts\image_000072_5a6f516bf223880f835a7609ba78be25203c722b66dca69c462a60d8cea404bf.png)

Figure 9-3. Device Supplied by a Low Voltage

![Image](output_part1_artifacts\image_000073_4f6b99d39091a88277fbb8bb5c85235bcf77d4bc6c9bedf53146362dc7c04efa.png)

## 9.2 Power Supply Recommendation

In the event of a glitch or data corruption, TCA6424A can be reset to its default conditions by using the power-on reset feature. Power-on reset requires that the device go through a power cycle to be completely reset. This reset also happens when the device is powered on for the first time in an application.

Ramping up the device VCCP before VCCI is recommended to prevent SDA from potentially being stuck LOW.

The two types of power-on reset are shown in Figure 9-4 and Figure 9-5.

Figure 9-4. VCC is Lowered Below 0.2 V or 0 V and Then Ramped Up to VCC

![Image](output_part1_artifacts\image_000074_c586616afe751ea68a66a045e927c9da81e8b37870b889f72109fe6f45216986.png)

![Image](output_part1_artifacts\image_000075_5a4d3bb2db6c1c6d391ac6fa5a97c8fd7a6e61fbe4b687842e1543f637a08a1d.png)

![Image](output_part1_artifacts\image_000076_e93261316fa140da6ae88aadc0a45a3308db677993eccfb28f11237468b8eb86.png)

Figure 9-5. VCC is Lowered Below the POR Threshold, Then Ramped Back Up to VCC

![Image](output_part1_artifacts\image_000077_b2bc42b85a47c39f175a499d2d01ebe0ca5d07a668b9e2bd1cfa1a116031fbd1.png)

Table  9-1  specifies  the  performance  of  the  power-on  reset  feature  for  TCA6424A  for  both  types  of  power-on reset.

Table 9-1. Recommended Supply Sequencing and Rates (1)

| PARAMETER       | PARAMETER                                                                                       | PARAMETER      |   MIN |   MAX | UNIT   |
|-----------------|-------------------------------------------------------------------------------------------------|----------------|-------|-------|--------|
| t VCC_FT        | Fall rate                                                                                       | See Figure 9-4 |     1 |   100 | ms     |
| t VCC_RT        | Rise rate                                                                                       | See Figure 9-4 |  0.01 |   100 | ms     |
| t VCC_TRR_GND   | Time to re-ramp (when V CC drops to GND)                                                        | See Figure 9-4 |    40 |       | μs     |
| t VCC_TRR_POR50 | Time to re-ramp (when V CC drops to V POR_MIN - 50 mV)                                          | See Figure 9-5 |    40 |       | μs     |
| V CC_GH         | Level that V CCP can glitch down to, but not cause a functional disruption when V CCX_GW = 1 μs | See Figure 9-6 |       |   1.2 | V      |
| t VCC_GW        | Glitch width that will not cause a functional disruption when V CCX_GH = 0.5 × V CCx            | See Figure 9-6 |       |    10 | μs     |
| V PORF          | Voltage trip point of POR on falling V CC                                                       |                | 0.767 | 1.144 | V      |
| V PORR          | Voltage trip point of POR on rising V CC                                                        |                | 1.033 | 1.428 | V      |

- (1) TA = -40°C to 85°C (unless otherwise noted)

Glitches  in  the  power  supply  can  also  affect  the  power-on  reset  performance  of  this  device.  The  glitch  width (VCC\_GW) and height (VCC\_GH) are dependent on each other. The bypass capacitance, source impedance, and device impedance are factors that affect power-on reset performance. Figure 9-6 and Table 9-1 provide more information on how to measure these specifications.

Figure 9-6. Glitch Width and Glitch Height

![Image](output_part1_artifacts\image_000078_1b84ad42d6f2839784ce42702a4ca7e91ef41f6e0c426f2d46753f319d793bc5.png)

VPOR is critical to the power-on reset. V POR  is the voltage level at which the reset condition is released and all the registers and the I 2 C/SMBus state machine are initialized to the default states. The value of VPOR differs based on the VCC being lowered to or from 0. Figure 9-7 and Table 9-1 provide more details on this specification.

![Image](output_part1_artifacts\image_000079_45b5422b067b6161eae3ec86fb0e8ec847665948c2088a594733c941d30222b0.png)

Figure 9-7. VPOR

![Image](output_part1_artifacts\image_000080_b90dacdbcb17b351d793f1692d2d48554edb93dd344906e313b96069584ff07a.png)