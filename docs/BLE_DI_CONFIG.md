# BLE GATT DI 延時設定實現指南

## 概述

根據開發式樣書，透過 BLE GATT 設定 DI1/DI2 的判定時間需要使用 **OP Code 0xB5** (AI 設定變更指令)。

## 設定流程

### 1. 解鎖設備
使用者必須先長按設備本體的 **"S" (Set Mode) 按鈕 3 秒以上**，藍牙端才能獲取寫入權限。

### 2. 發送設定指令
App 對 **Command 特徵值** (UUID: `4d6f8c03-4b9a-4c1b-9a61-112233445500`) 發起 **WRITE** 操作。

### 3. 封包格式

使用 Protocol.h 中定義的封包格式：

```
STX + OP_CODE + PAYLOAD_LEN + PAYLOAD + CHECKSUM + ETX
```

#### OP Code 0xB5 - AI 設定變更

**Payload 結構** (8 bytes):
```
Byte 0-1: DI1_ON_TIME  (2-byte, Little Endian / LSB first, 單位：秒)
Byte 2-3: DI1_OFF_TIME (2-byte, Little Endian / LSB first, 單位：秒)
Byte 4-5: DI2_ON_TIME  (2-byte, Little Endian / LSB first, 單位：秒)
Byte 6-7: DI2_OFF_TIME (2-byte, Little Endian / LSB first, 單位：秒)
```

**範圍**: 1-999 秒

#### 範例封包

設定 DI1 (ON=5秒, OFF=3秒), DI2 (ON=10秒, OFF=5秒):

```
STX:  0x02
OP:   0xB5
LEN:  0x08
DATA: 0x05 0x00  (DI1_ON_TIME = 5, LSB first)
      0x03 0x00  (DI1_OFF_TIME = 3, LSB first)
      0x0A 0x00  (DI2_ON_TIME = 10, LSB first)
      0x05 0x00  (DI2_OFF_TIME = 5, LSB first)
CRC:  [計算得出]
ETX:  0x03
```

### 4. 確認機制
設備收到後會透過 **INDICATE** 將原封包 Echo 回傳，作為 ACK 確認。

## 程式碼實現

### Step 1: 在 protocol.h 中添加 OP Code 定義

```c
// inc/protocol.h
#define OP_BLE_SET_DI_CONFIG    0xB5  // AI 設定變更 (DI 判定時間)
```

### Step 2: 在 ble_gatt.c 的 protocol_handler 中添加處理邏輯

```c
// src/ble_gatt.c
#include "di.h"  // 添加 DI 模組標頭檔

// 在 protocol_handler() 函數的 switch 中添加：
case OP_BLE_SET_DI_CONFIG:
    LOG_INF("CMD: Set DI Config");
    {
        // 檢查 payload 長度
        if (packet->payload_len != 8) {
            LOG_ERR("Invalid DI config payload length: %d", packet->payload_len);
            uint8_t error = 0xFF;
            tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
            break;
        }

        // 解析 DI1 設定 (Little Endian / LSB first)
        uint16_t di1_on_time = packet->payload[0] | (packet->payload[1] << 8);
        uint16_t di1_off_time = packet->payload[2] | (packet->payload[3] << 8);
        
        // 解析 DI2 設定 (Little Endian / LSB first)
        uint16_t di2_on_time = packet->payload[4] | (packet->payload[5] << 8);
        uint16_t di2_off_time = packet->payload[6] | (packet->payload[7] << 8);

        LOG_INF("DI1: ON=%d, OFF=%d", di1_on_time, di1_off_time);
        LOG_INF("DI2: ON=%d, OFF=%d", di2_on_time, di2_off_time);

        // 設定 DI1
        int rc1 = di_set_config(DI_CHANNEL_1, di1_on_time, di1_off_time);
        
        // 設定 DI2
        int rc2 = di_set_config(DI_CHANNEL_2, di2_on_time, di2_off_time);

        // 檢查結果
        if (rc1 == 0 && rc2 == 0) {
            // 成功：Echo 回原始 payload
            tx_len = protocol_compose(op_code, packet->payload, 
                                     packet->payload_len, tx_buf, sizeof(tx_buf));
            LOG_INF("DI config updated successfully");
        } else {
            // 失敗：回傳錯誤碼
            uint8_t error = 0xFF;
            tx_len = protocol_compose(op_code, &error, 1, tx_buf, sizeof(tx_buf));
            LOG_ERR("Failed to update DI config: rc1=%d, rc2=%d", rc1, rc2);
        }
    }
    break;
```

### Step 3: (可選) 添加讀取 DI 配置的 OP Code

如果需要讀取當前配置，可以添加一個對應的讀取指令：

```c
#define OP_BLE_GET_DI_CONFIG    0xB6  // 讀取 DI 配置

case OP_BLE_GET_DI_CONFIG:
    LOG_INF("CMD: Get DI Config");
    {
        di_config_t di1_cfg, di2_cfg;
        
        di_get_config(DI_CHANNEL_1, &di1_cfg);
        di_get_config(DI_CHANNEL_2, &di2_cfg);
        
        uint8_t response[8];
        // Little Endian / LSB first
        response[0] = di1_cfg.on_time & 0xFF;
        response[1] = (di1_cfg.on_time >> 8) & 0xFF;
        response[2] = di1_cfg.off_time & 0xFF;
        response[3] = (di1_cfg.off_time >> 8) & 0xFF;
        response[4] = di2_cfg.on_time & 0xFF;
        response[5] = (di2_cfg.on_time >> 8) & 0xFF;
        response[6] = di2_cfg.off_time & 0xFF;
        response[7] = (di2_cfg.off_time >> 8) & 0xFF;
        
        tx_len = protocol_compose(op_code, response, 8, tx_buf, sizeof(tx_buf));
    }
    break;
```

## App 端實現範例 (Swift/iOS)

```swift
// 設定 DI1 和 DI2 的判定時間
func setDIConfig(di1On: UInt16, di1Off: UInt16, 
                 di2On: UInt16, di2Off: UInt16) {
    var payload = Data()
    
    // DI1 ON_TIME (Little Endian / LSB first)
    payload.append(UInt8(di1On & 0xFF))
    payload.append(UInt8((di1On >> 8) & 0xFF))
    
    // DI1 OFF_TIME (Little Endian / LSB first)
    payload.append(UInt8(di1Off & 0xFF))
    payload.append(UInt8((di1Off >> 8) & 0xFF))
    
    // DI2 ON_TIME (Little Endian / LSB first)
    payload.append(UInt8(di2On & 0xFF))
    payload.append(UInt8((di2On >> 8) & 0xFF))
    
    // DI2 OFF_TIME (Little Endian / LSB first)
    payload.append(UInt8(di2Off & 0xFF))
    payload.append(UInt8((di2Off >> 8) & 0xFF))
    
    // 組裝完整封包
    let packet = composePacket(opCode: 0xB5, payload: payload)
    
    // 寫入 Command 特徵值
    peripheral.writeValue(packet, 
                         for: commandCharacteristic, 
                         type: .withResponse)
}

// 監聽 Indication 回應
func peripheral(_ peripheral: CBPeripheral, 
                didUpdateValueFor characteristic: CBCharacteristic, 
                error: Error?) {
    if characteristic.uuid == commandUUID {
        guard let data = characteristic.value else { return }
        
        // 解析回應封包
        let packet = parsePacket(data)
        
        if packet.opCode == 0xB5 {
            if packet.payload.count == 8 {
                print("DI config set successfully!")
            } else if packet.payload.first == 0xFF {
                print("DI config set failed!")
            }
        }
    }
}
```

## 測試步驟

### 1. 使用 nRF Connect 測試

1. 連接到設備
2. 找到 Energy Service (4d6f8c00-...)
3. 找到 Command 特徵值 (4d6f8c03-...)
4. 啟用 Indication
5. 寫入封包：`02 08 00 B5 05 00 03 00 0A 00 05 00 03 [CRC]` (LSB first)
6. 等待 Indication 回應

### 2. 驗證設定生效

```c
// 在設備端讀取配置驗證
di_config_t config;
di_get_config(DI_CHANNEL_1, &config);
printk("DI1: ON=%d, OFF=%d\n", config.on_time, config.off_time);
```

## 注意事項

1. **權限控制**: 實際產品中需要實現 "S" 按鈕解鎖機制
2. **持久化**: 設定後應保存到 Flash，重啟後恢復
3. **範圍檢查**: di_set_config() 已實現 1-999 秒範圍檢查
4. **錯誤處理**: 處理無效參數和設定失敗的情況
5. **原子操作**: 兩個通道的設定應該是原子操作（全成功或全失敗）

## 相關文檔

- [docs/DI_3.md](DI_3.md) - App 設定與停電保護
- [inc/di.h](../inc/di.h) - DI 模組 API
- [inc/protocol.h](../inc/protocol.h) - Protocol OP Code 定義
