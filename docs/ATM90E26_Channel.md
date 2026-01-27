在 nRF54L15 的 Zephyr Sensor Driver 架構下，針對 **Atmel M90E26** 計量晶片，應根據晶片提供的物理測量參數與系統（TJ3-nRF）的監控需求，輸出對應的 **Sensor Channels**。

根據來源文件（Datasheet 與應用筆記），建議輸出的標準與自定義 Channel 如下：

### 1. 標準電性參數 Channel (Standard Channels)
這些 Channel 對應 Zephyr 內建的 `sensor_chan` 枚舉，直接映射 ATM90E26 的測量暫存器：

*   **`SENSOR_CHAN_VOLTAGE` (電壓)**：對應 `Urms` (49H) 暫存器，提供實體單位的電壓 RMS 值（XXX.XX V）。
*   **`SENSOR_CHAN_CURRENT` (電流)**：
    *   對應 `Irms` (48H, L 線) 與 `Irms2` (68H, N 線) 暫存器。
    *   在 TJ3-nRF 系統中，這也包含 6 個通道的負荷電流測量。
*   **`SENSOR_CHAN_POWER` (有功功率)**：對應 `Pmean` (4AH) 與 `Pmean2` (6AH) 暫存器。
*   **`SENSOR_CHAN_REACTIVE_POWER` (無功功率)**：對應 `Qmean` (4BH) 與 `Qmean2` (6BH) 暫存器。
*   **`SENSOR_CHAN_APPARENT_POWER` (視在功率)**：對應 `Smean` (4FH) 與 `Smean2` (6FH) 暫存器。
*   **`SENSOR_CHAN_POWER_FACTOR` (功率因數)**：對應 `PowerF` (4DH) 與 `PowerF2` (6DH) 暫存器。
*   **`SENSOR_CHAN_ENERGY` (電能量)**：對應 `APenergy` (40H, 正向有功電能) 等能量暫存器。注意此暫存器讀取後會自動歸零 (Read/Clear)。

### 2. 頻率與相位 Channel
*   **`SENSOR_CHAN_S_HZ` (頻率)**：對應 `Freq` (4CH) 暫存器，測量電網頻率（45.00~65.00 Hz）。
*   **相位角 (Phase Angle)**：對應 `Pangle` (4EH) 與 `Pangle2` (6EH)。Zephyr 標準通道可能需要使用自定義通道（如 `SENSOR_CHAN_PRIV_START` 之後的定義），提供 V 與 I 之間的相位差（-180.0° ~ +180.0°）。

### 3. TJ3-nRF 系統特定自定義 Channel (System Specific)
由於本系統是用於漏電與地絡監控，Driver 應額外定義以下擴充 Channel 以便上層應用調用：

*   **Io (總漏電電流)**：晶片讀取的原始漏電電流值。
*   **Ior (抵抗分漏電電流)**：由 MCU 讀取 `Io` 與 `Phase Angle` 後運算得出的核心指標。
*   **Ig (地絡電流)**：對應第 7 通道 (CH7) 的地絡測量值。
*   **DI Status (接點狀態)**：輸出 DI1/DI2 的數位邏輯狀態。

### 數據格式化要求
在 Zephyr Driver 實作中，所有輸出的數據應轉換為 `sensor_value` 結構（包含整數 `val1` 與百萬分之一的小數 `val2`）：
*   **電流**：應符合 `XX.XXX A` 格式。
*   **電壓**：應符合 `XXX.XX V` 格式。
*   **相位角**：應符合 `XXX.X °` 格式。

**💡 實作提示**：考慮到 ATM90E26 的 **LSB 暫存器 (08H)** 可以擴展 RMS 與功率的精度（由 16-bit 擴展至更高等級），Driver 的 `sample_fetch` 動作應確保讀取目標暫存器後立即鎖定 LSB 數據，以提供更精確的輸出。