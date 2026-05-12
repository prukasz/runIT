![Image](output_part2_artifacts\image_000000_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 7.6.6 MODE Register (Address = 6h) [reset = 00100000h]

MODE is shown in Figure 7-21 and described in Table 7-10.

Return to Summary Table.

MODE controls the operating mode of the TPS55289.

## Figure 7-21. MODE Register

| 7      | 6      | 5      | 4      | 3        | 2        | 1      | 0        |
|--------|--------|--------|--------|----------|----------|--------|----------|
| OE     | FSW    | HICCUP | DISCHG | Reserved | Reserved | FPWM   | Reserved |
| R/W-0b | R/W-0b | R/W-1b | R/W-0b | R/W-0b   | R/W-0b   | R/W-0b | R/W-0b   |

Table 7-10. MODE Register Field Descriptions

|   Bit | Field    | Type   | Reset   | Description                                                                                                                                                                                                                                                                         |
|-------|----------|--------|---------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|     7 | OE       | R/W    | 0b      | Output enable 0b = Output disabled (Default) 1b = Output enable                                                                                                                                                                                                                     |
|     6 | FSWDBL   | R/W    | 0b      | Switching frequency doubling in buck-boost mode TI does not recommend using double frequency function at switching frequency above 1.6 MHz. 0b = Keep the switching frequency unchanged during buck-boost mode (Default) 1b = Double the switching frequency during buck-boost mode |
|     5 | HICCUP   | R/W    | 1b      | Hiccup mode 0b = Disable the hiccup during output short circuit protection. 1b = Enable the hiccup during output short circuit protection (Default)                                                                                                                                 |
|     4 | DISCHG   | R/W    | 0b      | Output discharge 0b = Disabled VOUT discharge when the device is in shutdown mode (Default) 1b = Enable VOUT discharge. VOUT is discharged to ground by an internal 100-mA current sink                                                                                             |
|     3 | RESERVED | R      | 0b      | Reserved                                                                                                                                                                                                                                                                            |
|     2 | RESERVED | R      | 0b      | Reserved                                                                                                                                                                                                                                                                            |
|     1 | FPWM     | R/W    | 0b      | Select operating mode at light load condition 0b = PFM operating mode at light load condition (Default) 1b = FPWM operating mode at light load condition                                                                                                                            |
|     0 | RESERVED | R      | 0b      | Reserved                                                                                                                                                                                                                                                                            |

## 7.6.7 STATUS Register (Address = 7h) [reset = 00000011h]

STATUS is shown in Figure 7-22 and described in Table 7-11.

## Return to Summary Table.

The STATUS register stores the operating status of the TPS55289. When any of the SCP bit, the OCP bit, or the OVP bit are set, and the corresponding mask bit in register 05h is set as well, the FB/INT ̅  pin outputs low logic level to indicate the situation. Reading register 07h clears the SCP bit, OCP bit, and OVP bit. After the SCP bit, OCP bit, or OVP bit is set, it does not reset until the register is read. If the situation still exists, the corresponding bit is set again.

## Figure 7-22. STATUS Register

| 7    | 6    | 5    | 4        | 3        | 2        | 1      |
|------|------|------|----------|----------|----------|--------|
| SCP  | OCP  | OVP  | Reserved | Reserved | Reserved | STATUS |
| R-0b | R-0b | R-0b | R/W-0b   | R/W-0b   | R/W-0b   | R-11b  |

Table 7-11. STATUS Register Field Descriptions

| Bit   | Field    | Type   | Reset   | Description                                                                                                                                                      |
|-------|----------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7     | SCP      | R      | 0b      | Short circuit protection 0b = No short circuit 1b = Short circuit happens. Does not reset until it is read.                                                      |
| 6     | OCP      | R      | 0b      | Overcurrent protection 0b = No output overcurrent 1b = Output current hits the current limit sensed at the ISP and the ISN pin. Does not reset until it is read. |
| 5     | OVP      | R      | 0b      | Overvoltage protection 0b = No OVP 1b = Output voltage exceeds the OVP threshold. Does not reset until it is read.                                               |
| 4     | RESERVED | R      | 0b      | Reserved                                                                                                                                                         |
| 3     | RESERVED | R      | 0b      | Reserved                                                                                                                                                         |
| 2     | RESERVED | R      | 0b      | Reserved                                                                                                                                                         |
| 1-0   | STATUS   | R      | 11b     | Operating status 00b = Boost 01b = Buck 10b = Buck-Boost 11b = Reserved                                                                                          |

![Image](output_part2_artifacts\image_000001_2c3de8d38d7127fc9191723744751ba19a2abda9341507814fc2a5026cf43b9b.png)

![Image](output_part2_artifacts\image_000002_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 8 Application and Implementation

## Note

Information  in  the  following  applications  sections  is  not  part  of  the  TI  component  specification, and  TI does  not  warrant its accuracy  or completeness.  TI's customers  are  responsible  for determining suitability of components for their purposes, as well as validating and testing their design implementation to confirm system functionality.

## 8.1 Application Information

The TPS55289 can operate over a wide range of 3.0-V to 30-V input voltage and output 0.8 V to 22 V. The device can transition among buck mode, buck-boost mode, and boost mode smoothly according to the input voltage and the setting output voltage. The TPS55289 operates in buck mode when the input voltage is greater than the output voltage and in boost mode when the input voltage is less than the output voltage. When the input voltage is close to the output voltage, the TPS55289 operates in one-cycle buck and one-cycle boost mode alternately. The switching frequency is set by an external resistor. To reduce the switching power loss in high power conditions, set the switching frequency below 500 kHz. If a system requires higher switching frequency above 500 kHz, set the lower switch current limit for better thermal performance.

## 8.2 Typical Application

The  TPS55289  provides  a  small  size  solution  for  USB  PD  power  supply  application  with  the  input  voltage ranging from 9 V to 30 V.

Figure 8-1. USB PD Power Supply With 9-V to 30-V Input Voltage

![Image](output_part2_artifacts\image_000003_b1ca83fb28afdcfb4b94a9dd7b0cb4aed35b11e8542ab1181f44d30c822b7cdf.png)

## 8.2.1 Design Requirements

The design parameters are listed in Table 8-1:

Table 8-1. Design Parameters

| Parameters                   | Values        |
|------------------------------|---------------|
| Input voltage                | 9 V to 30 V   |
| Output voltage               | 3.3 V to 20 V |
| Output current limit         | 2.25 A        |
| Output voltage ripple        | ±50 mV        |
| Operating mode at light load | FPWM          |

## 8.2.2 Detailed Design Procedure

## 8.2.2.1 Switching Frequency

The switching frequency of the TPS55289 is set by a resistor at the FSW pin. Use Equation 3 to calculate the resistance for the desired frequency. To reduce the switching power loss with such a high current application, a 1% standard resistor of 49.9 kΩ is selected for 400-kHz switching frequency for this application.

## 8.2.2.2 Output Voltage Setting

The  TPS55289  has  I 2 C  interface  to  set  the  internal  reference  voltage.  A  microcontroller  can  easily  set  the desired output voltage by writing the proper data into the reference voltage registers through I 2 C bus.

## 8.2.2.3 Inductor Selection

Since  the  selection  of  the  inductor  affects  steady  state  operation,  transient  behavior,  and  loop  stability,  the inductor  is  the  most  important  component  in  power  regulator  design.  There  are  three  important  inductor specifications: inductance, saturation current, and DC resistance.

The TPS55289 is designed to work with inductor values between 1 µH and 10 µH. The inductor selection is based on consideration of both buck and boost modes of operation.

For  buck  mode,  the  inductor  selection  is  based  on  limiting  the  peak-to-peak  current  ripple  to  the  maximum inductor  current  at  the  maximum  input  voltage.  In  CCM,  Equation  9  shows  the  relationship  between  the inductance and the inductor ripple current.

![Image](output_part2_artifacts\image_000004_e293069f1a81bdea8b3b24a8e3bd44cd7a65fd70dbeb4871c28a610d366a5836.png)

<!-- formula-not-decoded -->

## where

- VIN(MAX)  is the maximum input voltage.
- VOUT is the output voltage.
- ΔIL(P-P) is the peak to peak ripple current of the inductor.
- f SW is the switching frequency.

For  a  certain  inductor,  the  inductor  ripple  current  achieves  maximum  value  when  V OUT   equals  half  of the  maximum  input  voltage.  Choosing  higher  inductance  gets  smaller  inductor  current  ripple  while  smaller inductance gets larger inductor current ripple.

For  boost  mode,  the  inductor  selection  is  based  on  limiting  the  peak-to-peak  current  ripple  to  the  maximum inductor  current  at  the  maximum  output  voltage.  In  CCM,  Equation  10  shows  the  relationship  between  the inductance and the inductor ripple current.

![Image](output_part2_artifacts\image_000005_a7d3f03a1c3c0d6cdc26a79104941f577ce51bcf4ee6e857851c2bc2ea06ad84.png)

<!-- formula-not-decoded -->

where

![Image](output_part2_artifacts\image_000006_c978503d5215fb8ff7bbdae1839504348cf7d3f296afeaa774c0f997344d28a1.png)

![Image](output_part2_artifacts\image_000007_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## [www.ti.com](https://www.ti.com/)

- VIN is the input voltage.
- VOUT(MAX) is the maximum output voltage.
- ΔIL(P-P) is the peak-to-peak ripple current of the inductor.
- f SW is the switching frequency.

For  a  certain  inductor,  the  inductor  ripple  current  achieves  maximum  value  when  V IN equals  to  the  half  of the  maximum  output  voltage.  Choosing  higher  inductance  gets  smaller  inductor  current  ripple  while  smaller inductance gets larger inductor current ripple.

For  this  application  example,  a  4.7-µH  inductor  is  selected,  which  produces  approximate  maximum  inductor current  ripple  of  50%  of  the  highest  average  inductor  current  in  buck  mode  and  50%  of  the  highest  average inductor current in boost mode.

In buck mode, the inductor DC current equals to the output current. In boost mode, the inductor DC current can be calculated with Equation 11.

<!-- formula-not-decoded -->

## where

- VOUT is the output voltage.
- I OUT is the output current.
- VIN is the input voltage.
- η is the power conversion efficiency.

For  a  given  maximum  output  current  of  the  TPS55289,  the  maximum  inductor  DC  current  happens  at  the minimum  input  voltage  and  maximum  output  voltage.  Set  the  inductor  current  limit  of  the  TPS55289  higher than the calculated maximum inductor DC current to make sure the TPS55289 has the desired output current capability.

In boost mode, the inductor ripple current is calculated with Equation 12.

<!-- formula-not-decoded -->

## where

- ΔIL(P-P) is the inductor ripple current.
- L is the inductor value.
- f SW is the switching frequency.
- VOUT is the output voltage.
- VIN is the input voltage.

Therefore, the inductor peak current is calculated with Equation 13.

<!-- formula-not-decoded -->

Normally, it is advisable to work with an inductor peak-to-peak current of less than 40% of the average inductor current  for  maximum  output  current.  A  smaller  ripple  from  a  larger  valued  inductor  reduces  the  magnetic hysteresis losses in the inductor and EMI, but in the same way, load transient response time is increased. The selected inductor must have higher saturation current than the calculated peak current.

The  conversion  efficiency  is  dependent  on  the  resistance  of  its  current  path.  The  switching  loss  associated with  the  switching  MOSFETs,  and  the  inductor  core  loss.  Therefore,  the  overall  efficiency  is  affected  by  the inductor DC resistance (DCR), equivalent series resistance (ESR) at the switching frequency, and the core loss. Table  8-2  lists  recommended  inductors  for  the  TPS55289.  In  this  application  example,  the  Coilcraft  inductor XAL7070-472 is selected for its small size, high saturation current, and small DCR.

![Image](output_part2_artifacts\image_000008_bee02fdcc2ff86d2e02fdf53c075f520f6e5cf616ccda5178bbe87bb52a61c62.png)

Table 8-2. Recommended Inductors

| Part Number        |   L (µH) |   DCR (Maximum) (mΩ) | Saturation Current/Heat Rating Current (A)   | Size (L×W×Hmm)    | Vendor (1)   |
|--------------------|----------|----------------------|----------------------------------------------|-------------------|--------------|
| XAL7070-472ME      |      4.7 |                 14.3 | 15.2/10.5                                    | 7.5 × 7.2 × 7.0   | Coilcraft    |
| VCHA085D-4R7MS6    |      4.7 |                 15.6 | 16.0/8.8                                     | 8.7 × 8.2 × 5.2   | Cyntec       |
| IHLP4040DZER4R7M01 |      4.7 |                 16.5 | 17/9.5                                       | 10.2 × 10.2 × 4.0 | Vishay       |

(1) See the Third-party Products disclaimer.

## 8.2.2.4 Input Capacitor

In buck mode, the input capacitor supplies high ripple current. The RMS current in the input capacitors is given by Equation 14.

<!-- formula-not-decoded -->

## where

- I CIN(RMS)  is the RMS current through the input capacitor.
- I OUT is the output current.

The maximum RMS current occurs at the output voltage is half of the input voltage, which gives I CIN(RMS)  = I OUT  / 2. Ceramic capacitors are recommended for their low ESR and high ripple current capability. A total of 20 µF effective capacitance is a good starting point for this application. Add a 0.1-µF/0402 package ceramic capacitor and place it close to VIN pin and GND pin to suppress high frequency noise.

## 8.2.2.5 Output Capacitor

In  boost  mode,  the  output  capacitor  conducts  high  ripple  current.  The  output  capacitor  RMS  ripple  current  is given  by  Equation  15,  where  the  minimum  input  voltage  and  the  maximum  output  voltage  correspond  to  the maximum capacitor current.

<!-- formula-not-decoded -->

## where

- I COUT(RMS) is the RMS current through the output capacitor.
- I OUT is the output current.

In this example, the maximum output ripple RMS current is 2.5 A.

The ESR of the output capacitor causes an output voltage ripple given by Equation 16 in boost mode.

<!-- formula-not-decoded -->

## where

- RCOUT is the ESR of the output capacitance.

The capacitance also causes a capacitive output voltage ripple given by Equation 17 in boost mode. When input voltage  reaches  the  minimum  value  and  the  output  voltage  reaches  the  maximum  value,  there  is  the  largest output voltage ripple caused by the capacitance.

![Image](output_part2_artifacts\image_000009_335309de30b7244b510ca8db4b87748f85fa45750ec08367de6aab160e1f5892.png)

![Image](output_part2_artifacts\image_000010_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

<!-- formula-not-decoded -->

Typically,  a  combination of ceramic capacitors and bulk electrolytic capacitors is needed to provide low ESR, high ripple current, and small output voltage ripple. From the required output voltage ripple, use Equation 16 and Equation 17 to calculate the minimum required effective capacitance of the COUT.

Add a 0.1-μF/0402 package ceramic capacitor and place it close to VOUT pin and GND pin to suppress high frequency noise.

## 8.2.2.6 Output Current Limit

The output current limit is implemented by placing a current sense resistor between the ISP and ISN pins along with setting a limit voltage between the ISP pin and the ISN pin through register 02h. The maximum value of the limit voltage between the ISP and ISN pins is 63.5 mV. The default limit voltage is 50 mV. The current sense resistor  between  the  ISP  and  ISN  pins  should  be  selected  to  ensure  that  the  output  current  limit  is  set  high enough for output. The output current limit setting resistor is given by Equation 18.

<!-- formula-not-decoded -->

## where

- VSNS is the current limit setting voltage between the ISP and ISN pins.
- I OUT\_LIMIT  is the desired output current limit.

Because the  power  dissipation  is  large,  make  sure  the  current  sense  resistor  has  enough  power  dissipation capability with a large package.

## 8.2.2.7 Loop Stability

The  TPS55289  uses  average  current  control  scheme.  The  inner  current  loop  uses  internal  compensation and  requires  the  inductor  value  must  be  larger  than  1.2/f SW.  The  outer  voltage  loop  requires  an  external compensation. The COMP pin is the output of the internal  voltage  error  amplifier.  An  external  compensation network comprised of resistor and ceramic capacitors is connected to the COMP pin.

The TPS55289 operates in buck mode or boost mode. Therefore, both buck and boost operating modes require loop compensations. The restrictive one of both compensations is selected as the overall compensation from a loop stability point of view. Typically for a converter designed either work in buck mode or boost mode, the boost mode compensation design is more restrictive due to the presence of a right half plane zero (RHPZ).

The power stage in boost mode can be modeled by Equation 18.

<!-- formula-not-decoded -->

## where

- RLOAD is the output load resistance.
- D is the switching duty cycle in boost mode.
- RSENSE is the equivalent internal current sense resistor, which is 0.055 Ω.

The  power  stage  has  two  zeros  and  one  pole  generated  by  the  output  capacitor  and  load  resistance.  Use Equation 20 to Equation 22 to calculate them.

![Image](output_part2_artifacts\image_000011_bee02fdcc2ff86d2e02fdf53c075f520f6e5cf616ccda5178bbe87bb52a61c62.png)

![Image](output_part2_artifacts\image_000012_c978503d5215fb8ff7bbdae1839504348cf7d3f296afeaa774c0f997344d28a1.png)

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

The internal transconductance amplifier together with the compensation network at the COMP pin constitutes the control portion of the loop. The transfer function of the control portion is shown by Equation 23.

<!-- formula-not-decoded -->

## where

- GEA is the transconductance of the error amplifier.
- REA is the output resistance of the error amplifier.
- VREF is the reference voltage input to the error amplifier.
- VOUT is the output voltage.
- fCOMP1 and fCOMP2 are the pole's frequency of the compensation network.
- fCOMZ is the zero's frequency of the compensation network.

The total  open-loop  gain  is  the  product  of  G PS(s)  and  G C(s).  The  next  step  is  to  choose  the  loop  crossover frequency, f C ,  at  which the total open-loop gain is 1, namely 0 dB. The higher in frequency that the loop gain stays above 0 dB before crossing over, the faster the loop response. It is generally accepted that the loop gain cross over 0 dB at the frequency no higher than the lower of either 1/10 of the switching frequency, f SW , or 1/5 of the RHPZ frequency, fRHPZ.

Then, set the value of R C , CC, and CP by Equation 24 to Equation 26.

<!-- formula-not-decoded -->

where

- f C is the selected crossover frequency.

<!-- formula-not-decoded -->

<!-- formula-not-decoded -->

If the calculated C P is less than 10 pF, it can be left open.

Designing the loop for greater than 45° of phase margin and greater than 10-dB gain margin eliminates output voltage ringing during the line and load transient.

![Image](output_part2_artifacts\image_000013_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 8.2.3 Application Curves

![Image](output_part2_artifacts\image_000014_a1826686bb07650c6c490f6e0964d727dd440d1ca5a9761426372b4a1c3b1de9.png)

![Image](output_part2_artifacts\image_000015_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

![Image](output_part2_artifacts\image_000016_8805e19efdcb478d3e11a176d1722c174d3e54ca95b2861ecc0eb010d150f0a9.png)

![Image](output_part2_artifacts\image_000017_22300a3d2139657d2bb4b69b2ec0ee7245f7a6451118c80e072cf2a04de7bb7e.png)

[www.ti.com](https://www.ti.com/)

## 9 Power Supply Recommendations

The device is designed to operate from an input voltage supply range between 3.0 V to 30 V. This input supply must  be  well  regulated.  If  the  input  supply  is  located  more  than  a  few  inches  from  the  converter,  additional bulk capacitance can be required in addition to the ceramic bypass capacitors. A typical choice is an aluminum electrolytic capacitor with a value of 100 μF.

## 10 Layout

## 10.1 Layout Guidelines

As  for  all  switching  power  supplies,  especially  those  running  at  high  switching  frequency  and  high  currents, layout is an important design step. If layout is not carefully done, the regulator can suffer from instability and noise problems.

- Place the 0.1-μF small package (0402) ceramic capacitors close to the VIN/VOUT pins to minimize high frequency current loops. This improves the radiation of high-frequency noise (EMI) and efficiency.
- Use multiple GND vias near PGND pin to connect the PGND to the internal ground plane. This also improves thermal performance.
- Minimize the SW1 and SW2 loop areas as these are high dv/dt nodes. Use a ground plane under the switching regulator to minimize interplane coupling.
- Use Kelvin connections to RSENSE for the current sense signals ISP and ISN and run lines in parallel from the RSENSE terminals to the IC pins. Place the filter capacitor for the current sense signal as close to the IC pins as possible.
- Place the BOOT1 bootstrap capacitor close to the IC and connect directly to the BOOT1 to SW1 pins. Place the BOOT2 bootstrap capacitor close to the IC and connect directly to the BOOT2 and SW2 pins.
- Place the VCC capacitor close to the IC with wide and short trace. The GND terminal of the VCC capacitor should be directly connected with PGND plane through three to four vias.
- Isolate the power ground from the analog ground. The PGND plane and AGND plane are connected at the terminal of the VCC capacitor. Thus the noise caused by the MOSFET driver and parasitic inductance does not interface with the AGND and internal control circuit.
- Place the compensation components as close to the COMP pin as possible. Keep the compensation components, feedback components, and other sensitive analog circuitry far away from the power components, switching nodes SW1 and SW2, and high-current trace to prevent noise coupling into the analog signals.
- To improve thermal performance, it is recommended to use thermal vias beneath the TPS55289 connecting the VIN pin to a large VIN area, and the VOUT pin to a large VOUT area separately.

## 10.2 Layout Example

![Image](output_part2_artifacts\image_000018_e7c39014400543607913c4c271836faa78ffe03886368e31e580d4625f1e6982.png)

The first inner layer is the PGND plane

Figure 10-1. Layout Example

![Image](output_part2_artifacts\image_000019_2932a743510cdd1a5789205a19fa0d5e886ba4b08a3b4501d89491c8aae142d9.png)

![Image](output_part2_artifacts\image_000020_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## 11 Device and Documentation Support

## 11.1 Device Support

## 11.1.1 Third-Party Products Disclaimer

TI'S PUBLICATION OF INFORMATION REGARDING THIRD-PARTY PRODUCTS OR SERVICES DOES NOT CONSTITUTE AN ENDORSEMENT REGARDING THE SUITABILITY OF SUCH PRODUCTS OR SERVICES OR A WARRANTY, REPRESENTATION OR ENDORSEMENT OF SUCH PRODUCTS OR SERVICES, EITHER ALONE OR IN COMBINATION WITH ANY TI PRODUCT OR SERVICE.

## 11.2 Receiving Notification of Documentation Updates

To  receive  notification  of  documentation  updates,  navigate  to  the  device  product  folder  on  ti.com.  Click  on Subscribe to updates to register and receive a weekly digest of any product information that has changed. For change details, review the revision history included in any revised document.

## 11.3 Support Resources

TI E2E ™  support forums are an engineer's go-to source for fast, verified answers and design help - straight from the experts. Search existing answers or ask your own question to get the quick design help you need.

Linked content is provided "AS IS" by the respective contributors. They do not constitute TI specifications and do not necessarily reflect TI's views; see TI's Terms of Use.

## 11.4 Trademarks

HotRod ™  and TI E2E ™  are trademarks of Texas Instruments. All trademarks are the property of their respective owners.

## 11.5 Electrostatic Discharge Caution

![Image](output_part2_artifacts\image_000021_34ebdc140d275d24df38674a74611f5ecfb23a4a996b6bb9e883dae6dccbb19f.png)

This integrated circuit can be damaged by ESD. Texas Instruments recommends that all integrated circuits be handled with appropriate precautions. Failure to observe proper handling and installation procedures can cause damage.

ESD damage can range from subtle performance degradation to complete device failure. Precision integrated circuits may be more susceptible to damage because very small parametric changes could cause the device not to meet its published specifications.

## 11.6 Glossary

TI Glossary This glossary lists and explains terms, acronyms, and definitions.

## 12 Mechanical, Packaging, and Orderable Information

The  following  pages  include  mechanical,  packaging,  and  orderable  information.  This  information  is  the  most current data available for the designated devices. This data is subject to change without notice and revision of this document. For browser-based versions of this data sheet, refer to the left-hand navigation.

## RYQ0021A

![Image](output_part2_artifacts\image_000022_56475fa5d72d0bff6b51993d60db1daf5ef4c0f4dda5856d47f29801ff93d69d.png)

SCALE  3.0

![Image](output_part2_artifacts\image_000023_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

## PACKAGE OUTLINE

## VQFN - 1.0 mm max height

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000024_1ff39d7c2827dc87888d552b7b0f1c35706530d29a770a99e1cd330f28d0dc36.png)

## NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. The package thermal pad must be soldered to the printed circuit board for thermal and mechanical performance.

![Image](output_part2_artifacts\image_000025_5f154a9d19ec9c0769ce8abacefe043b99093b5edc198f39af55a4a043921c9c.png)

www.ti.com

![Image](output_part2_artifacts\image_000026_8ef6ab5d1d7f4e5497c05279fca2d4c1617401afad5b848926a2d4d413ea1401.png)

## RYQ0021A

## EXAMPLE BOARD LAYOUT

## VQFN - 1.0 mm max height

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000027_ef80519d53451ae4eeacec36b53b820bfae144e6f98de3bab5c6eb55bdaaf0ab.png)

NOTES: (continued)

4. This package is designed to be soldered to a thermal pad on the board. For more information, see Texas Instruments literature number SLUA271 (www.ti.com/lit/slua271).

5. Vias are optional depending on application, refer to device data sheet. If any vias are implemented, refer to their locations shown on this view. It is recommended that vias under paste be filled, plugged or tented.

![Image](output_part2_artifacts\image_000028_778acba18f1d0cf2c94e43aeb6fc4b9190a711651ac364ae1c4e5938668ace0c.png)

www.ti.com

![Image](output_part2_artifacts\image_000029_4361a09dfee51443e226fae8c4c2ddee42548470667b045bb18cc73bc2840869.png)

RYQ0021A

![Image](output_part2_artifacts\image_000030_f77bdd756b8bda7889c248e07cf3abe12dda806caf23a3a2f7249d3948b6b84b.png)

## EXAMPLE STENCIL DESIGN

## VQFN - 1.0 mm max height

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000031_3057cd83e5ba3b3b08659315d1795491797d5f3f3a5ef57a7e20659bb9dcb257.png)

NOTES: (continued)

6. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate design recommendations.

![Image](output_part2_artifacts\image_000032_778acba18f1d0cf2c94e43aeb6fc4b9190a711651ac364ae1c4e5938668ace0c.png)

www.ti.com

![Image](output_part2_artifacts\image_000033_aec33460263832ad92b1995b813f2a7af9a0df7a177eada2ab8d0069424ff22c.png)

www.ti.com

## PACKAGING INFORMATION

| Orderable part number   | Status (1)   | Material type (2)   | Package &#124; Pins     | Package qty &#124; Carrier   | RoHS (3)   | Lead finish/ Ball material (4)   | MSL rating/ Peak reflow (5)   | Op temp (°C)   | Part marking (6)   |
|-------------------------|--------------|---------------------|-------------------------|------------------------------|------------|----------------------------------|-------------------------------|----------------|--------------------|
| TPS55289RYQR            | Active       | Production          | VQFN-HR (RYQ) &#124; 21 | 3000 &#124; LARGE T&R        | Yes        | SN                               | Level-2-260C-1 YEAR           | -40 to 125     | S55289             |
| TPS55289RYQR.A          | Active       | Production          | VQFN-HR (RYQ) &#124; 21 | 3000 &#124; LARGE T&R        | Yes        | SN                               | Level-2-260C-1 YEAR           | -40 to 125     | S55289             |

- (4) Lead finish/Ball material: Parts may have multiple material finish options. Finish options are separated by a vertical ruled line. Lead finish/Ball material values may wrap to two lines if the finish value exceeds the maximum column width.

(5) MSL rating/Peak reflow: The moisture sensitivity level ratings and peak solder (reflow) temperatures. In the event that a part has multiple moisture sensitivity ratings, only the lowest level per JEDEC standards is shown. Refer to the shipping label for the actual reflow temperature that will be used to mount the part to the printed circuit board.

- (6) Part marking: There may be an additional marking, which relates to the logo, the lot trace code information, or the environmental category of the part.

Multiple part markings will be inside parentheses. Only one part marking contained in parentheses and separated by a "~" will appear on a part. If a line is indented then it is a continuation of the previous line and the two combined represent the entire part marking for that device.

Important Information and Disclaimer: The information provided on this page represents TI's knowledge and belief as of the date that it is provided. TI bases its knowledge and belief on information provided by third parties, and makes no representation or warranty as to the accuracy of such information. Efforts are underway to better integrate information from third parties. TI has taken and continues to take reasonable steps to provide representative and accurate information but may not have conducted destructive testing or chemical analysis on incoming materials and chemicals. TI and TI suppliers consider certain information to be proprietary, and thus CAS numbers and other limited information may not be available for release.

In no event shall TI's liability arising out of such information exceed the total purchase price of the TI part(s) at issue in this document sold by TI to Customer on an annual basis.

## OTHER QUALIFIED VERSIONS OF TPS55289 :

- Automotive : TPS55289-Q1

## PACKAGE OPTION ADDENDUM

9-Nov-2025

![Image](output_part2_artifacts\image_000034_aec33460263832ad92b1995b813f2a7af9a0df7a177eada2ab8d0069424ff22c.png)

www.ti.com

NOTE: Qualified Version Definitions:

• Automotive - Q100 devices qualified for high-reliability automotive applications targeting zero defects

## PACKAGE OPTION ADDENDUM

9-Nov-2025

![Image](output_part2_artifacts\image_000035_5d4ed78c8362cfa66f79d55d3ac80225c236bdd95695b3a42030daba7904ac99.png)

www.ti.com

## TAPE AND REEL INFORMATION

![Image](output_part2_artifacts\image_000036_7916e760e90f26e4f04a586b6bf543e28e99fceab9db9b8da4c50bcc5b5eb1c8.png)

## QUADRANT ASSIGNMENTS FOR PIN 1 ORIENTATION IN TAPE

![Image](output_part2_artifacts\image_000037_86c531b4779dc5f6d3c7fa31ae4c08507fee8a20fcfadb36f3f6c9a19cdf1393.png)

| Device       | Package Type   | Package Drawing   |   Pins |   SPQ |   Reel Diameter (mm) |   Reel Width W1 (mm) |   A0 (mm) |   B0 (mm) |   K0 (mm) |   P1 (mm) |   W (mm) | Pin1 Quadrant   |
|--------------|----------------|-------------------|--------|-------|----------------------|----------------------|-----------|-----------|-----------|-----------|----------|-----------------|
| TPS55289RYQR | VQFN- HR       | RYQ               |     21 |  3000 |                330.0 |                 12.4 |       3.2 |      5.25 |       1.2 |       8.0 |     12.0 | Q2              |

## *All dimensions are nominal

## PACKAGE MATERIALS INFORMATION

23-Sep-2022

![Image](output_part2_artifacts\image_000038_5d4ed78c8362cfa66f79d55d3ac80225c236bdd95695b3a42030daba7904ac99.png)

www.ti.com

![Image](output_part2_artifacts\image_000039_8820c93a2294a34101c87dbe60bab20b550570fa4048f0dda10aa7134fe43cf7.png)

*All dimensions are nominal

| Device       | Package Type   | Package Drawing   |   Pins |   SPQ |   Length (mm) |   Width (mm) |   Height (mm) |
|--------------|----------------|-------------------|--------|-------|---------------|--------------|---------------|
| TPS55289RYQR | VQFN-HR        | RYQ               |     21 |  3000 |         367.0 |        367.0 |          35.0 |

## PACKAGE MATERIALS INFORMATION

23-Sep-2022

5 x 3, 0.5 mm pitch

## GENERIC PACKAGE VIEW

PLASTIC QUAD FLATPACK - NO LEAD

This image is a representation of the package family, actual package may vary. Refer to the product data sheet for package details.

![Image](output_part2_artifacts\image_000040_e8df3b00e81ee03bb898d409625ad56626598a26e9aacfdeb0f01cf5bcb3a0a5.png)

![Image](output_part2_artifacts\image_000041_ef7530faca57e89fbdbbf8dcf1617a9f3fcca57c748c4776622b4be9afbd973c.png)

![Image](output_part2_artifacts\image_000042_4d53f5f5286a08215e28e24620f576a918ab730fa2c1c7145df93d8882e8a5a7.png)

SCALE  3.0

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000043_7e679bd491e99bc4e47d9dfb3ad9d0694ac4dc3364801047b59517441533ff82.png)

## NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. The package thermal pad must be soldered to the printed circuit board for thermal and mechanical performance.

![Image](output_part2_artifacts\image_000044_e49726711684173967c0255df6a285f7d622595743f2aa3730d58630b945f106.png)

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000045_eeeb64799d8d74afe72b3c53f43c021af0b4c8fee7622a06c7075eb94a9ce200.png)

NOTES: (continued)

4. This package is designed to be soldered to a thermal pad on the board. For more information, see Texas Instruments literature number SLUA271 (www.ti.com/lit/slua271).
5. Vias are optional depending on application, refer to device data sheet. If any vias are implemented, refer to their locations shown on this view. It is recommended that vias under paste be filled, plugged or tented.

![Image](output_part2_artifacts\image_000046_e49726711684173967c0255df6a285f7d622595743f2aa3730d58630b945f106.png)

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part2_artifacts\image_000047_332eb04246a21d8dfb312b58f270c29346c35bfff9f03065ea7de2fd365fcb76.png)

NOTES: (continued)

6. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate design recommendations.

![Image](output_part2_artifacts\image_000048_e49726711684173967c0255df6a285f7d622595743f2aa3730d58630b945f106.png)

## IMPORTANT NOTICE AND DISCLAIMER

TI PROVIDES TECHNICAL AND RELIABILITY DATA (INCLUDING DATASHEETS), DESIGN RESOURCES (INCLUDING REFERENCE DESIGNS), APPLICATION OR OTHER DESIGN ADVICE, WEB TOOLS, SAFETY INFORMATION, AND OTHER RESOURCES 'AS IS' AND WITH ALL FAULTS, AND DISCLAIMS ALL WARRANTIES, EXPRESS AND IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY RIGHTS.

These resources are intended for skilled developers designing with TI products. You are solely responsible for (1) selecting the appropriate TI products for your application, (2) designing, validating and testing your application, and (3) ensuring your application meets applicable standards, and any other safety, security, regulatory or other requirements.

These resources are subject to change without notice. TI grants you permission to use these resources only for development of an application that uses the TI products described in the resource. Other reproduction and display of these resources is prohibited. No license is granted to any other TI intellectual property right or to any third party intellectual property right. TI disclaims responsibility for, and you fully indemnify TI and its representatives against any claims, damages, costs, losses, and liabilities arising out of your use of these resources.

TI's products are provided subject to TI's Terms of Sale, TI's General Quality Guidelines, or other applicable terms available either on ti.com or provided in conjunction with such TI products. TI's provision of these resources does not expand or otherwise alter TI's applicable warranties or warranty disclaimers for TI products. Unless TI explicitly designates a product as custom or customer-specified, TI products are standard, catalog, general purpose devices.

TI objects to and rejects any additional or different terms you may propose.

IMPORTANT NOTICE

Copyright © 2025, Texas Instruments Incorporated Last updated 10/2025