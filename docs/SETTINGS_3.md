# 電流計算所需 Settings 參數說明
根據代碼分析，在電流計算公式中需要從 settings 讀取的值有：

## 電流計算公式需要的 Settings 參數

### 1. **CT Ratio (CT 比值)** - `meter_cfg.ct_ratio[ch]`
- **儲存位置**: `meter_config_t.ct_ratio[6]`
- **資料型別**: `uint16_t` (每個通道一個值)
- **用途**: 在公式中作為 `Ratio` 參數
- **範例值**: 
  - ZCT: 400 (2000/5)
  - CT: 100 (1000/10)
- **存取方式**: 透過 BLE UUID `...8c02...` (Meter Config) 讀寫

### 2. **通道增益校正值 (Igain)** - `meter_cfg.ch_igain[ch]`
- **儲存位置**: `meter_config_t.ch_igain[6]`
- **資料型別**: `uint16_t` (每個通道一個值)
- **用途**: 在公式中作為 `Igain` 參數，用於軟體校正
- **功能**: 調整讀值與實際儀器量測值一致
- **存取方式**: 透過 BLE UUID `...8c02...` (Meter Config) 讀寫

### 3. **計算公式**
```c
實際電流 = (暫存器讀值 / 1000) × ct_ratio[ch] / ch_igain[ch]
```

## Ior 計算額外需要的參數 (從 `ai_config_t`)

### 4. **警報類型** - `ai_cfg.alarm_type[ch]`
- **資料型別**: `alarm_type_t` (enum)
- **選項**: 
  - `ALARM_TYPE_IO = 3` (Io 模式)
  - `ALARM_TYPE_IOR_AUTO = 1` (Ior 自動模式)
  - `ALARM_TYPE_IOR_MANUAL = 2` (Ior 手動模式)
- **用途**: 決定是否需要計算 Ior

### 5. **相位類型** - `ai_cfg.phase_type[ch]`
- **資料型別**: `phase_type_t` (enum)
- **選項**:
  - `PHASE_SINGLE = 1` (單相)
  - `PHASE_THREE = 3` (三相)
- **用途**: 決定使用哪個 Ior 計算公式

### 6. **補正相位角** - `ai_cfg.phase_corr[ch]`
- **資料型別**: `phase_correction_t` (enum)
- **範圍**: 0-5 (對應 0°, 30°, 60°, 90°, 120°, 150°)
- **用途**: 在 Ior 計算中作為補償角 α

### 7. **Ior 計算公式**
```c
// 單相
Ior = Io × cos(φ + α)

// 三相
Ior = Io × sin(φ + α) / cos(30°)
```

## 漏電警報判定需要的參數

### 8. **漏電閥值**
- `ai_cfg.light_leak_th[ch]` - 輕漏電閥值 (mA)
- `ai_cfg.heavy_leak_th[ch]` - 重漏電閥值 (mA)

## 設定值存取方式

- **BLE 讀寫**: 透過 UUID `...8c02...` 存取 `meter_config_t` (CT比值、Igain)
- **BLE/MQTT 修改**: 使用 OP Code `0xB5` 修改 AI 設定 (`ai_config_t`)
- **儲存媒介**: External SPI Flash (64Mbit)
- **安全機制**: 需長按 "S" 按鈕 3 秒才能解鎖修改

這些參數在系統啟動時從 Flash 載入，並在需要時透過 `settings_get_meter_config()` 和 `settings_get_ai_config()` 函數讀取。