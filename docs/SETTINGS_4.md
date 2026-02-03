# 設定值說明文件
## 📋 完整設定值清單

### 1️⃣ **AI 監控設定** (`ai_config_t`) - OP Code 0xB5

#### 每通道配置 (CH1-CH6，共 6 個通道)
- **`alarm_type[6]`** - 警報類型
  - `ALARM_TYPE_IO = 3` (Io 模式)
  - `ALARM_TYPE_IOR_AUTO = 1` (Ior 自動模式)
  - `ALARM_TYPE_IOR_MANUAL = 2` (Ior 手動模式)

- **`phase_type[6]`** - 相位設定
  - `PHASE_SINGLE = 1` (單相)
  - `PHASE_THREE = 3` (三相)

- **`phase_corr[6]`** - 補正相位角 (0° ~ 150°, 每30度一檔)
  - 0: 0°
  - 1: 30°
  - 2: 60°
  - 3: 90°
  - 4: 120°
  - 5: 150°

- **`zct_ratio[6]`** - ZCT 比值
  - 0: 4500:1
  - 1: 2000:1

- **`light_leak_th[6]`** - 輕漏電閥值 (mA)
- **`heavy_leak_th[6]`** - 重漏電閥值 (mA)
- **`leak_on_time[6]`** - 漏電判定時間 (秒)
- **`leak_off_time[6]`** - 漏電復歸時間 (秒)

#### 地絡 (Ig) 設定
- **`ig_threshold`** - 地絡閥值 (mA)
- **`ig_on_time`** - 地絡判定時間 (秒)
- **`ig_off_time`** - 地絡復歸時間 (秒)

---

### 2️⃣ **需量與 Modem 設定** (`demand_config_t`) - OP Code 0xE5

#### 需量警報
- **`demand_alarm1`** - 目標電力 (kW)
- **`demand_alarm2`** - 限界電力 (kW)

#### 脈衝係數
- **`pulse_const`** - 脈衝係數 (Little Endian, 32-bit)

#### Modem 設定
- **`modem_ip[4]`** - Modem IP
- **`apn[64]`** - APN 名稱

---

### 3️⃣ **MQTT 客戶端設定** (`mqtt_config_t`) - OP Code 0xA5

#### 連線設定
- **`server_url[81]`** - MQTT Server URL (80 bytes + null)
  - 支援 URL 格式: `mqtt://192.168.1.100:1883`
  - 支援協定: `mqtt://`, `mqtts://`, `ws://`, `wss://`
- **`server_port`** - MQTT 伺服器 Port (Little Endian, 可選)

#### 認證資訊
- **`client_id[17]`** - MQTT Client ID (16 bytes + null)
- **`username[33]`** - MQTT Username (32 bytes + null)
- **`password[33]`** - MQTT Password (32 bytes + null)

---

### 4️⃣ **設備資訊** (`device_info_t`) - OP Code 0xD5
- **`device_name[21]`** - 設備名稱 (20 bytes + null)
- **`location[61]`** - 位置名稱 (60 bytes + null)

---

### 5️⃣ **計量校正參數** (`meter_config_t`) - BLE UUID 8c02
- **`ct_ratio[6]`** - CT 變流器比例 (每通道)
- **`ch_igain[6]`** - 通道增益校正值 (每通道)

---

## 📊 設定值統計

| 分類          | 參數數量                   | 存取方式           |
| ------------- | -------------------------- | ------------------ |
| AI 監控設定   | 39個 (6通道×6項 + 地絡3項) | BLE OP 0xB5 / MQTT |
| 需量與 Modem  | 5個                        | BLE OP 0xE5 / MQTT |
| MQTT 連線設定 | 5個                        | BLE OP 0xA5        |
| 設備資訊      | 2個                        | BLE OP 0xD5        |
| 計量校正      | 12個 (6通道×2項)           | BLE UUID 8c02      |
| **總計**      | **63個獨立參數**           | -                  |

## 💾 儲存位置

- **External SPI Flash** (64Mbit)
  - 長期儲存所有設定值
  - 檔案格式: `settings.txt` (key-value)
  - 歷史數據: CSV 格式

- **ATM90E26 暫存器**
  - 計量校正參數 (21H-2BH, 31H-3AH)
  - 需在系統啟動時從 Flash 重新載入

## 🔒 存取控制

- **硬體保護**: 修改需長按 "S" 按鈕 3 秒
- **校驗保護**: CS1 (2CH) 和 CS2 (3BH) checksum
- **啟動檢查**: 整合性驗證 (Consistency Check)
- **停電保存**: 關鍵數據自動寫入 Flash