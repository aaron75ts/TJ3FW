# TJ3 測電功能實現進度

## 已完成

### 1. 設定值管理模組 (settings.c/h)
✅ 創建了完整的設定值數據結構
✅ 實現了 Flash 存取功能 (key-value 格式)
✅ 提供了以下設定值的讀寫 API:
- AI 監控設定 (OP Code 0xB5)
- 通訊與需量設定 (OP Code 0xE5)
- MQTT 設定 (OP Code 0xA5)
- 設備資訊 (OP Code 0xD5)
- 計量校正參數 (特徵值 8c02)

## 待實現

### 2. 測電計算模組 (measure.c/h)
需要實現以下功能：

#### 2.1 基礎數據讀取
- [ ] 從 3 個 ATM90E26 讀取原始數據
- [ ] 通道映射 (ATM1/2/3 → CH1-7)

#### 2.2 增益補正計算
參考 CH1-7_CALC.md：
```c
// CH1 & CH2 (經過隔離變壓器)
Real_Irms_CH1 = Register_Value / ((CT_ratio) * 20000 * 0.4 * CH_Igain);

// CH3 ~ CH6 (差動負載)
Real_Irms_CH3 = Register_Value / ((CT_ratio) * 200 * CH_Igain);
```

#### 2.3 向量合成 (三相電流)
參考 三相電流合成公式.docx：
```c
// 極坐標 → 直角坐標
x_R = I_R * cos(θ_R);
y_R = I_R * sin(θ_R);
x_T = I_T * cos(θ_T);
y_T = I_T * sin(θ_T);

// 合成 S 相
x_S = -(x_R + x_T);
y_S = -(y_R + y_T);
I_S = sqrt(x_S^2 + y_S^2);
```

#### 2.4 Ior 抵抗分漏電計算
```c
// 單相
Ior = Io * cos(θ + α);

// 三相
Ior = Io * sin(θ + α) / cos(30°);
```

### 3. BLE GATT 設定值修改
需要在 ble_gatt.c 中添加：

#### 3.1 Meter Config 特徵值 (8c02) - READ/WRITE
```c
// 20 bytes 格式：
// [CT_Ratio_CH1-6: 2 bytes each = 12 bytes]
// [CH_Igain_CH1-6: 2 bytes each = 12 bytes] - 只佔前6bytes，剩餘padding
```

#### 3.2 Command 特徵值 (8c03) 新增 OP Codes
- ✅ 0xB5: AI 設定變更 (已實現 DI 部分，需擴展完整 AI 設定)
- [ ] 0xE5: 通訊與需量設定
- [ ] 0xA5: MQTT 設定
- [ ] 0xD5: 設備名稱與位置
- ✅ 0xB6: 讀取 AI 設定 (已實現 DI 部分)
- [ ] 0xE6: 讀取通訊與需量設定
- [ ] 0xA6: 讀取 MQTT 設定
- [ ] 0xD6: 讀取設備名稱與位置

### 4. 整合與測試
- ✅ 添加 settings.c 和 measure.c 到 CMakeLists.txt
- ✅ 在 main.c 初始化 settings 和 measure 模組
- ✅ 在主循環調用 measure_perform_cycle()
- ✅ 在主循環調用 measure_perform_cycle()
- [ ] 測試設定值的 Flash 存取
- [ ] 測試 BLE GATT 設定值修改
- [ ] 測試測電計算的準確性

## 目前狀態摘要

✅ **已完成**:
- Settings 模組 (Flash key-value 存取)
- Measure 模組 (測電計算與 Ior)
- 基礎整合 (CMakeLists.txt + main.c)

⏳ **進行中**:
- BLE GATT 設定值修改 (部分 OP codes)

⏸️ **待開始**:
- 完整 BLE 設定修改功能
- 硬體測試與驗證
- 數據記錄與 CSV 輸出

## 實現建議

### 優先級 1: 完成 BLE GATT 設定值修改
1. 在 `ble_gatt.c` 添加 Meter Config 特徵值的 READ/WRITE 處理
2. 實現完整的 OP Code 0xB5 (AI 設定)
3. 實現 OP Code 0xE5, 0xA5, 0xD5

### 優先級 2: 實現測電計算
1. 創建 measure.c/h 基礎架構
2. 實現通道映射和增益補正
3. 實現向量合成
4. 實現 Ior 計算

### 優先級 3: 數據記錄
1. 整合 measure 模組到主循環
2. 定期將計算結果寫入 CSV
3. 實現 1 分鐘值和 30 分鐘值記錄

## 關鍵文檔參考

- `docs/BLE_SETTING.md` - BLE 設定封包格式
- `docs/SETTINGS_1.md` - 設定值存取規範
- `docs/SETTINGS_2.md` - 設定值格式規範
- `docs/CH1-7_CALC.md` - 通道計算公式
- `docs/三相電流合成公式.docx` - 向量合成公式
- `docs/TJ3 測電功能說明(全功能).docx` - 完整功能說明

## 使用範例

### 讀取/修改設定值 (C 代碼)
```c
#include "settings.h"

// 初始化
settings_init();

// 讀取 AI 設定
ai_config_t ai_cfg;
settings_get_ai_config(&ai_cfg);

// 修改並保存
ai_cfg.light_leak_th[0] = 150;  // CH1 輕漏電閥值改為 150mA
settings_set_ai_config(&ai_cfg);  // 自動保存到 Flash
```

### 透過 BLE 修改設定
```python
# OP Code 0xB5 封包範例 (簡化版)
payload = bytearray()
# 警報類型 (6 CH)
payload.extend(b'333333')  # 全部使用 Io 模式

# 完整封包需要按照 BLE_SETTING.md 規範組裝
packet = compose_packet(0xB5, payload)
characteristic.write(packet)
```

## 注意事項

1. **Little Endian**: 大部分數值使用 Little Endian (LSB first)，但 IP 地址使用 Big Endian
2. **S 按鈕**: BLE 修改設定前需長按 S 按鈕 3 秒解鎖
3. **Flash 寫入**: 設定值變更會自動保存到 Flash (settings.txt)
4. **ATM90E26 重載**: 修改計量參數後需要重新初始化 ATM90E26
5. **數據精度**: 電流值精度為 0.001A，漏電值為 1mA

## 下一步行動

建議按以下順序實現：
1. ✅ 完成 settings 模組基礎功能
2. 實現 ble_gatt.c 中的設定值讀寫 (Meter Config + Command)
3. 創建 measure 模組並實現計算邏輯
4. 整合所有模組並測試
