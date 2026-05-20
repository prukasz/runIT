![Image](output_part3_artifacts\image_000000_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Table 93. MIN\_CH6\_LSB Register Field Descriptions

| Bit   | Field                   | Type   | Reset     | Description                                                                                                                                     |
|-------|-------------------------|--------|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | MIN_VALUE_CH6_LSB[7: 0] | R      | 11111111b | Minimum code recorded on the analog input channel from the last time this register was read. Reading the register will reset the value to 0xFF. |

## 8.6.83 MIN\_CH6\_MSB Register (Address = 0x8D) [reset = 0xFF]

MIN\_CH6\_MSB is shown in Figure 120 and described in Table 94.

Return to the Summary Table.

Figure 120. MIN\_CH6\_MSB Register

| 7                      | 6   | 5   | 4   | 3           | 2   | 1   | 0   |
|------------------------|-----|-----|-----|-------------|-----|-----|-----|
| MIN_VALUE_CH6_MSB[7:0] |     |     |     |             |     |     |     |
|                        |     |     |     | R-11111111b |     |     |     |

## Table 94. MIN\_CH6\_MSB Register Field Descriptions

| Bit   | Field                   | Type   | Reset     | Description                                                                                                                                     |
|-------|-------------------------|--------|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | MIN_VALUE_CH6_MSB[7 :0] | R      | 11111111b | Minimum code recorded on the analog input channel from the last time this register was read. Reading the register will reset the value to 0xFF. |

## 8.6.84 MIN\_CH7\_LSB Register (Address = 0x8E) [reset = 0xFF]

MIN\_CH7\_LSB is shown in Figure 121 and described in Table 95.

Return to the Summary Table.

Figure 121. MIN\_CH7\_LSB Register

| 7                      |                        |
|------------------------|------------------------|
| MIN_VALUE_CH7_LSB[7:0] | MIN_VALUE_CH7_LSB[7:0] |
| R-11111111b            | R-11111111b            |

## Table 95. MIN\_CH7\_LSB Register Field Descriptions

| Bit   | Field                   | Type   | Reset     | Description                                                                                                                                     |
|-------|-------------------------|--------|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | MIN_VALUE_CH7_LSB[7: 0] | R      | 11111111b | Minimum code recorded on the analog input channel from the last time this register was read. Reading the register will reset the value to 0xFF. |

## 8.6.85 MIN\_CH7\_MSB Register (Address = 0x8F) [reset = 0xFF]

MIN\_CH7\_MSB is shown in Figure 122 and described in Table 96.

Return to the Summary Table.

Figure 122. MIN\_CH7\_MSB Register

| 7                      | 5 4                    |
|------------------------|------------------------|
| MIN_VALUE_CH7_MSB[7:0] | MIN_VALUE_CH7_MSB[7:0] |
| R-11111111b            | R-11111111b            |

## Table 96. MIN\_CH7\_MSB Register Field Descriptions

| Bit   | Field                   | Type   | Reset     | Description                                                                                                                                     |
|-------|-------------------------|--------|-----------|-------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | MIN_VALUE_CH7_MSB[7 :0] | R      | 11111111b | Minimum code recorded on the analog input channel from the last time this register was read. Reading the register will reset the value to 0xFF. |

## 8.6.86 RECENT\_CH0\_LSB Register (Address = 0xA0) [reset = 0x0]

RECENT\_CH0\_LSB is shown in Figure 123 and described in Table 97.

Return to the Summary Table.

## Figure 123. RECENT\_CH0\_LSB Register

| 7                       | 4                       |
|-------------------------|-------------------------|
| LAST_VALUE_CH0_LSB[7:0] | LAST_VALUE_CH0_LSB[7:0] |
| R-0b                    | R-0b                    |

## Table 97. RECENT\_CH0\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH0_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.87 RECENT\_CH0\_MSB Register (Address = 0xA1) [reset = 0x0]

RECENT\_CH0\_MSB is shown in Figure 124 and described in Table 98.

Return to the Summary Table.

Figure 124. RECENT\_CH0\_MSB Register

|                         | 5 4 3 2                 |
|-------------------------|-------------------------|
| LAST_VALUE_CH0_MSB[7:0] | LAST_VALUE_CH0_MSB[7:0] |
| R-0b                    | R-0b                    |

## Table 98. RECENT\_CH0\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH0_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.88 RECENT\_CH1\_LSB Register (Address = 0xA2) [reset = 0x0]

RECENT\_CH1\_LSB is shown in Figure 125 and described in Table 99.

Return to the Summary Table.

Figure 125.

RECENT\_CH1\_LSB Register

| 7 6 5 4 3               |
|-------------------------|
| LAST_VALUE_CH1_LSB[7:0] |
| R-0b                    |

## Table 99. RECENT\_CH1\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH1_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.89 RECENT\_CH1\_MSB Register (Address = 0xA3) [reset = 0x0]

RECENT\_CH1\_MSB is shown in Figure 126 and described in Table 100.

Return to the Summary Table.

![Image](output_part3_artifacts\image_000001_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000002_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Figure 126. RECENT\_CH1\_MSB Register

| 7 6 5 4 3               | 2   | 1   | 0   |
|-------------------------|-----|-----|-----|
| LAST_VALUE_CH1_MSB[7:0] |     |     |     |

## Table 100. RECENT\_CH1\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH1_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.90 RECENT\_CH2\_LSB Register (Address = 0xA4) [reset = 0x0]

RECENT\_CH2\_LSB is shown in Figure 127 and described in Table 101.

Return to the Summary Table.

## Figure 127. RECENT\_CH2\_LSB Register

| 7                       | 4                       |                         |                         |
|-------------------------|-------------------------|-------------------------|-------------------------|
| LAST_VALUE_CH2_LSB[7:0] | LAST_VALUE_CH2_LSB[7:0] | LAST_VALUE_CH2_LSB[7:0] | LAST_VALUE_CH2_LSB[7:0] |
| R-0b                    | R-0b                    | R-0b                    | R-0b                    |

## Table 101. RECENT\_CH2\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH2_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.91 RECENT\_CH2\_MSB Register (Address = 0xA5) [reset = 0x0]

RECENT\_CH2\_MSB is shown in Figure 128 and described in Table 102.

Return to the Summary Table.

## Figure 128. RECENT\_CH2\_MSB Register

| 7                       | 6                       | 5                       | 4 3                     |
|-------------------------|-------------------------|-------------------------|-------------------------|
| LAST_VALUE_CH2_MSB[7:0] | LAST_VALUE_CH2_MSB[7:0] | LAST_VALUE_CH2_MSB[7:0] | LAST_VALUE_CH2_MSB[7:0] |
| R-0b                    |                         |                         |                         |

## Table 102. RECENT\_CH2\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH2_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.92 RECENT\_CH3\_LSB Register (Address = 0xA6) [reset = 0x0]

RECENT\_CH3\_LSB is shown in Figure 129 and described in Table 103.

Return to the Summary Table.

## Figure 129. RECENT\_CH3\_LSB Register

| 7                       | 5 4 3                   |
|-------------------------|-------------------------|
| LAST_VALUE_CH3_LSB[7:0] | LAST_VALUE_CH3_LSB[7:0] |
| R-0b                    | R-0b                    |

## Table 103. RECENT\_CH3\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH3_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.93 RECENT\_CH3\_MSB Register (Address = 0xA7) [reset = 0x0]

RECENT\_CH3\_MSB is shown in Figure 130 and described in Table 104.

Return to the Summary Table.

## Figure 130. RECENT\_CH3\_MSB Register

| 7                       | 5 4 3                   |
|-------------------------|-------------------------|
| LAST_VALUE_CH3_MSB[7:0] | LAST_VALUE_CH3_MSB[7:0] |
| R-0b                    | R-0b                    |

## Table 104. RECENT\_CH3\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH3_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.94 RECENT\_CH4\_LSB Register (Address = 0xA8) [reset = 0x0]

RECENT\_CH4\_LSB is shown in Figure 131 and described in Table 105.

Return to the Summary Table.

## Figure 131. RECENT\_CH4\_LSB Register

| 7                       | 4 3 2                   |
|-------------------------|-------------------------|
| LAST_VALUE_CH4_LSB[7:0] | LAST_VALUE_CH4_LSB[7:0] |
| R-0b                    | R-0b                    |

## Table 105. RECENT\_CH4\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH4_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.95 RECENT\_CH4\_MSB Register (Address = 0xA9) [reset = 0x0]

RECENT\_CH4\_MSB is shown in Figure 132 and described in Table 106.

Return to the Summary Table.

## Figure 132. RECENT\_CH4\_MSB Register

| 7 5                     | 6   | 4    | 3   | 2   | 1   | 0   |
|-------------------------|-----|------|-----|-----|-----|-----|
| LAST_VALUE_CH4_MSB[7:0] |     |      |     |     |     |     |
|                         |     | R-0b |     |     |     |     |

## Table 106. RECENT\_CH4\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH4_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

![Image](output_part3_artifacts\image_000003_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000004_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 8.6.96 RECENT\_CH5\_LSB Register (Address = 0xAA) [reset = 0x0]

RECENT\_CH5\_LSB is shown in Figure 133 and described in Table 107.

Return to the Summary Table.

## Figure 133. RECENT\_CH5\_LSB Register

| 7                       | 4                       |
|-------------------------|-------------------------|
| LAST_VALUE_CH5_LSB[7:0] | LAST_VALUE_CH5_LSB[7:0] |
| R-0b                    | R-0b                    |

## Table 107. RECENT\_CH5\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH5_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.97 RECENT\_CH5\_MSB Register (Address = 0xAB) [reset = 0x0]

RECENT\_CH5\_MSB is shown in Figure 134 and described in Table 108.

Return to the Summary Table.

Figure 134. RECENT\_CH5\_MSB Register

|                         | 5 4 3 2                 |
|-------------------------|-------------------------|
| LAST_VALUE_CH5_MSB[7:0] | LAST_VALUE_CH5_MSB[7:0] |
| R-0b                    | R-0b                    |

## Table 108. RECENT\_CH5\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH5_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.98 RECENT\_CH6\_LSB Register (Address = 0xAC) [reset = 0x0]

RECENT\_CH6\_LSB is shown in Figure 135 and described in Table 109.

Return to the Summary Table.

## Figure 135. RECENT\_CH6\_LSB Register

| 7                       | 4                       |
|-------------------------|-------------------------|
| LAST_VALUE_CH6_LSB[7:0] | LAST_VALUE_CH6_LSB[7:0] |
| R-0b                    | R-0b                    |

## Table 109. RECENT\_CH6\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH6_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.99 RECENT\_CH6\_MSB Register (Address = 0xAD) [reset = 0x0]

RECENT\_CH6\_MSB is shown in Figure 136 and described in Table 110.

Return to the Summary Table.

## Figure 136. RECENT\_CH6\_MSB Register

| 7 5 3                   | 6   | 4    | 2   | 1   | 0   |
|-------------------------|-----|------|-----|-----|-----|
| LAST_VALUE_CH6_MSB[7:0] |     |      |     |     |     |
|                         |     | R-0b |     |     |     |

## Table 110. RECENT\_CH6\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH6_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.100 RECENT\_CH7\_LSB Register (Address = 0xAE) [reset = 0x0]

RECENT\_CH7\_LSB is shown in Figure 137 and described in Table 111.

Return to the Summary Table.

## Figure 137. RECENT\_CH7\_LSB Register

| 7                       | 4                       |                         |
|-------------------------|-------------------------|-------------------------|
| LAST_VALUE_CH7_LSB[7:0] | LAST_VALUE_CH7_LSB[7:0] | LAST_VALUE_CH7_LSB[7:0] |
| R-0b                    | R-0b                    | R-0b                    |

## Table 111. RECENT\_CH7\_LSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                               |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH7_LSB[ 7:0] | R      | 0b      | Lower 8 bits of the last conversion result for this analog input channel. |

## 8.6.101 RECENT\_CH7\_MSB Register (Address = 0xAF) [reset = 0x0]

RECENT\_CH7\_MSB is shown in Figure 138 and described in Table 112.

Return to the Summary Table.

## Figure 138. RECENT\_CH7\_MSB Register

| 7 4                     | 6                       | 5                       | 3                       |
|-------------------------|-------------------------|-------------------------|-------------------------|
| LAST_VALUE_CH7_MSB[7:0] | LAST_VALUE_CH7_MSB[7:0] | LAST_VALUE_CH7_MSB[7:0] | LAST_VALUE_CH7_MSB[7:0] |
| R-0b                    |                         |                         |                         |

## Table 112. RECENT\_CH7\_MSB Register Field Descriptions

| Bit   | Field                    | Type   | Reset   | Description                                                                           |
|-------|--------------------------|--------|---------|---------------------------------------------------------------------------------------|
| 7-0   | LAST_VALUE_CH7_MSB [7:0] | R      | 0b      | MSB aligned first 8 bits of the last conversion result for this analog input channel. |

## 8.6.102 RMS\_CFG Register (Address = 0xC0) [reset = 0x0]

RMS\_CFG is shown in Figure 139 and described in Table 113.

Return to the Summary Table.

## Figure 139. RMS\_CFG Register

| 6             | 3        | 2          | 1 0              |
|---------------|----------|------------|------------------|
| RMS_CHID[3:0] | RESERVED | RMS_DC_SUB | RMS_SAMPLES[1:0] |
| R/W-0b        | R-0b     | R/W-0b     | R/W-0b           |

![Image](output_part3_artifacts\image_000005_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000006_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

Table 113. RMS\_CFG Register Field Descriptions

| Bit   | Field            | Type   | Reset   | Description                                                                                                                                                |
|-------|------------------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-4   | RMS_CHID[3:0]    | R/W    | 0b      | Select analog input channel for RMS computation.                                                                                                           |
| 3     | RESERVED         | R      | 0b      | Reserved. Reads return 0b.                                                                                                                                 |
| 2     | RMS_DC_SUB       | R/W    | 0b      | Subtract DC component from the RMS result. 0b = Do not subtract DC component. 1b = Subtract DC component.                                                  |
| 1-0   | RMS_SAMPLES[1:0] | R/W    | 0b      | Number of samples for computing RMS result. Additional 40 samples are required for completing RMS computation. 0b = 1024 1b = 4096 10b = 16384 11b = 65536 |

## 8.6.103 RMS\_LSB Register (Address = 0xC1) [reset = 0x0]

RMS\_LSB is shown in Figure 140 and described in Table 114.

Return to the Summary Table.

Figure 140. RMS\_LSB Register

| 7                   | 4                   |
|---------------------|---------------------|
| RMS_RESULT_LSB[7:0] | RMS_RESULT_LSB[7:0] |
| R-0b                | R-0b                |

## Table 114. RMS\_LSB Register Field Descriptions

| Bit   | Field               | Type   | Reset   | Description                             |
|-------|---------------------|--------|---------|-----------------------------------------|
| 7-0   | RMS_RESULT_LSB[7:0] | R      | 0b      | Lower 8-bits of RMS computation result. |

## 8.6.104 RMS\_MSB Register (Address = 0xC2) [reset = 0x0]

RMS\_MSB is shown in Figure 141 and described in Table 115. Return to the Summary Table.

Figure 141. RMS\_MSB Register

| 7                   | 6                   | 5                   | 4                   | 3                   | 2                   | 1                   | 0                   |
|---------------------|---------------------|---------------------|---------------------|---------------------|---------------------|---------------------|---------------------|
| RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] | RMS_RESULT_MSB[7:0] |
|                     |                     |                     |                     | R-0b                |                     |                     |                     |

## Table 115. RMS\_MSB Register Field Descriptions

| Bit   | Field               | Type   | Reset   | Description                 |
|-------|---------------------|--------|---------|-----------------------------|
| 7-0   | RMS_RESULT_MSB[7:0] | R      | 0b      | Upper 8-bits of RMS result. |

## 8.6.105 GPO0\_TRIG\_EVENT\_SEL Register (Address = 0xC3) [reset = 0x2]

GPO0\_TRIG\_EVENT\_SEL is shown in Figure 142 and described in Table 116.

Return to the Summary Table.

Figure 142. GPO0\_TRIG\_EVENT\_SEL Register

| 7                        | 5 4 3                    |
|--------------------------|--------------------------|
| GPO0_TRIG_EVENT_SEL[7:0] | GPO0_TRIG_EVENT_SEL[7:0] |

## Table 116. GPO0\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO0_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO0. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO0 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO0 output. |

## 8.6.106 GPO1\_TRIG\_EVENT\_SEL Register (Address = 0xC5) [reset = 0x2]

GPO1\_TRIG\_EVENT\_SEL is shown in Figure 143 and described in Table 117.

Return to the Summary Table.

## Figure 143. GPO1\_TRIG\_EVENT\_SEL Register

| 5 4 3                    |
|--------------------------|
| GPO1_TRIG_EVENT_SEL[7:0] |
| R/W-10b                  |

## Table 117. GPO1\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO1_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO1. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO1 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO1 output. |

## 8.6.107 GPO2\_TRIG\_EVENT\_SEL Register (Address = 0xC7) [reset = 0x2]

GPO2\_TRIG\_EVENT\_SEL is shown in Figure 144 and described in Table 118.

Return to the Summary Table.

## Figure 144. GPO2\_TRIG\_EVENT\_SEL Register

| 7                        | 4                        |                          |
|--------------------------|--------------------------|--------------------------|
| GPO2_TRIG_EVENT_SEL[7:0] | GPO2_TRIG_EVENT_SEL[7:0] | GPO2_TRIG_EVENT_SEL[7:0] |
| R/W-10b                  | R/W-10b                  | R/W-10b                  |

## Table 118. GPO2\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO2_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO2. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO2 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO2 output. |

## 8.6.108 GPO3\_TRIG\_EVENT\_SEL Register (Address = 0xC9) [reset = 0x2]

GPO3\_TRIG\_EVENT\_SEL is shown in Figure 145 and described in Table 119.

Return to the Summary Table.

![Image](output_part3_artifacts\image_000007_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000008_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Figure 145. GPO3\_TRIG\_EVENT\_SEL Register

| 7                        | 4                        |                          |
|--------------------------|--------------------------|--------------------------|
| GPO3_TRIG_EVENT_SEL[7:0] | GPO3_TRIG_EVENT_SEL[7:0] | GPO3_TRIG_EVENT_SEL[7:0] |
| R/W-10b                  | R/W-10b                  | R/W-10b                  |

## Table 119. GPO3\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO3_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO3. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO3 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO3 output. |

## 8.6.109 GPO4\_TRIG\_EVENT\_SEL Register (Address = 0xCB) [reset = 0x2]

GPO4\_TRIG\_EVENT\_SEL is shown in Figure 146 and described in Table 120.

Return to the Summary Table.

## Figure 146. GPO4\_TRIG\_EVENT\_SEL Register

| 7                        | 5                        |
|--------------------------|--------------------------|
| GPO4_TRIG_EVENT_SEL[7:0] | GPO4_TRIG_EVENT_SEL[7:0] |

## Table 120. GPO4\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO4_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO4. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO4 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO4 output. |

## 8.6.110 GPO5\_TRIG\_EVENT\_SEL Register (Address = 0xCD) [reset = 0x2]

GPO5\_TRIG\_EVENT\_SEL is shown in Figure 147 and described in Table 121.

Return to the Summary Table.

## Figure 147. GPO5\_TRIG\_EVENT\_SEL Register

| 7                        |                          |
|--------------------------|--------------------------|
| GPO0_TRIG_EVENT_SEL[7:0] | GPO0_TRIG_EVENT_SEL[7:0] |
| R/W-10b                  | R/W-10b                  |

## Table 121. GPO5\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO0_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO5. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO5 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO5 output. |

## 8.6.111 GPO6\_TRIG\_EVENT\_SEL Register (Address = 0xCF) [reset = 0x2]

GPO6\_TRIG\_EVENT\_SEL is shown in Figure 148 and described in Table 122.

Return to the Summary Table.

Figure 148. GPO6\_TRIG\_EVENT\_SEL Register

| 7                        |                          |
|--------------------------|--------------------------|
| GPO6_TRIG_EVENT_SEL[7:0] | GPO6_TRIG_EVENT_SEL[7:0] |
| R/W-10b                  | R/W-10b                  |

## Table 122. GPO6\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO6_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO6. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO6 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO6 output. |

## 8.6.112 GPO7\_TRIG\_EVENT\_SEL Register (Address = 0xD1) [reset = 0x2]

GPO7\_TRIG\_EVENT\_SEL is shown in Figure 149 and described in Table 123.

Return to the Summary Table.

Figure 149. GPO7\_TRIG\_EVENT\_SEL Register

| 7                        | 5                        |
|--------------------------|--------------------------|
| GPO7_TRIG_EVENT_SEL[7:0] | GPO7_TRIG_EVENT_SEL[7:0] |

## Table 123. GPO7\_TRIG\_EVENT\_SEL Register Field Descriptions

| Bit   | Field                     | Type   | Reset   | Description                                                                                                                                                                                                                                                                      |
|-------|---------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO7_TRIG_EVENT_SE L[7:0] | R/W    | 10b     | Select the inputs AIN/GPIO[7:0], analog or digital, which can trigger an event based update on GPO7. 0b = Alert flags for the AIN/GPIO corresponding to this bit do not trigger GPO7 output. 1b = Alert flags for the AIN/GPIO corresponding to this bit do trigger GPO7 output. |

## 8.6.113 GPO\_VALUE\_ZCD\_CFG\_CH0\_CH3 Register (Address = 0xE3) [reset = 0x0]

GPO\_VALUE\_ZCD\_CFG\_CH0\_CH3 is shown in Figure 150 and described in Table 124.

Return to the Summary Table.

Figure 150. GPO\_VALUE\_ZCD\_CFG\_CH0\_CH3 Register

| 7 6                         | 5 4                         | 3 2                         | 1 0                         |
|-----------------------------|-----------------------------|-----------------------------|-----------------------------|
| GPO_VALUE_ZCD_CFG_CH3[1 :0] | GPO_VALUE_ZCD_CFG_CH2[1 :0] | GPO_VALUE_ZCD_CFG_CH1[1 :0] | GPO_VALUE_ZCD_CFG_CH0[1 :0] |
| R/W-0b                      | R/W-0b                      | R/W-0b                      | R/W-0b                      |

![Image](output_part3_artifacts\image_000009_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000010_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

Table 124. GPO\_VALUE\_ZCD\_CFG\_CH0\_CH3 Register Field Descriptions

| Bit   | Field                       | Type   | Reset   | Description                                                                                                                                                                                                                                                            |
|-------|-----------------------------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-6   | GPO_VALUE_ZCD_CFG _CH3[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |
| 5-4   | GPO_VALUE_ZCD_CFG _CH2[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |
| 3-2   | GPO_VALUE_ZCD_CFG _CH1[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |
| 1-0   | GPO_VALUE_ZCD_CFG _CH0[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |

## 8.6.114 GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 Register (Address = 0xE4) [reset = 0x0]

GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 is shown in Figure 151 and described in Table 125.

Return to the Summary Table.

Figure 151. GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 Register

| 7 6                         | 5 4                         | 3 2                         | 1 0                         |
|-----------------------------|-----------------------------|-----------------------------|-----------------------------|
| GPO_VALUE_ZCD_CFG_CH7[1 :0] | GPO_VALUE_ZCD_CFG_CH6[1 :0] | GPO_VALUE_ZCD_CFG_CH5[1 :0] | GPO_VALUE_ZCD_CFG_CH4[1 :0] |
| R/W-0b                      | R/W-0b                      | R/W-0b                      | R/W-0b                      |

Table 125. GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 Register Field Descriptions

| Bit   | Field                       | Type   | Reset   | Description                                                                                                                                                                                                                                                            |
|-------|-----------------------------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-6   | GPO_VALUE_ZCD_CFG _CH7[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |
| 5-4   | GPO_VALUE_ZCD_CFG _CH6[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |

## Figure 152. GPO\_ZCD\_UPDATE\_EN Register

| 7                      | 4 3                    |
|------------------------|------------------------|
| GPO_ZCD_UPDATE_EN[7:0] | GPO_ZCD_UPDATE_EN[7:0] |
| R/W-0b                 | R/W-0b                 |

## Table 126. GPO\_ZCD\_UPDATE\_EN Register Field Descriptions

| Bit   | Field                   | Type   | Reset   | Description                                                                                                                                                                                                                |
|-------|-------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO_ZCD_UPDATE_EN[ 7:0] | R/W    | 0b      | Update digital outputs GPO[7:0] synchronous to ZCD. 0b = Digital output is not updated synchronous to the ZCD event. 1b = Digital output is updated synchronous to ZCD event. Configure the GPO_VALUE_ON_ZCD_CFG register. |

## 8.6.116 GPO\_TRIGGER\_CFG Register (Address = 0xE9) [reset = 0x0]

GPO\_TRIGGER\_CFG is shown in Figure 153 and described in Table 127.

Return to the Summary Table.

## Figure 153. GPO\_TRIGGER\_CFG Register

| 7                          | 6                          | 5 4 3                      |
|----------------------------|----------------------------|----------------------------|
| GPO_TRIGGER_UPDATE_EN[7:0] | GPO_TRIGGER_UPDATE_EN[7:0] | GPO_TRIGGER_UPDATE_EN[7:0] |
| R/W-0b                     | R/W-0b                     | R/W-0b                     |

## Table 127. GPO\_TRIGGER\_CFG Register Field Descriptions

| Bit   | Field                       | Type   | Reset   | Description                                                                                                                                                                                                                                                                                                                       |
|-------|-----------------------------|--------|---------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO_TRIGGER_UPDAT E_EN[7:0] | R/W    | 0b      | Update digital outputs GPO[7:0] when the corresponding trigger is set. 0b = Digital output is not updated in response to the alert flags. 1b = Digital output is updated when the corresponding alert flags are set. Configure GPOx_TRIG_EVENT_SEL register to select which alert flags can trigger an update on the desired GPO. |

![Image](output_part3_artifacts\image_000011_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

## Table 125. GPO\_VALUE\_ZCD\_CFG\_CH4\_CH7 Register Field Descriptions (continued)

| Bit   | Field                       | Type   | Reset   | Description                                                                                                                                                                                                                                                            |
|-------|-----------------------------|--------|---------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 3-2   | GPO_VALUE_ZCD_CFG _CH5[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |
| 1-0   | GPO_VALUE_ZCD_CFG _CH4[1:0] | R/W    | 0b      | Define the GPO value to be launched on ZCD rising and falling edges. 0b = Rising (0) and falling (0) -> logic 0 on both edges 1b = Rising (0) and falling (1) -> ZCD 10b = Rising (1) and falling (0) -> ZCD 11b = Rising (1) and falling (1) -> logic 1 on both edges |

## 8.6.115 GPO\_ZCD\_UPDATE\_EN Register (Address = 0xE7) [reset = 0x0]

GPO\_ZCD\_UPDATE\_EN is shown in Figure 152 and described in Table 126.

Return to the Summary Table.

![Image](output_part3_artifacts\image_000012_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 8.6.117 GPO\_VALUE\_TRIG Register (Address = 0xEB) [reset = 0x0]

GPO\_VALUE\_TRIG is shown in Figure 154 and described in Table 128.

Return to the Summary Table.

## Figure 154. GPO\_VALUE\_TRIG Register

| 7                         | 5 4 3 2                   |
|---------------------------|---------------------------|
| GPO_VALUE_ON_TRIGGER[7:0] | GPO_VALUE_ON_TRIGGER[7:0] |
| R/W-0b                    | R/W-0b                    |

## Table 128. GPO\_VALUE\_TRIG Register Field Descriptions

| Bit   | Field                      | Type   | Reset   | Description                                                                                                                                                                                                                                                          |
|-------|----------------------------|--------|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 7-0   | GPO_VALUE_ON_TRIGG ER[7:0] | R/W    | 0b      | Value to be set on digital outputs GPO[7:0] when the corresponding trigger occurs. GPO update on alert flags must be enabled in the corresponding bit in the GPO_TRIGGER_CFG register. 0b = Digital output is set to logic 0. 1b = Digital output is set to logic 1. |

## 9 Application and Implementation

## NOTE

Information in the following applications sections is not part of the TI component specification, and TI does not warrant its accuracy or completeness. TI's customers are responsible for determining suitability of components for their purposes. Customers should validate and test their design implementation to confirm system functionality.

## 9.1 Application Information

The following sections give example circuits and suggestions for using the ADS7128 in various applications.

## 9.2 Typical Applications

## 9.2.1 Mixed-Channel Configuration

Figure 155. DAQ Circuit: Single-Supply DAQ

![Image](output_part3_artifacts\image_000013_45c75465f41e814b6f039052a240b6d37965de3a9584c4ec8559120c88cb412e.png)

## 9.2.1.1 Design Requirements

The goal of this application is to configure some channels of the ADS7128 as digital inputs, open-drain digital outputs, and push-pull digital outputs.

## 9.2.1.2 Detailed Design Procedure

The ADS7128 can support GPIO functionality at each input pin. Any analog input pin can be independently configured as a digital input, a digital open-drain output, or a digital push-pull output though the PIN\_CFG and GPIO\_CFG registers; see Table 4.

## 9.2.1.2.1 Digital Input

The digital input functionality can be used to monitor a signal within the system. Figure 156 illustrates that the state of the digital input can be read from the GPI\_VALUE register.

Figure 156. Digital Input

![Image](output_part3_artifacts\image_000014_ea6a70ab4275d4b341fd1f48f0d8ced51d2ac35e1b2970db96bf73e1fa76f993.png)

![Image](output_part3_artifacts\image_000015_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000016_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## Typical Applications (continued)

## 9.2.1.2.2 Digital Open-Drain Output

The channels of the ADS7128 can be configured as digital open-drain outputs supporting an output voltage up to 5.5 V. An open-drain output, as shown in Figure 157, consists of an internal FET (Q) connected to ground. The output is idle when not driven by the device, which means Q is off and the pull-up resistor, RPULL\_UP, connects the GPOx node to the desired output voltage. The output voltage can range anywhere up to 5.5 V, depending on the external voltage that the GPIOx is pulled up to. When the device is driving the output, Q turns on, thus connecting the pull-up resistor to ground and bringing the node voltage at GPOx low.

Figure 157. Digital Open-Drain Output

![Image](output_part3_artifacts\image_000017_da4518fbf37ab527fa9edf5cb1b6c82a94a56202e35960df9c48a0edddd4b1f8.png)

The minimum value of the pullup resistor, as calculated in Equation 6, is given by the ratio of VPULL\_UP and the maximum current supported by the device digital output (5 mA).

<!-- formula-not-decoded -->

The maximum value of the pullup resistor, as calculated in Equation 7, depends on the minimum input current requirement, ILOAD, of the receiving device driven by this GPIO.

<!-- formula-not-decoded -->

Select RPULL\_UP such that RMIN &lt; RPULL\_UP &lt; RMAX.

## 9.2.1.3 Application Curve

![Image](output_part3_artifacts\image_000018_b3bd55a44a1fa89e1e71a09ea27d416c484ba912c33d2e1c87105d0995abfc62.png)

Standard deviation = 0.49 LSB

Figure 158. DC Input Histogram

## Typical Applications (continued)

## 9.2.2 Digital Push-Pull Output

The channels of the ADS7128 can be configured as digital push-pull outputs supporting an output voltage up to AVDD. As shown in Figure 159, a push-pull output consists of two mirrored opposite bipolar transistors, Q1 and Q2. The device can both source and sink current because only one transistor is on at a time (either Q2 is on and pulls the output low, or Q1 is on and sets the output high). A push-pull configuration always drives the line opposed to an open-drain output where the line is left floating.

Figure 159. Digital Push-Pull Output

![Image](output_part3_artifacts\image_000019_349c3c1966fdb6a657c251d2bc7384475451140df524e5b2bab8107a4bc39ac6.png)

![Image](output_part3_artifacts\image_000020_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000021_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 10 Power Supply Recommendations

## 10.1 AVDD and DVDD Supply Recommendations

The ADS7128 has two separate power supplies: AVDD and DVDD. The device operates on AVDD; DVDD is used for the interface circuits. For supplies greater than 2.35 V, AVDD and DVDD can be shorted externally if single-supply operation is desired. The AVDD supply also defines the full-scale input range of the device. Decouple the AVDD and DVDD pins individually, as shown in Figure 160, with 1-µF ceramic decoupling capacitors. The minimum capacitor value required for AVDD and DVDD is 200 nF and 20 nF, respectively. If both supplies are powered from the same source, a minimum capacitor value of 220 nF is required for decoupling.

Connect a 1-µF decoupling capacitor between the DECAP and GND pins for the internal power supply.

Figure 160. Power-Supply Decoupling

![Image](output_part3_artifacts\image_000022_2bacc47f6ca3bc4925f82cad29d42a092c99d2cc31102014b1b611a3ca43f6d1.png)

## 11 Layout

## 11.1 Layout Guidelines

Figure 161 shows a board layout example for the ADS7128. Avoid crossing digital lines with the analog signal path and keep the analog input signals and the AVDD supply away from noise sources.

Use 1-µF ceramic bypass capacitors in close proximity to the analog (AVDD) and digital (DVDD) power-supply pins. Avoid placing vias between the AVDD and DVDD pins and the bypass capacitors. Connect the GND pin to the ground plane using short, low-impedance paths. The AVDD supply voltage also functions as the reference voltage for the ADS7128. Place the decoupling capacitor for AVDD close to the device AVDD and GND pins and connect the decoupling capacitor to the device pins with thick copper tracks.

## 11.2 Layout Example

Figure 161. Example Layout

![Image](output_part3_artifacts\image_000023_66afdbec5d489cb7f73816b24c615177f1e2a08d9e25564dfb1c6b23e9ec5d96.png)

![Image](output_part3_artifacts\image_000024_eb16710f3d92d6213b6f6d0a1e50751e2a6f27a8ee7ac9097976bd2bde5e897d.png)

![Image](output_part3_artifacts\image_000025_c432873868acfc8fa4713c7595dfc1a093e671d0942689508d67a8625859dd12.png)

## 12 Device and Documentation Support

## 12.1 Receiving Notification of Documentation Updates

To receive notification of documentation updates, navigate to the device product folder on ti.com. In the upper right corner, click on Alert me to register and receive a weekly digest of any product information that has changed. For change details, review the revision history included in any revised document.

## 12.2 Support Resources

TI E2E™ support forums are an engineer's go-to source for fast, verified answers and design help - straight from the experts. Search existing answers or ask your own question to get the quick design help you need.

Linked content is provided "AS IS" by the respective contributors. They do not constitute TI specifications and do not necessarily reflect TI's views; see TI's Terms of Use.

## 12.3 Trademarks

E2E is a trademark of Texas Instruments.

All other trademarks are the property of their respective owners.

## 12.4 Electrostatic Discharge Caution

![Image](output_part3_artifacts\image_000026_84a6751134446726bc667d78cf1b5c0f27d136d0fcf7cf1c40ed81e53a528c3b.png)

This integrated circuit can be damaged by ESD. Texas Instruments recommends that all integrated circuits be handled with appropriate precautions. Failure to observe proper handling and installation procedures can cause damage.

ESD damage can range from subtle performance degradation to complete device failure. Precision integrated circuits may be more susceptible to damage because very small parametric changes could cause the device not to meet its published specifications.

## 12.5 Glossary

SLYZ022 TI Glossary .

This glossary lists and explains terms, acronyms, and definitions.

## 13 Mechanical, Packaging, and Orderable Information

The following pages include mechanical, packaging, and orderable information. This information is the most current data available for the designated devices. This data is subject to change without notice and revision of this document. For browser-based versions of this data sheet, refer to the left-hand navigation.

![Image](output_part3_artifacts\image_000027_abc83b5cf45be650f509368d5c7e4edf9eca625998d3752fc0afe03ff4d78e2c.png)

SCALE  3.60

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000028_f7f1aa41721fa449b04a0b891577ab3df9468ae8ed6a9a17669e38e6538ff6fb.png)

## NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. The package thermal pad must be soldered to the printed circuit board for thermal and mechanical performance.

![Image](output_part3_artifacts\image_000029_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000030_9e714b3797fc6d10150ef7b5108f867d73403db4a8305c8fdfa74dc40a480a19.png)

NOTES: (continued)

4. This package is designed to be soldered to a thermal pad on the board. For more information, see Texas Instruments literature number SLUA271 (www.ti.com/lit/slua271).
5. Vias are optional depending on application, refer to device data sheet. If any vias are implemented, refer to their locations shown on this view. It is recommended that vias under paste be filled, plugged or tented.

![Image](output_part3_artifacts\image_000031_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

## WQFN - 0.8 mm max height

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000032_b93651a063361048b58d15ba2e5d599a9423fd0481c41da2c0a0989cf78d0eed.png)

NOTES: (continued)

6. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate design recommendations.

![Image](output_part3_artifacts\image_000033_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

![Image](output_part3_artifacts\image_000034_aec33460263832ad92b1995b813f2a7af9a0df7a177eada2ab8d0069424ff22c.png)

www.ti.com

## PACKAGING INFORMATION

| Orderable part number   | Status (1)   | Material type (2)   | Package &#124; Pins   |           | Package qty &#124; Carrier   | RoHS (3)   | Lead finish/ Ball material (4)   | MSL rating/ Peak reflow (5)   | Op temp (°C)   | Part marking (6)   |
|-------------------------|--------------|---------------------|-----------------------|-----------|------------------------------|------------|----------------------------------|-------------------------------|----------------|--------------------|
| ADS7128IRTER            | Active       | Production          | WQFN (RTE)            | &#124; 16 | 3000 &#124; LARGE T&R        | Yes        | NIPDAU                           | Level-1-260C-UNLIM            | -40 to 85      | X71X8              |
| ADS7128IRTER.A          | Active       | Production          | WQFN (RTE)            | &#124; 16 | 3000 &#124; LARGE T&R        | Yes        | NIPDAU                           | Level-1-260C-UNLIM            | -40 to 85      | X71X8              |
| ADS7128IRTET            | Active       | Production          | WQFN (RTE)            | &#124; 16 | 250 &#124; SMALL T&R         | Yes        | NIPDAU                           | Level-1-260C-UNLIM            | -40 to 85      | X71X8              |
| ADS7128IRTET.A          | Active       | Production          | WQFN (RTE)            | &#124; 16 | 250 &#124; SMALL T&R         | Yes        | NIPDAU                           | Level-1-260C-UNLIM            | -40 to 85      | X71X8              |

(3) RoHS values: Yes, No, RoHS Exempt. See the TI RoHS Statement for additional information and value definition.

(4) Lead finish/Ball material: Parts may have multiple material finish options. Finish options are separated by a vertical ruled line. Lead finish/Ball material values may wrap to two lines if the finish value exceeds the maximum column width.

(5) MSL rating/Peak reflow: The moisture sensitivity level ratings and peak solder (reflow) temperatures. In the event that a part has multiple moisture sensitivity ratings, only the lowest level per JEDEC standards is shown. Refer to the shipping label for the actual reflow temperature that will be used to mount the part to the printed circuit board.

(6) Part marking: There may be an additional marking, which relates to the logo, the lot trace code information, or the environmental category of the part.

Multiple part markings will be inside parentheses. Only one part marking contained in parentheses and separated by a "~" will appear on a part. If a line is indented then it is a continuation of the previous line and the two combined represent the entire part marking for that device.

Important Information and Disclaimer: The information provided on this page represents TI's knowledge and belief as of the date that it is provided. TI bases its knowledge and belief on information provided by third parties, and makes no representation or warranty as to the accuracy of such information. Efforts are underway to better integrate information from third parties. TI has taken and continues to take reasonable steps to provide representative and accurate information but may not have conducted destructive testing or chemical analysis on incoming materials and chemicals. TI and TI suppliers consider certain information to be proprietary, and thus CAS numbers and other limited information may not be available for release.

In no event shall TI's liability arising out of such information exceed the total purchase price of the TI part(s) at issue in this document sold by TI to Customer on an annual basis.

![Image](output_part3_artifacts\image_000035_5d4ed78c8362cfa66f79d55d3ac80225c236bdd95695b3a42030daba7904ac99.png)

www.ti.com

## TAPE AND REEL INFORMATION

![Image](output_part3_artifacts\image_000036_7916e760e90f26e4f04a586b6bf543e28e99fceab9db9b8da4c50bcc5b5eb1c8.png)

## QUADRANT ASSIGNMENTS FOR PIN 1 ORIENTATION IN TAPE

![Image](output_part3_artifacts\image_000037_86c531b4779dc5f6d3c7fa31ae4c08507fee8a20fcfadb36f3f6c9a19cdf1393.png)

| Device       | Package Type   | Package Drawing   |   Pins |   SPQ |   Reel Diameter (mm) |   Reel Width W1 (mm) |   A0 (mm) |   B0 (mm) |   K0 (mm) |   P1 (mm) |   W (mm) | Pin1 Quadrant   |
|--------------|----------------|-------------------|--------|-------|----------------------|----------------------|-----------|-----------|-----------|-----------|----------|-----------------|
| ADS7128IRTER | WQFN           | RTE               |     16 |  3000 |                330.0 |                 12.4 |       3.3 |       3.3 |       1.1 |       8.0 |     12.0 | Q2              |
| ADS7128IRTET | WQFN           | RTE               |     16 |   250 |                180.0 |                 12.4 |       3.3 |       3.3 |       1.1 |       8.0 |     12.0 | Q2              |

## *All dimensions are nominal

## PACKAGE MATERIALS INFORMATION

10-Jul-2025

![Image](output_part3_artifacts\image_000038_5d4ed78c8362cfa66f79d55d3ac80225c236bdd95695b3a42030daba7904ac99.png)

www.ti.com

![Image](output_part3_artifacts\image_000039_8820c93a2294a34101c87dbe60bab20b550570fa4048f0dda10aa7134fe43cf7.png)

*All dimensions are nominal

| Device       | Package Type   | Package Drawing   |   Pins |   SPQ |   Length (mm) |   Width (mm) |   Height (mm) |
|--------------|----------------|-------------------|--------|-------|---------------|--------------|---------------|
| ADS7128IRTER | WQFN           | RTE               |     16 |  3000 |         367.0 |        367.0 |          35.0 |
| ADS7128IRTET | WQFN           | RTE               |     16 |   250 |         210.0 |        185.0 |          35.0 |

## PACKAGE MATERIALS INFORMATION

10-Jul-2025

3 x 3, 0.5 mm pitch PLASTIC QUAD FLATPACK - NO LEAD

This image is a representation of the package family, actual package may vary. Refer to the product data sheet for package details.

![Image](output_part3_artifacts\image_000040_e14451b49a730459517068bda98d7f7be64032b976be55f72051ee7dc7b1365f.png)

![Image](output_part3_artifacts\image_000041_ef7530faca57e89fbdbbf8dcf1617a9f3fcca57c748c4776622b4be9afbd973c.png)

![Image](output_part3_artifacts\image_000042_abc83b5cf45be650f509368d5c7e4edf9eca625998d3752fc0afe03ff4d78e2c.png)

SCALE  3.60

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000043_f7f1aa41721fa449b04a0b891577ab3df9468ae8ed6a9a17669e38e6538ff6fb.png)

## NOTES:

1. All linear dimensions are in millimeters. Any dimensions in parenthesis are for reference only. Dimensioning and tolerancing per ASME Y14.5M.
2. This drawing is subject to change without notice.
3. The package thermal pad must be soldered to the printed circuit board for thermal and mechanical performance.

![Image](output_part3_artifacts\image_000044_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000045_9e714b3797fc6d10150ef7b5108f867d73403db4a8305c8fdfa74dc40a480a19.png)

NOTES: (continued)

4. This package is designed to be soldered to a thermal pad on the board. For more information, see Texas Instruments literature number SLUA271 (www.ti.com/lit/slua271).
5. Vias are optional depending on application, refer to device data sheet. If any vias are implemented, refer to their locations shown on this view. It is recommended that vias under paste be filled, plugged or tented.

![Image](output_part3_artifacts\image_000046_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

## WQFN - 0.8 mm max height

PLASTIC QUAD FLATPACK - NO LEAD

![Image](output_part3_artifacts\image_000047_b93651a063361048b58d15ba2e5d599a9423fd0481c41da2c0a0989cf78d0eed.png)

NOTES: (continued)

6. Laser cutting apertures with trapezoidal walls and rounded corners may offer better paste release. IPC-7525 may have alternate design recommendations.

![Image](output_part3_artifacts\image_000048_9f4fc5df32a33bd7d207e0ac6d3712c3b054039b7578de1bc569c674816626ff.png)

## IMPORTANT NOTICE AND DISCLAIMER

TI PROVIDES TECHNICAL AND RELIABILITY DATA (INCLUDING DATASHEETS), DESIGN RESOURCES (INCLUDING REFERENCE DESIGNS), APPLICATION OR OTHER DESIGN ADVICE, WEB TOOLS, SAFETY INFORMATION, AND OTHER RESOURCES 'AS IS' AND WITH ALL FAULTS, AND DISCLAIMS ALL WARRANTIES, EXPRESS AND IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY RIGHTS.

These resources are intended for skilled developers designing with TI products. You are solely responsible for (1) selecting the appropriate TI products for your application, (2) designing, validating and testing your application, and (3) ensuring your application meets applicable standards, and any other safety, security, regulatory or other requirements.

These resources are subject to change without notice. TI grants you permission to use these resources only for development of an application that uses the TI products described in the resource. Other reproduction and display of these resources is prohibited. No license is granted to any other TI intellectual property right or to any third party intellectual property right. TI disclaims responsibility for, and you fully indemnify TI and its representatives against any claims, damages, costs, losses, and liabilities arising out of your use of these resources.

TI's products are provided subject to TI's Terms of Sale, TI's General Quality Guidelines, or other applicable terms available either on ti.com or provided in conjunction with such TI products. TI's provision of these resources does not expand or otherwise alter TI's applicable warranties or warranty disclaimers for TI products. Unless TI explicitly designates a product as custom or customer-specified, TI products are standard, catalog, general purpose devices.

TI objects to and rejects any additional or different terms you may propose.

IMPORTANT NOTICE

Copyright © 2025, Texas Instruments Incorporated Last updated 10/2025