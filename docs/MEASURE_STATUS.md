# 測電計算模組實現狀態

## ✅ 已實現功能

### 1. 核心模組架構
- [x] `measure.h` - 完整的 API 定義
- [x] `measure.c` - 測電計算實現

### 2. 數據讀取 (2.1)
**measure_read_raw_data()** - 從 ATM90E26 讀取原始數據
```c
int measure_read_raw_data(void);
```

實現細節：
- ✅ **CH1**: ATM1 L-line (電壓、電流、功率、相位角)
- ✅ **CH2**: ATM1 N-line (從 current2 讀取)
- ✅ **CH3**: ATM2 L-line
- ✅ **CH4**: ATM2 N-line
- ✅ **CH5**: ATM3 L-line
- ✅ **CH6**: ATM3 N-line
- ⏳ **CH7**: Ig (地絡電流) - 待實現特殊邏輯

讀取數據：
- `raw_voltage`: 電壓原始值 (XXX.XX V, 放大 100 倍)
- `raw_current`: 電流原始值 (XXX.XXX A, 放大 1000 倍)
- `raw_power`: 功率原始值 (W, signed 16-bit)
- `raw_phase_angle`: 相位角原始值

### 3. 增益校正計算 (2.2)
**measure_calculate_channel()** - 計算單一通道實際值
```c
int measure_calculate_channel(channel_t ch);
```

實現細節：
- ✅ **CH1 & CH2 公式** (經過隔離變壓器):
  ```
  Real_Irms = raw_value / (CT_ratio × 20000 × 0.4 × CH_Igain)
  ```
  
- ✅ **CH3 ~ CH6 公式** (差動負載):
  ```
  Real_Irms = raw_value / (CT_ratio × 200 × CH_Igain)
  ```

- ✅ **電壓換算**: `voltage = raw_voltage / 100.0`
- ✅ **功率處理**: 直接使用 signed 16-bit 值
- ✅ **Io 計算**: `io = current × 1000` (A → mA)

### 4. 三相電流合成 (2.3)
**measure_calculate_three_phase()** - 三相向量合成
```c
int measure_calculate_three_phase(channel_t ch_r, channel_t ch_t, 
                                   three_phase_data_t *result);
```

實現細節：
- ✅ **極坐標 → 直角坐標**:
  ```
  x = I × cos(θ)
  y = I × sin(θ)
  ```

- ✅ **S 相合成** (基爾霍夫電流定律):
  ```
  x_S = -(x_R + x_T)
  y_S = -(y_R + y_T)
  I_S = √(x_S² + y_S²)
  ```

- ✅ **三相總電流**: `I_total = I_R + I_S + I_T`

輸出數據：
- `current_r`: R 相電流
- `current_s`: S 相電流 (計算得出)
- `current_t`: T 相電流
- `angle_r`, `angle_t`: R, T 相角
- `total_current`: 三相總電流

### 5. Ior 殘留電流計算 (2.4)
**measure_calculate_ior()** - 計算 Ior
```c
float measure_calculate_ior(channel_t ch, float io, float phase_angle, 
                            phase_correction_t correction, phase_type_t phase_type);
```

實現細節：
- ✅ **補正角度**: α = correction × 30° (0-5 對應 0°-150°)
- ✅ **單相公式**: `Ior = Io × cos(θ + α)`
- ✅ **三相公式**: `Ior = Io × sin(θ + α) / cos(30°)`

補正範圍：
- `PHASE_CORR_0` → 0°
- `PHASE_CORR_30` → 30°
- `PHASE_CORR_60` → 60°
- `PHASE_CORR_90` → 90°
- `PHASE_CORR_120` → 120°
- `PHASE_CORR_150` → 150°

### 6. 完整測量週期 (2.5)
**measure_perform_cycle()** - 執行完整測量流程
```c
int measure_perform_cycle(void);
```

工作流程：
1. ✅ 讀取 ATM1/2/3 原始數據 → 映射到 CH1-6
2. ✅ 計算所有通道的實際值 (增益校正)
3. ✅ 針對配置為 Ior 模式的通道計算 Ior
4. ✅ 執行三相合成 (如果配置為三相)

### 7. 漏電警報判定 (2.6)
**measure_check_leak_alarm()** - 判定漏電等級
```c
int measure_check_leak_alarm(channel_t ch, uint16_t light_threshold, 
                             uint16_t heavy_threshold);
```

實現細節：
- ✅ 根據配置使用 **Io** 或 **Ior** 判定
- ✅ 返回值: `0`=正常, `1`=輕漏電, `2`=重漏電
- ✅ 閾值比較: `leak_current >= heavy_threshold`

### 8. 數據查詢接口 (2.7)
**measure_get_channel_data()** - 獲取單一通道數據
```c
int measure_get_channel_data(channel_t ch, channel_data_t *data);
```

**measure_get_ig_data()** - 獲取 Ig (CH7) 數據
```c
int measure_get_ig_data(channel_data_t *data);
```

### 9. 設定值整合
- ✅ 從 `settings_get_meter_config()` 讀取:
  - `ct_ratio[7]`: CT 變比
  - `ch_igain[7]`: 通道增益
  
- ✅ 從 `settings_get_ai_config()` 讀取:
  - `alarm_type[6]`: Io/Ior 模式選擇
  - `phase_corr[6]`: 相位補正
  - `phase_type[6]`: 單相/三相

### 10. 模組初始化
**measure_init()** - 初始化測電模組
```c
int measure_init(void);
```

執行動作：
- ✅ 獲取 ATM1/2/3 設備指標
- ✅ 載入 meter_config 和 ai_config
- ✅ 初始化通道數據陣列

---

## ⏳ 待實現功能

### 1. CH7 (Ig) 讀取邏輯
- 目前標記為 `TODO`
- 需確認 Ig 的數據來源 (可能來自特定 ATM 通道或獨立 ADC)

### 2. 與 BLE GATT 整合
- 讀取測量數據並更新 BLE 特徵值
- 定期發送測量結果

### 3. 與 MQTT 整合
- 發送測量數據到雲端
- 漏電警報推送

### 4. 與 Flash 日誌整合
- 記錄異常測量值
- 保存歷史數據

---

## 📊 數據流程圖

```
ATM90E26 (x3) 
    ↓ sensor_sample_fetch()
    ↓ sensor_channel_get()
Raw Data (voltage, current, power, phase_angle)
    ↓ measure_read_raw_data()
CH1-6 原始數據
    ↓ measure_calculate_channel()
    ↓ (CT_ratio, CH_Igain 增益校正)
CH1-6 實際值 (V, A, W, θ)
    ↓ measure_calculate_ior()
    ↓ (相位補正, 單相/三相公式)
Ior 殘留電流
    ↓ measure_check_leak_alarm()
    ↓ (輕漏電/重漏電閾值判定)
警報等級 (0/1/2)
```

---

## 🔧 使用範例

### 範例 1: 定期測量
```c
/* 在 main.c 中每 5 秒執行 */
void measurement_timer_handler(struct k_timer *timer)
{
    measure_perform_cycle();
    
    /* 獲取 CH1 數據 */
    channel_data_t ch1_data;
    measure_get_channel_data(CH1, &ch1_data);
    
    printk("CH1: %.2f V, %.3f A, %.1f W, Io=%.1f mA, Ior=%.1f mA\n",
           ch1_data.voltage, ch1_data.current, ch1_data.power,
           ch1_data.io, ch1_data.ior);
    
    /* 檢查漏電 */
    int alarm = measure_check_leak_alarm(CH1, 30, 100);  // 30mA 輕, 100mA 重
    if (alarm > 0) {
        printk("CH1 leak alarm level: %d\n", alarm);
    }
}
```

### 範例 2: 三相計算
```c
three_phase_data_t phase_data;
int ret = measure_calculate_three_phase(CH1, CH3, &phase_data);
if (ret == 0) {
    printk("R-phase: %.3f A\n", phase_data.current_r);
    printk("S-phase: %.3f A (calculated)\n", phase_data.current_s);
    printk("T-phase: %.3f A\n", phase_data.current_t);
    printk("Total: %.3f A\n", phase_data.total_current);
}
```

---

## ✅ 驗證檢查清單

### 2.1 讀取原始數據 ✅
- [x] 從 ATM1 讀取 CH1/CH2
- [x] 從 ATM2 讀取 CH3/CH4
- [x] 從 ATM3 讀取 CH5/CH6
- [x] 錯誤處理與日誌

### 2.2 增益校正 ✅
- [x] CH1/CH2 公式 (隔離變壓器)
- [x] CH3-6 公式 (差動負載)
- [x] CT_ratio 與 CH_Igain 參數
- [x] 電壓/功率換算

### 2.3 三相合成 ✅
- [x] 極坐標 → 直角坐標
- [x] 向量加法 (S = -(R+T))
- [x] 直角坐標 → 極坐標 (模長)
- [x] 數學函數 (sin, cos, sqrt)

### 2.4 Ior 計算 ✅
- [x] 相位補正角度對應
- [x] 單相公式
- [x] 三相公式
- [x] cos(30°) 常數

### 2.5 完整流程 ✅
- [x] 讀取 → 計算 → Ior → 三相
- [x] 設定值快取
- [x] 條件判斷 (Ior 模式)

### 2.6 警報判定 ✅
- [x] Io/Ior 模式選擇
- [x] 輕/重漏電閾值
- [x] 返回值定義

---

## 🎯 下一步工作

1. **整合到 main.c**
   - 在 `main()` 中調用 `measure_init()`
   - 修改定時器改用 `measure_perform_cycle()`

2. **實現 CH7 (Ig) 讀取**
   - 確認硬體規格
   - 實現數據讀取

3. **更新 CMakeLists.txt**
   - 添加 `src/measure.c`
   - 確保 math.h 可用

4. **BLE GATT 設定修改**
   - 實現 8c02 (Meter Config) 讀寫
   - 實現 8c03 (Command) 擴展 OP codes

5. **測試驗證**
   - 單元測試各計算函數
   - 整合測試完整流程
   - 實際硬體驗證
