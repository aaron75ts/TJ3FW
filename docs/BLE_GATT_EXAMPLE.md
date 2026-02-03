# BLE GATT 設定值修改範例

本文件提供透過 BLE GATT 修改 TJ3 系統設定值的完整範例。

## 🔐 前置條件

**重要**: 在修改任何設定前，必須先長按設備上的 **"S" 按鈕 3 秒以上** 進入設定模式，否則設備會拒絕所有寫入操作。

## 📡 BLE GATT 服務架構

### 能源監控服務 (Energy Service)
**UUID**: `4d6f8c00-4b9a-4c1b-9a61-112233445500`

| 特徵值            | UUID 尾碼     | 權限             | 用途                        |
| ----------------- | ------------- | ---------------- | --------------------------- |
| Meter Snapshot    | `...8c01`     | READ + NOTIFY    | 即時數據快照 (20 bytes)     |
| **Meter Config**  | **`...8c02`** | **READ + WRITE** | **計量校正參數 (20 bytes)** |
| Command           | `...8c03`     | WRITE + INDICATE | 控制指令 (1-32 bytes)       |
| Power Meter Pulse | `...8c04`     | READ + NOTIFY    | 脈衝頻率                    |
| Alarm Status      | `...8c05`     | READ + NOTIFY    | 警報狀態                    |

---

## 範例 1: 修改計量校正參數 (Meter Config)

### 通訊方式
- **UUID**: `4d6f8c02-4b9a-4c1b-9a61-112233445500` (Meter Config 特徵值)
- **操作**: 直接 WRITE 20 bytes 原始數據
- **不需要封裝**: 無需 STX/ETX/CRC，直接寫入數據

### 數據格式 (24 bytes)

```
Byte 0-11:  CT Ratio (6 通道 × 2 bytes, Little Endian uint16_t)
Byte 12-23: CH Igain (6 通道 × 2 bytes, Little Endian uint16_t)
```

**注意**: 原設計為 20 bytes 但實際應為 24 bytes (12+12)，需要確認 BLE GATT 特徵值長度配置。

### Python 範例代碼 (使用 bleak 庫)

```python
import asyncio
from bleak import BleakClient

# TJ3 設備 MAC 地址
DEVICE_ADDRESS = "XX:XX:XX:XX:XX:XX"

# Meter Config 特徵值 UUID
METER_CONFIG_UUID = "4d6f8c02-4b9a-4c1b-9a61-112233445500"

async def write_meter_config():
    """修改計量校正參數"""
    
    # 構建 20 bytes 數據
    # CT Ratio: CH1=400, CH2=400, CH3=100, CH4=100, CH5=100, CH6=100
    ct_ratios = [
        400,  # CH1 (ATM1 L-line)
        400,  # CH2 (ATM1 N-line)
        100,  # CH3 (ATM2 L-line)
        100,  # CH4 (ATM2 N-line)
        100,  # CH5 (ATM3 L-line)
        100,  # CH6 (ATM3 N-line)
    ]
    
    # CH Igain: 每通道的校正值 (對應 ATM90E26 IgainL/IgainN 暫存器)
    ch_igains = [
        2579,  # CH1 (0x0A13 - 預設值)
        2579,  # CH2
        2579,  # CH3
        2579,  # CH4
        2579,  # CH5
        2579,  # CH6
    ]
    
    # 組裝數據包
    data = bytearray()
    
    # CT Ratio (12 bytes, Little Endian uint16_t)
    for ratio in ct_ratios:
        data.extend(ratio.to_bytes(2, byteorder='little'))
    
    # CH Igain (12 bytes, Little Endian uint16_t)
    for igain in ch_igains:
        data.extend(igain.to_bytes(2, byteorder='little'))
    
    # 連接設備並寫入
    async with BleakClient(DEVICE_ADDRESS) as client:
        print("Connected to TJ3 device")
        
        # 讀取當前配置
        current = await client.read_gatt_char(METER_CONFIG_UUID)
        print(f"Current config: {current.hex()}")
        
        # 寫入新配置
        await client.write_gatt_char(METER_CONFIG_UUID, bytes(data))
        print(f"Written new config: {data.hex()}")
        
        # 讀回確認
        new_config = await client.read_gatt_char(METER_CONFIG_UUID)
        print(f"Verified config: {new_config.hex()}")

# 執行
asyncio.run(write_meter_config())
```

### 十六進制範例 (24 bytes)

使用預設值 (CT Ratio=400/100, Igain=0x0A13):

```
原始數據 (24 bytes):
90 01  # CH1 CT=400 (0x0190, Little Endian)
90 01  # CH2 CT=400
64 00  # CH3 CT=100 (0x0064)
64 00  # CH4 CT=100
64 00  # CH5 CT=100
64 00  # CH6 CT=100
13 0A  # CH1 Igain=2579 (0x0A13, Little Endian)
13 0A  # CH2 Igain=2579
13 0A  # CH3 Igain=2579
13 0A  # CH4 Igain=2579
13 0A  # CH5 Igain=2579
13 0A  # CH6 Igain=2579

修改 CH1 (CT=500, Igain=2600):
F4 01  # CH1 CT=500 (0x01F4)
90 01  # CH2 CT=400
64 00  # CH3 CT=100
64 00  # CH4 CT=100
64 00  # CH5 CT=100
64 00  # CH6 CT=100
28 0A  # CH1 Igain=2600 (0x0A28, Little Endian)
13 0A  # CH2 Igain=2579
13 0A  # CH3 Igain=2579
13 0A  # CH4 Igain=2579
13 0A  # CH5 Igain=2579
13 0A  # CH6 Igain=2579
```

---

## 範例 2: 修改 AI 監控設定 (使用 Command 特徵值)

### 通訊方式
- **UUID**: `4d6f8c03-4b9a-4c1b-9a61-112233445500` (Command 特徵值)
- **操作**: WRITE + INDICATE (設備會回傳確認)
- **需要封裝**: `[STX] [LEN_L] [LEN_H] [OP_CODE] [DATA...] [ETX] [CRC1] [CRC2]`

### 封包格式

```
STX      = 0x02 (固定)
LEN_L    = 數據長度低位 (不含 STX/LEN/CRC)
LEN_H    = 數據長度高位
OP_CODE  = 0xB5 (AI 設定)
DATA     = 設定數據
ETX      = 0x03 (固定)
CRC1     = CRC 校驗碼低位
CRC2     = CRC 校驗碼高位
```

### Python 範例代碼

```python
import asyncio
from bleak import BleakClient
import struct

DEVICE_ADDRESS = "XX:XX:XX:XX:XX:XX"
COMMAND_UUID = "4d6f8c03-4b9a-4c1b-9a61-112233445500"

def calc_crc16(data):
    """計算 CRC-16 校驗碼"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

async def write_ai_config():
    """修改 AI 監控設定 (OP Code 0xB5)"""
    
    # AI 設定數據
    ai_data = bytearray()
    
    # 警報類型 (6 通道, ASCII)
    # '1'=Ior自動, '2'=Ior手動, '3'=Io
    alarm_types = b'111333'  # CH1-2: Ior自動, CH3-6: Io
    ai_data.extend(alarm_types)
    
    # 相位設定 (6 通道, ASCII)
    # '1'=單相, '3'=三相
    phase_types = b'111133'  # CH1-4: 單相, CH5-6: 三相
    ai_data.extend(phase_types)
    
    # 補正相位角 (6 通道, ASCII)
    # '0'=0°, '1'=30°, '2'=60°, '3'=90°, '4'=120°, '5'=150°
    phase_corrs = b'000011'  # CH1-4: 0°, CH5-6: 30°
    ai_data.extend(phase_corrs)
    
    # 漏電閥值和時間 (每通道 8 bytes, Little Endian)
    for ch in range(6):
        light_th = 100   # 輕漏電閥值 (mA)
        heavy_th = 500   # 重漏電閥值 (mA)
        on_time = 5      # 判定時間 (秒)
        off_time = 10    # 復歸時間 (秒)
        
        ai_data.extend(struct.pack('<HHHH', light_th, heavy_th, on_time, off_time))
    
    # ZCT 比值 (6 通道, ASCII)
    # '0'=4500:1, '1'=2000:1
    zct_ratios = b'110000'  # CH1-2: 2000:1, CH3-6: 4500:1
    ai_data.extend(zct_ratios)
    
    # 地絡 (Ig) 設定 (6 bytes, Little Endian)
    ig_threshold = 1000  # mA
    ig_on_time = 10      # 秒
    ig_off_time = 20     # 秒
    ai_data.extend(struct.pack('<HHH', ig_threshold, ig_on_time, ig_off_time))
    
    # 構建完整封包
    packet = bytearray()
    packet.append(0x02)  # STX
    
    # 計算長度 (OP_CODE + DATA + ETX)
    payload_len = 1 + len(ai_data) + 1
    packet.append(payload_len & 0xFF)  # LEN_L
    packet.append((payload_len >> 8) & 0xFF)  # LEN_H
    
    packet.append(0xB5)  # OP_CODE
    packet.extend(ai_data)
    packet.append(0x03)  # ETX
    
    # 計算 CRC (從 LEN_L 到 ETX)
    crc_data = packet[1:]
    crc = calc_crc16(crc_data)
    packet.append(crc & 0xFF)  # CRC1
    packet.append((crc >> 8) & 0xFF)  # CRC2
    
    # 連接並發送
    async with BleakClient(DEVICE_ADDRESS) as client:
        print("Connected to TJ3 device")
        
        # 訂閱 Indicate 接收確認
        def indication_handler(sender, data):
            print(f"Received indication: {data.hex()}")
        
        await client.start_notify(COMMAND_UUID, indication_handler)
        
        # 發送指令
        await client.write_gatt_char(COMMAND_UUID, bytes(packet))
        print(f"Sent AI config: {packet.hex()}")
        
        # 等待確認
        await asyncio.sleep(2)

asyncio.run(write_ai_config())
```

---

## 範例 3: 修改 MQTT 客戶端設定 (OP Code 0xA5)

```python
async def write_mqtt_config():
    """修改 MQTT 客戶端設定"""
    
    # MQTT 設定數據 (固定長度)
    mqtt_data = bytearray()
    
    # MQTT Client ID (固定 16 bytes, ASCII)
    client_id = b'TJ3_NODE_000001 '  # 補空格至 16 bytes
    mqtt_data.extend(client_id[:16])
    
    # MQTT Server URL (固定 80 bytes, ASCII)
    server_url = b'mqtt://broker.example.com:1883'
    mqtt_data.extend(server_url[:80].ljust(80, b'\x00'))  # 補 0 至 80 bytes
    
    # 構建封包
    packet = bytearray()
    packet.append(0x02)  # STX
    
    payload_len = 1 + len(mqtt_data) + 1
    packet.append(payload_len & 0xFF)
    packet.append((payload_len >> 8) & 0xFF)
    
    packet.append(0xA5)  # OP_CODE
    packet.extend(mqtt_data)
    packet.append(0x03)  # ETX
    
    crc = calc_crc16(packet[1:])
    packet.append(crc & 0xFF)
    packet.append((crc >> 8) & 0xFF)
    
    # 發送 (同上)
    # ...
```

---

## 範例 4: 讀取當前設定

```python
async def read_current_config():
    """讀取當前計量校正參數"""
    async with BleakClient(DEVICE_ADDRESS) as client:
        # 讀取 Meter Config
        config = await client.read_gatt_char(METER_CONFIG_UUID)
        
        # 解析數據
        ct_ratios = []
        for i in range(6):
            ratio = int.from_bytes(config[i*2:(i+1)*2], byteorder='little')
            ct_ratios.append(ratio)
        
        ch_igains = list(config[12:18])
        
        print("Current Configuration:")
        print("CT Ratios:", ct_ratios)
        print("CH Igains:", ch_igains)
        
        # 對應到通道
        channels = ["CH1 (ATM1 L)", "CH2 (ATM1 N)", 
                   "CH3 (ATM2 L)", "CH4 (ATM2 N)",
                   "CH5 (ATM3 L)", "CH6 (ATM3 N)"]
        
        for i, ch in enumerate(channels):
            print(f"{ch}: CT={ct_ratios[i]}, Igain={ch_igains[i]}")

asyncio.run(read_current_config())
```

---

## 🔍 驗證設定是否生效

修改設定後，可透過以下方式驗證：

1. **讀回確認**: 立即讀取特徵值，確認數據已更新
2. **檢查 Flash**: 設定會自動保存到 `/lfs/settings.txt`
3. **觀察測量值**: 電流計算公式會使用新的 CT Ratio 和 Igain
4. **重啟測試**: 重啟設備後確認設定持久化成功

---

## 📱 使用工具

### 推薦的 BLE 測試工具
- **nRF Connect** (Android/iOS) - Nordic 官方 App
- **LightBlue** (iOS) - 簡單易用的 BLE 掃描工具
- **Bluetooth LE Explorer** (Windows) - 微軟官方工具
- **Python bleak** - 跨平台自動化腳本

### 測試步驟
1. 長按設備 "S" 按鈕 3 秒
2. 使用工具連接到 TJ3 設備
3. 找到對應的 Service UUID
4. 寫入或讀取特徵值
5. 驗證結果

---

## ⚠️ 注意事項

1. **安全限制**: 必須先按 "S" 按鈕才能修改
2. **數據格式**: 注意 Little Endian / Big Endian
3. **長度限制**: Meter Config 固定 20 bytes
4. **CRC 校驗**: Command 封包需正確計算 CRC-16
5. **持久化**: 修改後會自動保存到 Flash

---

## 📚 相關文件

- [BLE_UUID.md](BLE_UUID.md) - BLE UUID 定義
- [BLE_SETTING.md](BLE_SETTING.md) - 詳細封包格式
- [settings_example.txt](settings_example.txt) - Flash 設定範例
- [SETTINGS_4.md](SETTINGS_4.md) - 完整設定值清單
