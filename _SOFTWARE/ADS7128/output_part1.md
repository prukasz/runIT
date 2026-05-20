1

![Image](output_part1_artifacts\image_000000_5f219e3535c292d1be9824dfb6372ee06222700b02e17a63f63cfb58dbce045a.png)

![Image](output_part1_artifacts\image_000001_fc783f9e0abd0fc39fe344445f71b9e453336f04b64822e905f5a5ff5d66d9e6.png)

![Image](output_part1_artifacts\image_000002_c0dc80c105150402f382050a48744869414dacf590e5aaf66cc7e4a36f441074.png)

![Image](output_part1_artifacts\image_000003_d324cb5f58cb9150e6b4caa7566ed7b2639f1331966147309d9d93f4608b0bb6.png)

![Image](output_part1_artifacts\image_000004_43bf617aefbb794d3384a158c95d7b4052d07a6d551bdaeb457f9ca86e1c45cd.png)

![Image](output_part1_artifacts\image_000005_f7c49c409c642cd52147488fabf881cc180652f3c0768dd4974eaff5ca3fbaef.png)

![Image](output_part1_artifacts\image_000006_ef4bcd06522332650eb7e10e8ed1fc14d0f227aa03d3fe5987aebdd032191eb5.png)

ADS7128

SBAS868A -MAY 2019-REVISED MAY 2020

## ADS7128 Small, 8-Channel, 12-Bit ADC With I 2 C Interface, GPIOs, and CRC

## 1 Features

- 1 · Small package size:
- -3-mm × 3-mm WQFN
- 8 channels configurable as any combination of:
- -Up to 8 analog inputs, digital inputs, or digital outputs
- GPIOs for I/O expansion:
- -Open-drain, push-pull digital outputs
- Analog watchdog:
- -Programmable thresholds per channel
- -Event counter for transient rejection
- Wide operating ranges:
- -AVDD: 2.35 V to 5.5 V
- -DVDD: 1.65 V to 5.5 V
- --40°C to +85°C temperature range
- CRC for read/write operations:
- -CRC on data read/write
- -CRC on power-up configuration
- I 2 C interface:
- -Up to 3.4 MHz (high-speed mode)
- -8 configurable I 2 C addresses
- Programmable averaging filters
- Root-mean-square module:
- -16-bit true RMS output
- -Programmable RMS time window
- Zero-crossing-detect module:
- -ZCD output corresponding to any analog input
- -Built-in transient rejection and hysteresis
- -Digitally adjustable detection threshold

## 2 Applications

- Mobile robot CPU boards
- Refrigerators and freezers
- Digital multimeters (DMM)
- Rack servers

## 3 Description

The ADS7128 is an easy-to-use, 8-channel, multiplexed, 12-bit, successive approximation register analog-to-digital converter (SAR ADC). The eight channels can be independently configured as either analog inputs, digital inputs, or digital outputs. The device has an internal oscillator for ADC conversion processes.

The ADS7128 communicates via an I 2 C-compatible interface and operates in either autonomous or single-shot conversion mode. The ADS7128 implements analog watchdog function by eventtriggered interrupts per channel using a digital window comparator with programmable high and low thresholds, hysteresis, and an event counter. The ADS7128 has a built-in cyclic redundancy check (CRC) for data read/write operations and the powerup configuration. The ADS7128 features a root-meansquare (RMS) module that computes a 16-bit true RMS result for any analog input channel. The integrated zero-crossing-detect (ZCD) module allows for transient rejection and hysteresis near the configurable threshold crossings.

## Device Information (1)

| PART NAME   | PACKAGE   | BODY SIZE (NOM)   |
|-------------|-----------|-------------------|
| ADS7128     | WQFN (16) | 3.00 mm × 3.00 mm |

## ADS7128 Block Diagram and Applications

## Device Block Diagram

![Image](output_part1_artifacts\image_000007_dffbdca2635cf06175b65781eec79ff315b61de62e68721d59cd09fbdb65fbf5.png)

## Example System Architecture

![Image](output_part1_artifacts\image_000008_3cd88ec725f485c061617139df34b267fb96f9532ef15848d352102cf59b2f63.png)

OVP: Over voltage protection

OCP: Over current protection

·

## Table of Contents

| 1                                                                                                                                             | Features ..................................................................                                                                   | Features ..................................................................                                                                   | 1                                                                                                                                             | 8.3 Feature Description.................................................                                                                      | 14   |
|-----------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|------|
| 2                                                                                                                                             | Applications ...........................................................                                                                      | Applications ...........................................................                                                                      | 1                                                                                                                                             | 8.4 Device Functional Modes........................................                                                                           | 24   |
| 3                                                                                                                                             | Description .............................................................                                                                     | Description .............................................................                                                                     | 1                                                                                                                                             | 8.5 Programming...........................................................                                                                    | 27   |
| 4                                                                                                                                             | Revision History .....................................................                                                                        | Revision History .....................................................                                                                        | 2                                                                                                                                             | 8.6 ADS7128 Registers.................................................                                                                        | 30   |
| 5                                                                                                                                             | Device Comparison Table .....................................                                                                                 | Device Comparison Table .....................................                                                                                 | 3 9                                                                                                                                           | Application and Implementation ........................                                                                                       | 74   |
| 6                                                                                                                                             | Pin Configuration and Functions ......................... 4                                                                                   | Pin Configuration and Functions ......................... 4                                                                                   |                                                                                                                                               | 9.1 Application Information............................................                                                                       | 74   |
| 7                                                                                                                                             |                                                                                                                                               | .........................................................                                                                                     |                                                                                                                                               | 9.2 Typical Applications ................................................                                                                     | 74   |
|                                                                                                                                               | Specifications 7.1                                                                                                                            | Absolute Maximum Ratings ......................................                                                                               | 5 5 10                                                                                                                                        | Power Supply Recommendations .....................                                                                                            | 77   |
|                                                                                                                                               | 7.2                                                                                                                                           | ESD Ratings..............................................................                                                                     | 5                                                                                                                                             | 10.1 AVDD and DVDD Supply Recommendations.......                                                                                              | 77   |
|                                                                                                                                               | 7.3                                                                                                                                           | Recommended Operating Conditions.......................                                                                                       | 5 11                                                                                                                                          | Layout ...................................................................                                                                    | 78   |
|                                                                                                                                               | 7.4                                                                                                                                           | Thermal Information..................................................                                                                         | 5                                                                                                                                             | 11.1 Layout Guidelines .................................................                                                                      | 78   |
|                                                                                                                                               | 7.5                                                                                                                                           | Electrical Characteristics...........................................                                                                         | 6                                                                                                                                             | 11.2 Layout Example ....................................................                                                                      | 78   |
|                                                                                                                                               | 7.6 I                                                                                                                                         | 2 C Timing Requirements..........................................                                                                             | 7 12                                                                                                                                          | Device and Documentation Support .................                                                                                            | 79   |
|                                                                                                                                               | 7.7                                                                                                                                           | Timing Requirements................................................                                                                           | 7                                                                                                                                             | 12.1 Receiving Notification of Documentation Updates                                                                                          | 79   |
|                                                                                                                                               | 7.8                                                                                                                                           | I 2 C Switching Characteristics....................................                                                                           | 7                                                                                                                                             | 12.2 Support Resources ...............................................                                                                        | 79   |
|                                                                                                                                               | 7.9                                                                                                                                           | Switching Characteristics..........................................                                                                           | 8                                                                                                                                             | 12.3 Trademarks...........................................................                                                                    | 79   |
|                                                                                                                                               | 7.10                                                                                                                                          | Typical Characteristics............................................                                                                           | 9                                                                                                                                             | 12.4 Electrostatic Discharge Caution............................                                                                              | 79   |
| 8                                                                                                                                             | Detailed Description ............................................ 13                                                                          | Detailed Description ............................................ 13                                                                          |                                                                                                                                               | 12.5 Glossary................................................................                                                                 | 79   |
|                                                                                                                                               | 8.1                                                                                                                                           | Overview .................................................................                                                                    | 13 13                                                                                                                                         | Mechanical, Packaging, and Orderable                                                                                                          |      |
|                                                                                                                                               | 8.2                                                                                                                                           | Functional Block Diagram.......................................                                                                               | 13                                                                                                                                            | Information ...........................................................                                                                       | 79   |
| NOTE: Page numbers for previous revisions may differ from page numbers in the current version. Changes from Original (May 2019) to Revision A | NOTE: Page numbers for previous revisions may differ from page numbers in the current version. Changes from Original (May 2019) to Revision A | NOTE: Page numbers for previous revisions may differ from page numbers in the current version. Changes from Original (May 2019) to Revision A | NOTE: Page numbers for previous revisions may differ from page numbers in the current version. Changes from Original (May 2019) to Revision A | NOTE: Page numbers for previous revisions may differ from page numbers in the current version. Changes from Original (May 2019) to Revision A | Page |

![Image](output_part1_artifacts\image_000009_d84d49d3d31a7a3fd6641803ddd3654a586bda0028dd44c4bda3eac3ddabaa51.png)

![Image](output_part1_artifacts\image_000010_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 5 Device Comparison Table

| PART NUMBER   | DESCRIPTION                                          | CRC MODULE   | ZERO-CROSSING-DETECT (ZCD) MODULE   | ROOT-MEAN-SQUARE (RMS) MODULE   |
|---------------|------------------------------------------------------|--------------|-------------------------------------|---------------------------------|
| ADS7128       | 8-channel, 12-bit ADC with I 2 C interface and GPIOs | Yes          | Yes                                 | Yes                             |
| ADS7138       | 8-channel, 12-bit ADC with I 2 C interface and GPIOs | Yes          | No                                  | No                              |
| ADS7138-Q1    | 8-channel, 12-bit ADC with I 2 C interface and GPIOs | Yes          | No                                  | No                              |

## 6 Pin Configuration and Functions

![Image](output_part1_artifacts\image_000011_3178e8c0a7fbf9aceb47306c4daf0110365a2fd4a2ee3e2a5497ebfd80fc1872.png)

## Pin Functions

| PIN         | PIN   | FUNCTION (1)   | DESCRIPTION                                                                                                                              |
|-------------|-------|----------------|------------------------------------------------------------------------------------------------------------------------------------------|
| NAME        | NO.   | FUNCTION (1)   | DESCRIPTION                                                                                                                              |
| AIN0/GPIO0  | 15    | AI, DI, DO     | Channel 0; configurable as either an analog input (default) or a general-purpose input/output (GPIO)                                     |
| AIN1/GPIO1  | 16    | AI, DI, DO     | Channel 1; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN2/GPIO2  | 1     | AI, DI, DO     | Channel 2; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN3/GPIO3  | 2     | AI, DI, DO     | Channel 3; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN4/GPIO4  | 3     | AI, DI, DO     | Channel 4; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN5/GPIO5  | 4     | AI, DI, DO     | Channel 5; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN6/GPIO6  | 5     | AI, DI, DO     | Channel 6; configurable as either an analog input (default) or a GPIO                                                                    |
| AIN7/GPIO7  | 6     | AI, DI, DO     | Channel 7; configurable as either an analog input (default) or a GPIO                                                                    |
| ADDR        | 11    | AI             | Input for selecting the device I 2 C address. Connect a resistor to this pin from DECAP pin or GND to select one of the eight addresses. |
| ALERT       | 12    | Digital output | Open-drain (default) or push-pull output for the digital comparator                                                                      |
| AVDD        | 7     | Supply         | Analog supply input, also used as the reference voltage to the ADC; connect a 1-µF decoupling capacitor to GND                           |
| DECAP       | 8     | Supply         | Connect a1-µF decoupling capacitor between the DECAP and GND pins for the internal power supply                                          |
| DVDD        | 10    | Supply         | Digital I/O supply voltage; connect a 1-µF decoupling capacitor to GND                                                                   |
| GND         | 9     | Supply         | Ground for the power supply; all analog and digital signals are referred to this pin voltage                                             |
| SDA         | 14    | DI, DO         | Serial data input or output for the I 2 C interface                                                                                      |
| SCL         | 13    | DI             | Serial clock for the I 2 C interface                                                                                                     |
| Thermal pad | -     | Supply         | Exposed thermal pad; connect to GND.                                                                                                     |

![Image](output_part1_artifacts\image_000012_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000013_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 7 Specifications

## 7.1 Absolute Maximum Ratings

over operating ambient temperature range (unless otherwise noted) (1)

|                                                | MIN       | MAX        | UNIT   |
|------------------------------------------------|-----------|------------|--------|
| DVDD to GND                                    | -0.3      | 5.5        | V      |
| AVDD to GND                                    | -0.3      | 5.5        | V      |
| AINx/GPOx (2)                                  | GND - 0.3 | AVDD + 0.3 | V      |
| ADDR                                           | GND - 0.3 | 2.1        | V      |
| Digital inputs                                 | GND - 0.3 | 5.5        | V      |
| Current through any pin except supply pins (3) | -10       | 10         | mA     |
| Junction temperature, T J                      | -40       | 125        | °C     |
| Storage temperature, T stg                     | -60       | 150        | °C     |

## 7.2 ESD Ratings

|         |                         |                                                                                                                                   | VALUE      | UNIT   |
|---------|-------------------------|-----------------------------------------------------------------------------------------------------------------------------------|------------|--------|
| V (ESD) | Electrostatic discharge | Human body model (HBM), per ANSI/ESDA/JEDEC JS-001, all pins (1) Charged device model (CDM), per JEDEC specification JESD22-C101, | ±2000 ±500 | V      |

## 7.3 Recommended Operating Conditions

over operating free-air temperature range (unless otherwise noted)

| PARAMETER         | PARAMETER              | TEST CONDITIONS   |   MIN |   TYP | MAX        | UNIT   |
|-------------------|------------------------|-------------------|-------|-------|------------|--------|
| POWER SUPPLY      | POWER SUPPLY           |                   |       |       |            |        |
| AVDD              | Analog supply voltage  |                   |  2.35 |   3.3 | 5.5        | V      |
| DVDD              | Digital supply voltage |                   |  1.65 |   3.3 | 5.5        | V      |
| ANALOG INPUTS     | ANALOG INPUTS          |                   |       |       |            |        |
| FSR               | Full-scale input range | AIN X (1) - GND   |     0 |       | AVDD       | V      |
| V IN              | Absolute input voltage | AIN X - GND       |  -0.1 |       | AVDD + 0.1 | V      |
| TEMPERATURE RANGE | TEMPERATURE RANGE      |                   |       |       |            |        |
| T A               | Ambient temperature    |                   |   -40 |    25 | 85         | ℃      |

## 7.4 Thermal Information

| THERMAL METRIC (1)   | THERMAL METRIC (1)                           |   ADS7128 RTE (WQFN) 16 PINS | UNIT   |
|----------------------|----------------------------------------------|------------------------------|--------|
| R θ JA               | Junction-to-ambient thermal resistance       |                         49.7 | °C/W   |
| R θ JC(top)          | Junction-to-case (top) thermal resistance    |                         53.4 | °C/W   |
| R θ JB               | Junction-to-board thermal resistance         |                         24.7 | °C/W   |
| Ψ JT                 | Junction-to-top characterization parameter   |                          1.3 | °C/W   |
| Ψ JB                 | Junction-to-board characterization parameter |                         24.7 | °C/W   |
| R θ JC(bot)          | Junction-to-case (bottom) thermal resistance |                          9.3 | °C/W   |

## 7.5 Electrical Characteristics

at AVDD = 2.35 V to 5 V, DVDD = 1.65 V to 5.5 V, and maximum throughput (unless otherwise noted); minimum and maximum values at TA = -40°C to +85°C; typical values at TA = 25°C.

| PARAMETER                       | PARAMETER                          | TEST CONDITIONS                                                          | MIN        | TYP   | MAX        | UNIT   |
|---------------------------------|------------------------------------|--------------------------------------------------------------------------|------------|-------|------------|--------|
| ANALOG INPUTS                   | ANALOG INPUTS                      |                                                                          |            |       |            |        |
| C SH Sampling                   | capacitance                        |                                                                          |            | 12    |            | pF     |
| DC PERFORMANCE                  | DC PERFORMANCE                     |                                                                          |            |       |            |        |
|                                 | Resolution                         | No missing codes                                                         |            | 12    |            | bits   |
| DNL                             | Differential nonlinearity          |                                                                          | -0.75      | ±0.45 | 0.75       | LSB    |
| INL                             | Integral nonlinearity              |                                                                          | -1.5       | ±0.5  | 1.5        | LSB    |
| V (OS)                          | Input offset error                 | Post offset calibration                                                  | -2         | ±0.3  | 2          | LSB    |
|                                 | Input offset thermal drift         | Post offset calibration                                                  |            | ±1    |            | ppm/°C |
| G E                             | Gain error                         |                                                                          | -0.065     | ±0.05 | 0.065      | %FSR   |
|                                 | Gain error thermal drift           |                                                                          |            | ±1    |            | ppm/°C |
| AC PERFORMANCE                  | AC PERFORMANCE                     |                                                                          |            |       |            |        |
| SINAD                           |                                    | AVDD = 5 V, f IN = 2 kHz                                                 | 70         | 72.8  |            | dB     |
|                                 | Signal-to-noise + distortion ratio | AVDD = 3 V, f IN = 2 kHz                                                 | 69.8       | 72.4  |            |        |
| SNR                             |                                    | AVDD = 5 V, f IN = 2 kHz                                                 | 71.2       | 73    |            | dB     |
|                                 | Signal-to-noise ratio              | AVDD = 3 V, f IN = 2 kHz                                                 | 70.5       | 72.5  |            |        |
|                                 | Crosstalk                          | 100-kHz signal applied on any OFF channel and measured on the ON channel |            | -100  |            | dB     |
| DECAP Pin                       | DECAP Pin                          |                                                                          |            |       |            |        |
| C DECAP                         | Decoupling capacitor on DECAP pin  |                                                                          | 0.1        | 1     | 4.7        | µF     |
|                                 | Voltage output on DECAP pin        | C DECAP = 1 µF                                                           |            | 1.8   |            | V      |
| DIGITAL INPUT/OUTPUT (SCL, SDA) | DIGITAL INPUT/OUTPUT (SCL, SDA)    |                                                                          |            |       |            |        |
| V IH                            | Input high logic level             | All I 2 C modes                                                          | 0.7 x DVDD |       | 5.5        | V      |
| V IL                            | Input low logic level              | All I 2 C modes                                                          | -0.3       |       | 0.3 x DVDD | V      |
| V OL                            | Output low logic level             | Sink current = 2 mA, DVDD > 2 V                                          | 0          |       | 0.4        | V      |
|                                 |                                    | Sink current = 2 mA, DVDD ≤ 2 V fast                                     | 0          |       | 0.2 x DVDD |        |
| I OL                            | Low-level output current (sink)    | V OL = 0.4 V, standard and mode                                          |            |       | 3          | mA     |
|                                 |                                    | V OL = 0.6 V, fast mode                                                  |            |       | 6          |        |
|                                 |                                    | V OL = 0.4 V, fast mode plus                                             |            |       | 20         |        |
| GPIOs                           | GPIOs                              |                                                                          |            |       |            |        |
| V IH                            | Input high logic level             |                                                                          | 0.7 x AVDD |       | AVDD + 0.3 | V      |
| V IL                            | Input low logic level              |                                                                          | -0.3       |       | 0.3 x AVDD | V      |
|                                 | Input leakge current               | GPIO configured as input                                                 |            | 10    | 100        | nA     |
| V OH                            | Output high logic level            | GPO_DRIVE_CFG = push-pull, I SOURCE = 2 mA                               | 0.8 x AVDD |       | AVDD       | V      |
| V OL                            | Output low logic level             | I SINK = 2 mA                                                            | 0          |       | 0.2 x AVDD | V      |
| I OH                            | Output high source current         | V OH > 0.7 x AVDD                                                        |            |       | 5          | mA     |
| I OL                            | Output low sink current            | V OL < 0.3 x AVDD                                                        |            |       | 5          | mA     |
| DIGITAL OUTPUT (ALERT)          | DIGITAL OUTPUT (ALERT)             |                                                                          |            |       |            |        |
| V OH                            | Output high logic level            | GPO_DRIVE_CFG = push-pull, I SOURCE = 2 mA                               | 0.8 x DVDD |       | DVDD       | V      |
| V OL                            | Output low logic level             | I SINK = 2 mA                                                            | 0          |       | 0.2 x DVDD | V      |
| I OH                            | Output high sink current           | V OH > 0.7 x DVDD                                                        |            |       | 5          | mA     |
| I OL                            | Output low sink current            | V OL < 0.3 x DVDD                                                        |            |       | 5          | mA     |
| POWER SUPPLY CURRENTS           | POWER SUPPLY CURRENTS              |                                                                          |            |       |            |        |

![Image](output_part1_artifacts\image_000014_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000015_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Electrical Characteristics (continued)

at AVDD = 2.35 V to 5 V, DVDD = 1.65 V to 5.5 V, and maximum throughput (unless otherwise noted); minimum and maximum values at TA = -40°C to +85°C; typical values at TA = 25°C.

| PARAMETER   | PARAMETER             | TEST CONDITIONS                   | MIN   |   TYP |   MAX | UNIT   |
|-------------|-----------------------|-----------------------------------|-------|-------|-------|--------|
| I AVDD      | Analog supply current | I 2 C high-speed mode, AVDD = 5 V |       |   155 |   195 | µA     |
| I AVDD      | Analog supply current | I 2 C fast mode plus, AVDD = 5 V  |       |    45 |    75 | µA     |
| I AVDD      | Analog supply current | I 2 C fast mode, AVDD = 5 V       |       |    29 |    37 |        |
| I AVDD      | Analog supply current | I 2 C standard mode, AVDD = 5 V   |       |    13 |    18 |        |
| I AVDD      | Analog supply current | No conversion, AVDD = 5 V         |       |     7 |    15 |        |

## 7.6 I 2 C Timing Requirements

|         |                                               | MODE (1)                           | MODE (1)                           | MODE (1)        | MODE (1)        |      |
|---------|-----------------------------------------------|------------------------------------|------------------------------------|-----------------|-----------------|------|
|         |                                               | STANDARD, FAST, AND FAST MODE PLUS | STANDARD, FAST, AND FAST MODE PLUS | HIGH-SPEED MODE | HIGH-SPEED MODE | UNIT |
|         |                                               | MIN                                | MAX                                | MIN             | MAX             |      |
| f SCL   | SCL clock frequency (2)                       |                                    | 1                                  |                 | 3.4             | MHz  |
| t SUSTA | START condition setup time for repeated start | 260                                |                                    | 160             |                 | ns   |
| t HDSTA | Start condition hold time                     | 260                                |                                    | 160             |                 | ns   |
| t LOW   | Clock low period                              | 500                                |                                    | 160             |                 | ns   |
| t HIGH  | Clock high period                             | 260                                |                                    | 60              |                 | ns   |
| t SUDAT | Data in setup time                            | 50                                 |                                    | 10              |                 | ns   |
| t HDDAT | Data in hold time                             | 0                                  |                                    | 0               |                 | ns   |
| t R     | SCL rise time                                 |                                    | 120                                |                 | 80              | ns   |
| t F     | SCL fall time                                 |                                    | 120                                |                 | 80              | ns   |
| t SUSTO | STOP condition hold time                      | 260                                |                                    | 60              |                 | ns   |
| t BUF   | Bus free time before new transmission         | 500                                |                                    | 300             |                 | ns   |

## 7.7 Timing Requirements

at AVDD = 2.35 V to 5 V, DVDD = 1.65 V to 5.5 V, and maximum throughput (unless otherwise noted); minimum and maximum values at TA = -40°C to +85°C; typical values at TA = 25°C.

|       |                  |   MIN | MAX   | UNIT   |
|-------|------------------|-------|-------|--------|
| t ACQ | Acquisition time |   300 |       | ns     |

## 7.8 I 2 C Switching Characteristics

|           |                                                | MODE                               | MODE                               | MODE            |      |
|-----------|------------------------------------------------|------------------------------------|------------------------------------|-----------------|------|
|           |                                                | STANDARD, FAST, AND FAST MODE PLUS | STANDARD, FAST, AND FAST MODE PLUS | HIGH-SPEED MODE | UNIT |
|           |                                                | MIN                                | MAX                                | MIN MAX         |      |
| t VDDATA  | SCL low to SDA data out valid                  | 450                                |                                    | 200             | ns   |
| t VDACK   | SCL low to SDA acknowledge time                | 450                                |                                    | 200             | ns   |
| t STRETCH | Clock stretch time in one-shot conversion mode | 1400                               |                                    | 1000            | ns   |
| t SP      | Noise supression time constant on SDA and SCL  | 50                                 |                                    | 10              | ns   |

## 7.9 Switching Characteristics

at AVDD = 2.35 V to 5 V, DVDD = 1.65 V to 5.5 V, and maximum throughput (unless otherwise noted); minimum and maximum values at TA = -40°C to +85°C; typical values at TA = 25°C.

| PARAMETER        | PARAMETER                                             | TEST CONDITIONS                |   MIN | MAX       | UNIT   |
|------------------|-------------------------------------------------------|--------------------------------|-------|-----------|--------|
| CONVERSION CYCLE | CONVERSION CYCLE                                      |                                |       |           |        |
| t CONV           | ADC conversion time                                   | Manual and auto sequence modes |       | t STRETCH | ns     |
| t CONV           | ADC conversion time                                   | Autonomous mode                |       | 600       | ns     |
| RESET AND ALERT  | RESET AND ALERT                                       |                                |       |           |        |
| t PU             | Power-up time for device                              | AVDD ≥ 2.35 V                  |       | 5         | ms     |
| t RST            | Delay time; RST bit = 1b to device reset complete (1) |                                |       | 5         | ms     |
| t ALERT_HI       | ALERT high period                                     | ALERT_LOGIC[1:0] = 1x          |    50 | 150       | ns     |
| t ALERT_LO       | ALERT low period                                      | ALERT_LOGIC[1:0] = 1x          |    50 | 150       | ns     |

## (1) RST bit is automatically reset to 0b after t RST .

NOTE: S = start, Sr = repeated start, and P = stop.

![Image](output_part1_artifacts\image_000016_9c9edd03b92c32a1dbd39093c49a55507656066ef6f84753d47cb2aaccd01b8f.png)

Figure 1. I 2 C Timing Diagram

![Image](output_part1_artifacts\image_000017_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000018_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## www.ti.com

## 7.10 Typical Characteristics

at TA = 25°C, AVDD = 5 V, DVDD = 3.3 V, and maximum throughput (unless otherwise noted)

![Image](output_part1_artifacts\image_000019_bc463f087311315a6aa02bf814379e8bd23494df9422833b41cdb0b1ef401e6f.png)

![Image](output_part1_artifacts\image_000020_d724d59143c3b38284639ddde435a99828e1eb4fd53be90edc757f2e175c4eaa.png)

## Typical Characteristics (continued)

at TA = 25°C, AVDD = 5 V, DVDD = 3.3 V, and maximum throughput (unless otherwise noted)

![Image](output_part1_artifacts\image_000021_9231a25bd40d903578241d370e53dd29331af89316357f3805ee6ab67f1544d1.png)

![Image](output_part1_artifacts\image_000022_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000023_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Typical Characteristics (continued)

at TA = 25°C, AVDD = 5 V, DVDD = 3.3 V, and maximum throughput (unless otherwise noted)

![Image](output_part1_artifacts\image_000024_cbf3400b22bc82ba2a9f4dd32d27b88bbd17ca08d22055908d10ab887ca358eb.png)

## Typical Characteristics (continued)

at TA = 25°C, AVDD = 5 V, DVDD = 3.3 V, and maximum throughput (unless otherwise noted)

![Image](output_part1_artifacts\image_000025_246343f9cc1091e5e7640604a199808c04c241ec95ef30edf4060a8ea8c191ab.png)

Figure 20. Analog Supply Current vs Throughput

![Image](output_part1_artifacts\image_000026_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000027_984e37a9637b43c02ad19c5457d376fd8701ab65655a0901ff42174c5ccad8ba.png)

## 8 Detailed Description

## 8.1 Overview

The ADS7128 is a small, eight-channel, multiplexed, 12-bit, analog-to-digital converter (ADC) with an I 2 Ccompatible serial interface. The eight channels of the ADS7128 can be individually configured as either analog inputs, digital inputs, or digital outputs. The device includes a digital comparator with a dedicated alert pin that can be used to interrupt the host when a programmed high or low threshold is crossed on any input channel. The device uses an internal oscillator for conversion. The ADC can be used in the manual mode for reading ADC data over the I 2 C interface or in autonomous mode for monitoring the analog inputs without an active I 2 C interface.

The device features a programmable averaging filter that outputs a 16-bit result for enhanced resolution. The root-mean-square (RMS) module computes a 16-bit true RMS result of any analog input channel over a configurable time window. The zero-crossing-detect (ZCD) module can be used to generate a digital output corresponding to the programmable threshold crossings of any analog input channel.

The I 2 C serial interface supports standard-mode, fast-mode, fast-mode plus, and high-speed mode. The device also features an 8-bit cyclic redundancy check (CRC) for the serial communication interface.

## 8.2 Functional Block Diagram

![Image](output_part1_artifacts\image_000028_bd6c4d4ebef7694e8626f7bcf78c55e639483ba6e0d17d7dba41e0613d307c49.png)

## 8.3 Feature Description

## 8.3.1 Multiplexer and ADC

The eight channels of the multiplexer can be independently configured as ADC inputs or general-purpose inputs/outputs (GPIOs). Figure 21 shows that each input pin has electrostatic discharge (ESD) protection diodes to AVDD and GND. On power-up or after device reset, all eight multiplexer channels are configured as analog inputs.

Figure 21 shows an equivalent circuit for pins configured as analog inputs. The ADC sampling switch is represented by an ideal switch (SW) in series with the resistor, RSW (typically 150 Ω ), and the sampling capacitor, CSH (typically 12 pF).

Figure 21. Analog Inputs, GPIOs, and ADC Connections

![Image](output_part1_artifacts\image_000029_b49a101897abb2e50fd5e14b19dc87797c61fef0ddd97aa883ec665d3f9a366f.png)

During acquisition, the SW switch is closed to allow the signal on the selected analog input channel to charge the internal sampling capacitor. During conversion, the SW switch is opened to disconnect the analog input channel from the sampling capacitor.

The multiplexer channels can be configured as GPIOs in the PIN\_CFG register. The direction of a GPIO (either as an input or an output) can be set in the GPIO\_CFG register. The logic level on the channels configured as digital I/O can be read from the GPI\_VALUE register. The digital outputs can be accessed by writing to the GPO\_VALUE register. The digital outputs can be configured as either open-drain or push-pull in the GPO\_DRIVE\_CFG register.

## 8.3.2 Reference

The device uses the analog supply voltage (AVDD) as a reference for the analog-to-digital conversion process. TI recommends connecting a 1-µF, low-equivalent series resistance (ESR) ceramic decoupling capacitor between the AVDD and GND pins.

## 8.3.3 ADC Transfer Function

The ADC output is in straight binary format. Equation 1 computes the ADC resolution:

<!-- formula-not-decoded -->

## where:

- VREF = AVDD
- N = 12

Figure 22 and Table 1 detail the transfer characteristics for the device.

![Image](output_part1_artifacts\image_000030_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

(1)

![Image](output_part1_artifacts\image_000031_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Feature Description (continued)

Figure 22. Ideal Transfer Characteristics

![Image](output_part1_artifacts\image_000032_4c57f6bb7db4c452ed26653552dfb6667aae2ced93e3b9378689ddb50b2dd4b0.png)

Table 1. Transfer Characteristics

| INPUT VOLTAGE                            | CODE     | DESCRIPTION              | IDEAL OUTPUT CODE   |
|------------------------------------------|----------|--------------------------|---------------------|
| ≤ 1 LSB                                  | NFSC     | Negative full-scale code | 000                 |
| 1 LSB to 2 LSBs                          | NFSC + 1 | -                        | 001                 |
| (AVDD / 2) to (AVDD / 2) + 1 LSB         | MC       | Mid code                 | 800                 |
| (AVDD / 2) + 1 LSB to (AVDD / 2) + 2 LSB | MC + 1   | -                        | 801                 |
| ≥ AVDD - 1 LSB                           | PFSC     | Positive full-scale code | FFF                 |

## 8.3.4 ADC Offset Calibration

The variation in ADC offset error resulting from changes in temperature or AVDD can be calibrated by setting the CAL bit in the GENERAL\_CFG register. The CAL bit is reset to 0 after calibration. The host can poll the CAL bit to check the ADC offset calibration completion status.

## 8.3.5 I 2 C Address Selector

The I 2 C address for the device is determined by connecting external resistors on the ADDR pin. The device address is determined at power-up based on the resistor values. The device retains this address until the next power-up event, until the next device reset, or until the device receives a command to program its own address. Figure 23 shows a connection diagram for the ADDR pin and Table 2 lists the resistor values for selecting different addresses of the device.

Figure 23. External Resistor Connection Diagram for the ADDR Pin

![Image](output_part1_artifacts\image_000033_6a8e90a65f4648b5303051ce51b894e2760a97c0a91deb705058be87764678c3.png)

![Image](output_part1_artifacts\image_000034_d724d59143c3b38284639ddde435a99828e1eb4fd53be90edc757f2e175c4eaa.png)

Table 2. I 2 C Address Selection

| RESISTORS   | RESISTORS   | ADDRESS         |
|-------------|-------------|-----------------|
| R1 (1)      | R2 (1)      | ADDRESS         |
| 0 Ω         | DNP (2)     | 001 0111b (17h) |
| 11 k Ω      | DNP (2)     | 001 0110b (16h) |
| 33 k Ω      | DNP (2)     | 001 0101b (15h) |
| 100 k Ω     | DNP (2)     | 001 0100b (14h) |
| DNP (2)     | DNP (2)     | 001 0000b (10h) |
| DNP (2)     | 11 k Ω      | 001 0001b (11h) |
| DNP (2)     | 33 k Ω      | 001 0010b (12h) |
| DNP (2)     | 100 k Ω     | 001 0011b (13h) |

- (1) Tolerance for R1, R2 ≤ ±5%.
- (2) DNP = Do not populate.

## 8.3.6 Programmable Averaging Filter

The ADS7128 features a built-in oversampling (OSR) function that can be used to average several samples. The averaging filter can be enabled by programming the OSR[2:0] bits in the OSR\_CFG register. The averaging filter configuration is common to all analog input channels. Figure 24 shows that the averaging filter module output is 16 bits long. In the manual conversion mode and auto-sequence mode, only the first conversion for the selected analog input channel must be initiated by the host; see the Manual Mode and Auto-Sequence Mode sections. As shown in Figure 24, any remaining conversions for the selected averaging factor are generated internally. The time required to complete the averaging operation is determined by the sampling speed and number of samples to be averaged. As shown in Figure 24, the 16-bit result can be read out after the averaging operation completes.

Figure 24. Averaging Example

![Image](output_part1_artifacts\image_000035_29d62b74736fb6cde6613fe7905528b23969d226c7f4244d962ab22ea69251c3.png)

In Figure 24, SCL is stretched by the device after the start of conversions until the averaging operation is complete.

If SCL stretching is not required during averaging, enable the statistics registers by setting STATS\_EN to 1b and initiate conversions by writing 1b to the CNVST bit. The OSR\_DONE bit in the SYSTEM\_STATUS register can be polled to check the averaging completion status. When using the CNVST bit to initiate conversion, the result can be read in the RECENT\_CHx\_LSB and RECENT\_CHx\_MSB registers.

In the autonomous mode of operation, samples from the analog input channels that are enabled in the AUTO\_SEQ\_CH\_SEL register are averaged sequentially; see the Autonomous Mode section. The digital window comparator compares the top 12 bits of the 16-bit average result with the thresholds.

Equation 2 provides the LSB value of the 16-bit average result.

<!-- formula-not-decoded -->

![Image](output_part1_artifacts\image_000036_d84d49d3d31a7a3fd6641803ddd3654a586bda0028dd44c4bda3eac3ddabaa51.png)

![Image](output_part1_artifacts\image_000037_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 8.3.7 CRC on Data Interface

The ADS7128 features a cyclic redundancy check (CRC) module for checking the integrity of the data bits exchanged over the I 2 C interface. The CRC module is bidirectional and appends an 8-bit CRC to every byte read from the device while also evaluating the CRC of every incoming byte over the I 2 C interface. The CRC module uses the CRC-8-CCITT polynomial (x 8 + x 2 + x + 1) for CRC computation.

To enable the CRC module, set the CRC\_EN bit in the GENERAL\_CFG register. Table 3 shows how a CRC error can be detected when configuring the ADS7128.

Table 3. Configuration Notifications When a CRC Error is Detected

| CRC ERROR NOTIFICATION   | CONFIGURATION       | DESCRIPTION                                                                                      |
|--------------------------|---------------------|--------------------------------------------------------------------------------------------------|
| ALERT pin                | ALERT_CRCIN = 1b    | ALERT pin is asserted if a CRC error is detected by the device.                                  |
| Status flags             | APPEND_STATUS = 10b | 4-bit status flags are appended to the ADC data; see the Output Data Format section for details. |
| Register read            | -                   | Read the CRC_ERR_IN bit to check if a CRC error is detected.                                     |

When the ADS7128 detects a CRC error, the erroneous data are ignored and the CRC\_ERR\_IN bit is set. Table 3 describes the additional notifications that can be enabled. Further register writes are disabled until the CRC\_ERR\_IN bit is cleared by writing 1b to it. When using autonomous mode, further conversions can be disabled on the CRC error by setting CONV\_ON\_ERR to 1b; see the Autonomous Mode section.

## 8.3.8 General-Purpose I/Os (GPIOs)

The eight channels of the ADS7128 can be independently configured as analog inputs, digital inputs, or digital outputs. Table 4 describes how the PIN\_CFG and GPIO\_CFG registers can be used to configure the channels.

Table 4. Configuring Channels as Analog Inputs or GPIOs

|   PIN_CFG[7:0] | GPIO_CFG[7:0]   | GPO_DRIVE_CFG[7:0]   | CHANNEL CONFIGURATION             |
|----------------|-----------------|----------------------|-----------------------------------|
|              0 | x               | x                    | Analog input (default)            |
|              1 | 0               | x                    | Digital input                     |
|              1 | 1               | 0                    | Digital output; open-drain driver |
|              1 | 1               | 1                    | Digital output; push-pull driver  |

The digital outputs can be configured to logic 1 or 0 by writing to the GPO\_VALUE register. Reading the GPI\_VALUE register returns the logic level for all channels configured as digital inputs.

## 8.3.9 Oscillator and Timing Control

The device uses an internal oscillator for conversions. When using the averaging module or the RMS module, the host initiates the first conversion and all subsequent conversions are generated internally by the device. However, in the autonomous mode of operation, the start of the conversion signal is generated by the device. Table 5 shows that when the device generates the start of the conversion, the sampling rate is controlled by the OSC\_SEL and CLK\_DIV[3:0] register fields.

Table 5. Configuring Sampling Rate for Internal Conversion Start Control

| CLK_DIV[3:0]   | OSC_SEL = 0                        | OSC_SEL = 0              | OSC_SEL = 1                        | OSC_SEL = 1              |
|----------------|------------------------------------|--------------------------|------------------------------------|--------------------------|
| CLK_DIV[3:0]   | SAMPLING FREQUENCY, f CYCLE (kSPS) | CYCLE TIME, t CYCLE (µs) | SAMPLING FREQUENCY, f CYCLE (kSPS) | CYCLE TIME, t CYCLE (µs) |
| 0000b          | 1000                               | 1                        | 31.25                              | 32                       |
| 0001b          | 666.7                              | 1.5                      | 20.83                              | 48                       |
| 0010b          | 500                                | 2                        | 15.63                              | 64                       |
| 0011b          | 333.3                              | 3                        | 10.42                              | 96                       |
| 0100b          | 250                                | 4                        | 7.81                               | 128                      |
| 0101b          | 166.7                              | 6                        | 5.21                               | 192                      |
| 0110b          | 125                                | 8                        | 3.91                               | 256                      |
| 0111b          | 83                                 | 12                       | 2.60                               | 384                      |
| 1000b          | 62.5                               | 16                       | 1.95                               | 512                      |
| 1001b          | 41.7                               | 24                       | 1.3                                | 768                      |
| 1010b          | 31.3                               | 32                       | 0.98                               | 1024                     |
| 1011b          | 20.8                               | 48                       | 0.65                               | 1536                     |
| 1100b          | 15.6                               | 64                       | 0.49                               | 2048                     |
| 1101b          | 10.4                               | 96                       | 0.33                               | 3072                     |
| 1110b          | 7.8                                | 128                      | 0.24                               | 4096                     |
| 1111b          | 5.2                                | 192                      | 0.16                               | 6144                     |

The conversion time of the device (see tCONV in the Switching Characteristics table) is independent of the OSC\_SEL and CLK\_DIV[3:0] configuration.

## 8.3.10 Output Data Format

Figure 25 illustrates various I 2 C frames for reading data.

- Read the ADC conversion result: Two 8-bit I 2 C packets are required (frame A).
- Read the averaged conversion result: Two 8-bit I 2 C packets are required (frame B).
- Read data with the channel ID or status flags appended: The 4-bit channel ID or status flags can be appended to the 12-bit ADC result by configuring the APPEND\_STATUS field in the GENERAL\_CFG register. The status flags can be used to detect if a CRC error is detected and if an alert condition is detected by the digital window comparator. When the channel ID or status flags are appended to the 12-bit ADC data, two I 2 C packets are required (frame C). If the channel ID or status flags are appended to the 16-bit average result, three I 2 C frames are required (frame D).

![Image](output_part1_artifacts\image_000038_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000039_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

Figure 25. Data Frames for Reading Data

![Image](output_part1_artifacts\image_000040_367534769a9a636945b8ad29090f9a9844565e946dea2eb63d5bdaba8a07ca40.png)

When status flags are enabled, APPEND\_STATUS is set to 10b and four bits are appended to the ADC output. The device outputs status flags in this order: {1b, 0b, CRCERR\_IN, ALERT}. The level transitions on the digital interface, resulting from the fixed 1b and 0b in the status flags, can be used to detect if the digital outputs are shorted to a fixed voltage in the system. The CRCERR\_IN flag reflects the corresponding bit in the GENERAL\_CFG register. The ALERT flag is the output of the logical OR of the bits in the EVENT\_FLAG register.

## 8.3.11 Digital Window Comparator

The internal digital window comparator (DWC) is available in all functional modes of the device (see the Device Functional Modes section for details). The digital window comparator controls output of the ALERT pin buffer. The ALERT pin can be configured as open-drain (default) or push-pull output using the ALERT\_DRIVE bit in the ALERT\_PIN\_CFG register. Figure 26 shows a block diagram for the digital window comparator.

Figure 26. Digital Window Comparator Block Diagram

![Image](output_part1_artifacts\image_000041_8e0f7e5a15c8423ff3ff9a320639717ad69972f2348c7a7a66e4562ba379516f.png)

![Image](output_part1_artifacts\image_000042_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

The low-side threshold, high-side threshold, event counter, and hysteresis parameters are independently programmable for each input channel. Figure 27 shows the events that can be monitored for every analog input channel by the window comparator.

![Image](output_part1_artifacts\image_000043_abe07780ac8ef26e79bbe534b37710ac567df01ad67f9474cbfa68aed17b8d0d.png)

xxxxxxxxxxxxxxxxxxxxxxxxx

xxxxxxxxxxxxxxxxxxxxxxxxx

xxxxxxxxxxxxxxxxxxxxxxxxx

xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxxxxxxxxxxx Figure 27. Event Monitoring With the Window Comparator To enable the digital window comparator, set the DWC\_EN bit in the GENERAL\_CFG register. By default, hysteresis is 0, the high threshold is 0xFFF, and the low threshold is 0x000. A 12-bit straight binary code cannot be higher than 0xFFF or lower than 0x000, thus the thresholds have no effect unless set to different values. Figure 27 shows the various types of event that can be detected by adjusting the thresholds. For detecting when a signal is in-band, the EVENT\_RGN register must be configured. In each of the cases shown in Figure 27, either or both EVENT\_HIGH\_FLAG and EVENT\_LOW\_FLAG can be set.

The programmable event counter counts consecutive thresholds violations before alert flags can be set. The event count can be set to a higher value to avoid transients in the input signal setting the alert flags.

In order to assert the ALERT pin when the alert flag is set for a particular analog input channel, set the corresponding bit in the ALERT\_CH\_SEL register. Alert flags are set regardless of the ALERT\_CH\_SEL configuration if DWC\_EN is 1 and the high or low thresholds are exceeded.

![Image](output_part1_artifacts\image_000044_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 8.3.11.1 Interrupts From Digital Inputs

Logic 1 or logic 0 events can detected on channels configured as digital inputs, as shown in Table 6, by enabling the corresponding ALERT\_CH\_SEL bit.

Table 6. Configuring Interrupts From Digital Inputs

|   PIN_CFG[7:0] |   GPIO_CFG[7:0] |   ALERT_CH_SEL[7: 0] |   EVENT_RGN [7:0] | EVENT DESCRIPTION                                                |
|----------------|-----------------|----------------------|-------------------|------------------------------------------------------------------|
|              1 |               0 |                    1 |                 0 | EVENT_HIGH_FLAG is set when digital input channel is at logic 1. |
|              1 |               0 |                    1 |                 1 | EVENT_LOW_FLAG is set when digital input channel is at logic 0.  |

## 8.3.11.2 Changing Digital Outputs on Alert and ZCD

Figure 28 shows how digital outputs can be updated in response to alerts from individual channels or synchronized to the zero-crossing-detect signal.

Figure 28. Block Diagram for the Digital Output Logic

![Image](output_part1_artifacts\image_000045_0f8009828cb0a272b7069ae90b17c493d8a3113c8f2e63c6306092fae245d5ec.png)

## 8.3.11.2.1 Changing Digital Outputs on Alerts

Any given digital output can be updated in response to an alert condition on one or more analog inputs and digital inputs. To update the digital output in response to alert conditions, the trigger must be configured and the value must be launched on the trigger.

## 8.3.11.2.1.1 Trigger

The following events can act as triggers for updating the value on the digital output:

- An alert occurs on one or more analog input channels. The digital window comparator must be enabled for these channels.
- An alert occurs on one or more digital input channels. The digital window comparator must be enabled for these channels.

Configure the GPOx\_TRIG\_EVENT\_SEL register to select which channels, analog inputs, or digital inputs can trigger an update on the digital output pin. After configuring the triggers for updating a digital output, the logic can be enabled by configuring the corresponding bit in the GPO\_TRIGGER\_UPDATE\_EN register.

## 8.3.11.2.1.2 Output Value

The digital outputs can be set to logic 1 or logic 0 in response to the triggers. The value to be updated on the digital output when a trigger event occurs can be configured in the GPO\_VALUE\_ON\_TRIGGER register.

## 8.3.11.2.2 Changing Digital Outputs Synchronous to the Zero-Crossing Detect

Individual digital outputs can be set to either logic 0, logic 1, ZCD, or ZCD synchronous to the zero-crossingdetect signal. This function can be enabled for individual digital outputs by configuring the GPO\_VALUE\_ON\_ZCD\_CFG\_CHx field and setting the corresponding bit in the GPO\_ZCD\_UPDATE\_EN [7:0] register.

## 8.3.12 Root-Mean-Square Module

The ADS7128 features an RMS computation module. Any one analog input channel can be selected for computing the RMS result. The RMS result is computed over a block of samples from the selected channel and the result can be read from the RMS\_RESULT\_LSB and RMS\_RESULT\_MSB registers. Equation 3 shows how the RMS result is computed by calculating the 16-bit square root of the mean of the accumulated result of the squares of the ADC conversion data.

<!-- formula-not-decoded -->

where

- D is the data corresponding to the analog input channel selected for RMS measurement
- N is the number of samples over which the RMS is computed
- (3)

The DC offset must be subtracted from the AC component because the analog input signal to the ADC is unipolar. DC subtraction can be enabled or disabled, as given by b in Equation 3, by configuring the DC\_SUB field. When DC subtraction is enabled, the DC input voltage must be within ±5% tolerance of the mid-scale voltage i.e. (0.5 × AVDD) ± 5%.

The RMS result is 16 bits long and Equation 4 gives the size of the 1 LSB of RMS result.

<!-- formula-not-decoded -->

The procedure for using the RMS module is outlined in the steps below:

1. Select the channel for the RMS computation using the RMS\_CHID field in the RMS\_CFG register.
2. Define the time over which the RMS is to be computed by configuring the RMS\_SAMPLES field.
3. Start the RMS computation by setting RMS\_EN to 1 in the GENERAL\_CFG register.
4. The device starts computing the RMS result when the sample size defined by RMS\_SAMPLES is converted on the analog input selected for RMS computation. An additional 40 samples must be converted to complete the RMS computation.
5. To monitor for when the RMS computation completes, poll the RMS\_DONE bit in the SYSTEM\_STATUS register. The ALERT pin can also be used for requesting an interrupt by configuring the ALERT\_RMS bit in the ALERT\_MAP register.

![Image](output_part1_artifacts\image_000046_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000047_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

6. For starting a new RMS measurement, write 1 to the RMS\_EN bit in the GENERAL\_CFG register.

## 8.3.13 Zero-Crossing-Detect Module

Figure 29 shows the zero-crossing-detection (ZCD) module that generates a digital output corresponding to the threshold crossings of an analog input. In order to detect threshold crossings on a particular analog input, configure the 4-bit channel ID in the ZCD\_CHID register field. The threshold crossing to be detected can be configured in the HIGH\_TH register. The output of the ZCD module can be mapped to any digital output by configuring the GPO\_ZCD\_UPDATE\_EN, GPO\_VALUE\_ZCD\_CFG\_CH0\_CH3, and GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 registers.

Figure 29. ZCD Module Operation Block Diagram

![Image](output_part1_artifacts\image_000048_ebe97d3202dbe0225f83f55793777d1aa7f66b4194f64ba275ddad81ec87dad6.png)

The ADC conversion result of the selected analog input channel is compared with the digital threshold and the digital output is set accordingly. Equation 5 shows how transients near zero crossings can be rejected by configuring the ZCD\_BLANKING register.

<!-- formula-not-decoded -->

## 8.3.14 Minimum, Maximum, and Latest Data Registers

The ADS7128 can record the minimum, maximum, and latest code (statistics registers) for every analog input channel. To enable or re-enable recording statistics, set the STATS\_EN bit in the GENERAL\_CFG register. Writing 1 to the STATS\_EN bit reinitializes the statistics module, after which results from new conversions are recorded in the statistics registers. Until a new conversion result is available, previous values can be read from the statistics registers. Before reading the statistics registers, set STATS\_EN to 0 to prevent any updates to this register block.

## 8.3.15 I 2 C Protocol Features

## 8.3.15.1 General Call

On receiving a general call (00h), the device provides an acknowledge (ACK).

## 8.3.15.2 General Call With Software Reset

On receiving a general call (00h) followed by a software reset (06h), the device resets itself.

## 8.3.15.3 General Call With a Software Write to the Programmable Part of the Slave Address

On receiving a general call (00h) followed by 04h, the device reevaluates its own I 2 C address configured by the ADDR pin. During this operation, the device does not respond to other I 2 C commands except the general-call command.

## 8.3.15.4 Configuring the Device for High-Speed I 2 C Mode

The device can be configured in high-speed I 2 C mode by providing an I 2 C frame with one of these codes: 0x09, 0x0B, 0x0D, or 0x0F.

After receiving one of these codes, the device sets the I2C\_HIGH\_SPEED bit in the SYSTEM\_STATUS register and remains in high-speed I 2 C mode until a STOP condition is received in an I 2 C frame.

## 8.4 Device Functional Modes

Table 7 lists the functional modes supported by the ADS7128.

Table 7. Functional Modes

| FUNCTIONAL MODE   | CONVERSION CONTROL            | MUX CONTROL                   | CONV_MODE[1:0]   | SEQ_MODE[1:0]   |
|-------------------|-------------------------------|-------------------------------|------------------|-----------------|
| Manual            | 9th falling edge of SCL (ACK) | Register write to MANUAL_CHID | 00b              | 00b             |
| Auto-sequence     | 9th falling edge of SCL (ACK) | Channel sequencer             | 00b              | 01b             |
| Autonomous        | Internal to the device        | Channel sequencer             | 01b              | 01b             |

The device powers up in manual mode (see the Manual Mode section) and can be configured into any mode listed in Table 7 by writing the configuration registers for the desired mode.

## 8.4.1 Device Power-Up and Reset

On power-up, the device calculates the address from the resistors connected on the ADDR pin and the BOR bit is set, thus indicating a power-cycle or reset event.

The device can be reset by an I 2 C general call (00h) followed by a software reset (06h), by setting the RST bit, or by recycling the power on the AVDD pin.

## 8.4.2 Manual Mode

Manual mode allows the external host processor to directly select the analog input channel. Figure 30 lists the steps for operating the device in manual mode.

![Image](output_part1_artifacts\image_000049_57296f2418194e87cb06cfbcb002b89e086396e8f998ace9ca986b6297c8b26a.png)

Manual mode with channel selection using register write

Figure 30. Device Operation in Manual Mode

![Image](output_part1_artifacts\image_000050_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000051_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## www.ti.com

Provide an I 2 C start or restart frame to initiate a conversion, as shown in the conversion start frame of Figure 31, after configuring the device registers. ADC data can be read in subsequent I 2 C frames. The number of I 2 C frames required to read conversion data depends on the output data frame size; see the Output Data Format section for more details. A new conversion is initiated on the ninth falling edge of SCL (ACK bit) when the last byte of output data is read.

Figure 31. Starting a Conversion and Reading Data in Manual Mode

![Image](output_part1_artifacts\image_000052_c48fcc3887d71eb4a1ae4488fae3f4e47b762ad72c7b483990354faae675320c.png)

## 8.4.3 Auto-Sequence Mode

In auto-sequence mode, the internal channel sequencer switches the multiplexer to the next analog input channel after every conversion. The desired analog input channels can be configured for sequencing in the AUTO\_SEQ\_CHSEL register. To enable the channel sequencer, set SEQ\_START to 1b. After every conversion, the channel sequencer switches the multiplexer to the next analog input in ascending order. To stop the channel sequencer from selecting channels, set SEQ\_START to 0b. Figure 32 lists the conversion start and read frames for auto-sequence mode.

Figure 32. Device Operation in Auto-Sequence Mode

![Image](output_part1_artifacts\image_000053_de0293f3e0bd92a2d0cc5e1e58389350435f738da79ba878b2df81dacb8665dc.png)

## 8.4.4 Autonomous Mode

In autonomous mode, the device can be programmed to monitor the voltage applied on the analog input pins of the device and generate a signal on the ALERT pin when the programmable high or low threshold values are crossed. In this mode, the device generates the start of conversion using the internal oscillator. The first start of conversion must be provided by the host and the device then generates the subsequent start of conversions.

Figure 33 shows the steps for configuring the operation mode to autonomous mode. Abort the ongoing sequence by setting SEQ\_START to 0b before changing the functional mode or device configuration.

Figure 33. Configuring the Device in Autonomous Mode

![Image](output_part1_artifacts\image_000054_6b13bb867d28c4f142516805925ee5858cbdab36c9a92192b6837c42463488c9.png)

![Image](output_part1_artifacts\image_000055_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000056_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

www.ti.com

## 8.5 Programming

Table 8 provides the acronyms for different conditions in an I 2 C frame. Table 9 lists the various command opcodes.

Table 8. I 2 C Frame Acronyms

| SYMBOL   | DESCRIPTION                           |
|----------|---------------------------------------|
| S        | Start condition for the I 2 C frame   |
| Sr       | Restart condition for the I 2 C frame |
| P        | Stop condition for the I 2 C frame    |
| A        | ACK (low)                             |
| N        | NACK (high)                           |
| R        | Read bit (high)                       |
| W        | Write bit (low)                       |

## Table 9. Opcodes for Commands

| OPCODE     | COMMAND DESCRIPTION                     |
|------------|-----------------------------------------|
| 0001 0000b | Single register read                    |
| 0000 1000b | Single register write                   |
| 0001 1000b | Set bit                                 |
| 0010 0000b | Clear bit                               |
| 0011 0000b | Reading a continuous block of registers |
| 0010 1000b | Writing a continuous block of registers |

## 8.5.1 Reading Registers

The I 2 C master can either read a single register or a continuous block registers from the device, as described in the Single Register Read and Reading a Continuous Block of Registers sections.

## 8.5.1.1 Single Register Read

To read a single register from the device, the I 2 C master must provide an I 2 C command with three frames to set the register address for reading data. Table 9 lists the opcodes for different commands. After this command is provided, the I 2 C master must provide another I 2 C frame (as shown in Figure 34) containing the device address and the read bit. After this frame, the device provides the register data. The device provides the same register data even if the host provides more clocks. To end the register read command, the master must provide a STOP or a RESTART condition in the I 2 C frame.

![Image](output_part1_artifacts\image_000057_f92cd7b5ae5a25edf8882fb59726841863ff923fceee54a457e37f582dfe21db.png)

| S   | 7-bit Slave Address   | W   | A   | 0001 0000b   | A   | Register Address   | A   | P/Sr   | S   | 7-bit Slave Address   | R   | A   | Register Data   | A P/Sr   |
|-----|-----------------------|-----|-----|--------------|-----|--------------------|-----|--------|-----|-----------------------|-----|-----|-----------------|----------|

0001 0000b

A

S

7-bit Slave Address

W

A

- [ ] Data from host to device

- [ ] Data from device to host

Register

Address

A

P/Sr

Register Data

A

S

7-bit Slave Address

R

A

P/Sr

NOTE: S = start, Sr = repeated start, and P = stop.

Figure 34. Reading Register Data

## 8.5.1.2 Reading a Continuous Block of Registers

To read a continuous block of registers, the I 2 C master must provide an I 2 C command to set the register address. The register address is the address of the first register in the block that must be read. After this command is provided, the I 2 C master must provide another I 2 C frame, as shown in Figure 35, containing the device address and the read bit. After this frame, the device provides the register data. The device provides data for the next register when more clocks are provided. When data are read from addresses that do not exist in the register map of the device, the device returns zeros. If the device does not have any further registers to provide data on, the device provide zeros. To end the register read command, the master must provide a STOP or a RESTART condition in the I 2 C frame.

0011 0000b

A

S

7-bit Slave Address

W

A

Data from host to device

Data from device to host

1 st

Reg Address

in the Block

A

P/Sr

Register Data

A

S

7-bit Slave Address

R

A

P/Sr

NOTE: S = start, Sr = repeated start, and P = stop.

Figure 35. Reading a Continuous Block of Registers

![Image](output_part1_artifacts\image_000058_25a90ac4aa208175e5bae4bc12e085106abdb2862a6a193857e1a3b8eea34688.png)

## 8.5.2 Writing Registers

The I 2 C master can either write a single register or a continuous block of registers to the device, set a few bits in a register, or clear a few bits in a register.

## 8.5.2.1 Single Register Write

To write a single register from the device, as shown in Figure 36, the I 2 C master must provide an I 2 C command with four frames. The register address is the address of the register that must be written and the register data is the value that must be written. Table 9 lists the opcodes for different commands. To end the register write command, the master must provide a STOP or a RESTART condition in the I 2 C frame.

![Image](output_part1_artifacts\image_000059_dccec704cad11bc812274eff748387a8c84657f45247cda3d9c4c43f53a277eb.png)

0000 1000b

A

S

7-bit Slave Address

W

A

- [ ] Data from host to device

- [ ] Data from device to host

Register

Address

A

Register Data

A

P/Sr

NOTE: S = start, Sr = repeated start, and P = stop.

Figure 36. Writing a Single Register

## 8.5.2.2 Set Bit

The I 2 C master must provide an I 2 C command with four frames, as shown in Figure 36, to set bits in a register without changing the other bits. The register address is the address of the register that the bits must set and the register data is the value representing the bits that must be set. Bits with a value of 1 in the register data are set and bits with a value of 0 in the register data are not changed. Table 9 lists the opcodes for different commands. To end this command, the master must provide a STOP or RESTART condition in the I 2 C frame.

## 8.5.2.3 Clear Bit

The I 2 C master must provide an I 2 C command with four frames, as shown in Figure 36, to clear bits in a register without changing the other bits. The register address is the address of the register that the bits must clear and the register data is the value representing the bits that must be cleared. Bits with a value of 1 in the register data are cleared and bits with a value of 0 in the register data are not changed. Table 9 lists the opcodes for different commands. To end this command, the master must provide a STOP or a RESTART condition in the I 2 C frame.

![Image](output_part1_artifacts\image_000060_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part1_artifacts\image_000061_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 8.5.2.4 Writing a Continuous Block of Registers

The I 2 C master must provide an I 2 C command, as shown in Figure 37, to write a continuous block of registers. The register address is the address of the first register in the block that must be written. The I 2 C master must provide data for registers in subsequent I 2 C frames in an ascending order of register addresses. Writing data to addresses that do not exist in the register map of the device have no effect. Table 9 lists the opcodes for different commands. If the data provided by the I 2 C master exceeds the address space of the device, the device ignores the data beyond the address space. To end the register write command, the master must provide a STOP or a RESTART condition in the I 2 C frame.

Figure 37. Writing a Continuous Block of Registers

![Image](output_part1_artifacts\image_000062_35cb0af77eaefbcb42a22c7edf57df8f424797d4c4df52658fd5385ca436ac8c.png)

## 8.6 ADS7128 Registers

Table 10 lists the ADS7128 registers. All register offset addresses not listed in Table 10 should be considered as reserved locations and the register contents should not be modified.

Table 10. ADS7128 Registers

| Address   | Acronym          | Section                                                                                                |
|-----------|------------------|--------------------------------------------------------------------------------------------------------|
| 0x0       | SYSTEM_STATUS    | SYSTEM_STATUS Register (Address = 0x0) [reset = 0x81]                                                  |
| 0x1       | GENERAL_CFG      | GENERAL_CFG Register (Address = 0x1) [reset = 0x0]                                                     |
| 0x2       | DATA_CFG         | DATA_CFG Register (Address = 0x2) [reset = 0x0]                                                        |
| 0x3       | OSR_CFG          | OSR_CFG Register (Address = 0x3) [reset = 0x0]                                                         |
| 0x4       | OPMODE_CFG       | OPMODE_CFG Register (Address = 0x4) [reset = 0x0]                                                      |
| 0x5       | PIN_CFG          | PIN_CFG Register (Address = 0x5) [reset = 0x0]                                                         |
| 0x7       | GPIO_CFG         | GPIO_CFG Register (Address = 0x7) [reset = 0x0]                                                        |
| 0x9       | GPO_DRIVE_CFG    | GPO_DRIVE_CFG Register (Address = 0x9) [reset = 0x0]                                                   |
| 0xB       | GPO_VALUE        | GPO_VALUE Register (Address = 0xB) [reset = 0x0]                                                       |
| 0xD       | GPI_VALUE        | GPI_VALUE Register (Address = 0xD) [reset = 0x0]                                                       |
| 0xF       | ZCD_BLANKING_CFG | ZCD_BLANKING_CFG Register (Address = 0xF) [reset = 0x0]                                                |
| 0x10      | SEQUENCE_CFG     | SEQUENCE_CFG Register (Address = 0x10) [reset = 0x0]                                                   |
| 0x11      | CHANNEL_SEL      | CHANNEL_SEL Register (Address = 0x11) [reset = 0x0]                                                    |
| 0x12      | AUTO_SEQ_CH_SEL  | AUTO_SEQ_CH_SEL Register (Address = 0x12) [reset = 0x0]                                                |
| 0x14      | ALERT_CH_SEL     | ALERT_CH_SEL Register (Address = 0x14) [reset = 0x0]                                                   |
| 0x16      | ALERT_MAP        | ALERT_MAP Register (Address = 0x16) [reset = 0x0]                                                      |
| 0x17      | ALERT_PIN_CFG    | ALERT_PIN_CFG Register (Address = 0x17) [reset = 0x0]                                                  |
| 0x18      | EVENT_FLAG       | EVENT_FLAG Register (Address = 0x18) [reset = 0x0]                                                     |
| 0x1A      | EVENT_HIGH_FLAG  | EVENT_HIGH_FLAG Register (Address = 0x1A) [reset = 0x0]                                                |
| 0x1C      | EVENT_LOW_FLAG   | EVENT_LOW_FLAG Register (Address = 0x1C) [reset = 0x0]                                                 |
| 0x1E      | EVENT_RGN        | EVENT_RGN Register (Address = 0x1E) [reset = 0x0]                                                      |
| 0x20      | HYSTERESIS_CH0   | HYSTERESIS_CH0 Register (Address = 0x20) [reset = 0xF0]                                                |
| 0x21      | HIGH_TH_CH0      | HIGH_TH_CH0 Register (Address = 0x21) [reset = 0xFF]                                                   |
| 0x22      | EVENT_COUNT_CH0  | EVENT_COUNT_CH0 Register (Address = 0x22) [reset = 0x0]                                                |
| 0x23      | LOW_TH_CH0       | LOW_TH_CH0 Register (Address = 0x23) [reset = 0x0]                                                     |
| 0x24      | HYSTERESIS_CH1   | HYSTERESIS_CH1 Register (Address = 0x24) [reset = 0xF0]                                                |
| 0x25      | HIGH_TH_CH1      | HIGH_TH_CH1 Register (Address = 0x25) [reset = 0xFF]                                                   |
| 0x26      | EVENT_COUNT_CH1  | EVENT_COUNT_CH1 Register (Address = 0x26) [reset = 0x0]                                                |
| 0x27      | LOW_TH_CH1       | LOW_TH_CH1 Register (Address = 0x27) [reset = 0x0]                                                     |
| 0x28      | HYSTERESIS_CH2   | HYSTERESIS_CH2 Register (Address = 0x28) [reset = 0xF0]                                                |
| 0x29      | HIGH_TH_CH2      | HIGH_TH_CH2 Register (Address = 0x29) [reset = 0xFF]                                                   |
| 0x2A      | EVENT_COUNT_CH2  | EVENT_COUNT_CH2 Register (Address = 0x2A) [reset = 0x0]                                                |
| 0x2B      | LOW_TH_CH2       | LOW_TH_CH2 Register (Address = 0x2B) [reset = 0x0]                                                     |
| 0x2C      | HYSTERESIS_CH3   | HYSTERESIS_CH3 Register (Address = 0x2C) [reset = 0xF0]                                                |
| 0x2D      | HIGH_TH_CH3      | HIGH_TH_CH3 Register (Address = 0x2D) [reset = 0xFF]                                                   |
| 0x2E      | EVENT_COUNT_CH3  | EVENT_COUNT_CH3 Register (Address = 0x2E) [reset = 0x0]                                                |
| 0x2F      | LOW_TH_CH3       | LOW_TH_CH3 Register (Address = 0x2F) [reset = 0x0]                                                     |
| 0x30      | HYSTERESIS_CH4   | HYSTERESIS_CH4 Register (Address = 0x30) [reset = 0xF0]                                                |
| 0x31      | HIGH_TH_CH4      | HIGH_TH_CH4 Register (Address = 0x31) [reset = 0xFF]                                                   |
| 0x32      | EVENT_COUNT_CH4  | EVENT_COUNT_CH4 Register (Address = 0x32) [reset = 0x0]                                                |
| 0x33      | LOW_TH_CH4       | LOW_TH_CH4 Register (Address = 0x33) [reset = 0x0]                                                     |
| 0x34      | HYSTERESIS_CH5   | HYSTERESIS_CH5 Register (Address = 0x34) [reset = 0xF0] HIGH_TH_CH5 Register (Address = 0x35) [reset = |
| 0x35      | HIGH_TH_CH5      | 0xFF]                                                                                                  |

![Image](output_part1_artifacts\image_000063_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)