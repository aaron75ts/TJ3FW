# DI (Digital Input) 監測模組

本模組實現 TJ3-nRF 系統的接點輸入監測功能，根據開發式樣書規範實現。

## 硬體配置

- **DI1**: P2.06 (GPIO 2-6)
- **DI2**: P2.07 (GPIO 2-7)
- 輸入模式: 帶上拉電阻的數位輸入
- 觸發電平: HIGH (1) = 警報狀態

## 功能特性

### 1. 狀態監測與判定
- 每秒輪詢 DI1 和 DI2 的狀態
- 支持可配置的判定時間 (1-999 秒)
- 自動過濾雜訊，避免誤報
- 分別支持警報判定時間 (ON_TIME) 和復歸判定時間 (OFF_TIME)

### 2. 警報通報流程
當接點輸入變為 HIGH 並持續超過判定時間後：
1. 發送 MQTT 通報 (識別碼 AD)
   - Topic: `kyokuto/<MainID>/<PeriID>/AD`
   - Payload 包含通道號碼和狀態值
2. 更新 BLE Alarm Status 特徵值
   - UUID: ...8c05
   - BIT_6: DI1 狀態
   - BIT_7: DI2 狀態
3. 記錄到 Flash 記憶體
   - 1 分鐘值記錄 (90 天)
   - 30 分鐘值記錄 (1 年)

### 3. 停電保護
- 當檢測到停電 (P1.03)，優先保存 DI 狀態到 Flash
- 復電後自動載入保存的狀態

## API 使用

### 初始化
```c
#include "di.h"

int main(void) {
    // 初始化 DI 監測模組
    di_init();
    
    // ... 其他初始化
}
```

### 獲取狀態
```c
// 讀取 DI1 當前狀態
di_state_t state1 = di_get_state(DI_CHANNEL_1);
if (state1 == DI_STATE_ALERT) {
    // DI1 處於警報狀態
}

// 讀取 DI2 當前狀態
di_state_t state2 = di_get_state(DI_CHANNEL_2);
```

### 配置判定時間
```c
// 設定 DI1: 警報判定時間 5 秒，復歸判定時間 3 秒
di_set_config(DI_CHANNEL_1, 5, 3);

// 設定 DI2: 警報判定時間 10 秒，復歸判定時間 5 秒
di_set_config(DI_CHANNEL_2, 10, 5);
```

### 讀取配置
```c
di_config_t config;
di_get_config(DI_CHANNEL_1, &config);
printk("DI1 ON_TIME: %d, OFF_TIME: %d\n", config.on_time, config.off_time);
```

### 停電保護
```c
// 在停電檢測中斷中調用
void power_off_handler(void) {
    // 保存 DI 狀態到 Flash
    di_save_state_to_flash();
    
    // ... 保存其他數據
}

// 在復電或系統啟動時調用
void power_on_handler(void) {
    // 從 Flash 載入 DI 狀態
    di_load_state_from_flash();
}
```

## BLE GATT 設定

透過 BLE 設定判定時間需要：

1. **解鎖設備**: 長按 "S" 按鈕 3 秒以上
2. **寫入 Command 特徵值** (UUID: 4d6f8c03-...)
3. **使用 OP Code 0xB5** (AI 設定變更)
4. **封包格式**:
   ```
   [OP_CODE][DI1_ON_TIME_H][DI1_ON_TIME_L][DI1_OFF_TIME_H][DI1_OFF_TIME_L]
           [DI2_ON_TIME_H][DI2_ON_TIME_L][DI2_OFF_TIME_H][DI2_OFF_TIME_L]
   ```
   - 時間單位: 秒 (2-byte HEX)
   - 範圍: 1-999 秒

## 實現細節

### 狀態機
每個 DI 通道維護以下狀態：
- `current_state`: GPIO 當前讀取值
- `last_stable_state`: 上次判定成功的穩定狀態
- `countdown`: 判定倒數計時
- `pending_alert`: 待判定警報標誌
- `pending_restore`: 待判定復歸標誌

### 輪詢機制
使用 Zephyr Timer 每秒觸發一次：
1. 讀取 GPIO 狀態
2. 檢測狀態變化
3. 更新倒數計時
4. 判定時間到達時執行通報

### TODO 項目
以下功能已預留接口，需要與其他模組整合：
- [ ] MQTT 發送實現 (需要整合 ext_comm 模組)
- [ ] BLE 狀態更新實現 (需要整合 ble_gatt 模組)
- [ ] Flash 記錄實現 (需要整合 fs_handler 模組)
- [ ] 停電檢測整合 (需要 P1.03 GPIO 中斷)

## 測試建議

### 功能測試
1. 短接 P2.06 或 P2.07 到 VDD，驗證警報觸發
2. 驗證判定時間機制 (在判定時間內取消訊號不應觸發警報)
3. 測試復歸機制
4. 驗證 BLE 設定功能

### 壓力測試
1. 快速切換輸入狀態，驗證雜訊過濾
2. 長時間運行，驗證計時器穩定性
3. 模擬停電，驗證狀態保存

## 日誌輸出

模組使用 LOG_MODULE_REGISTER(di) 註冊日誌：
```
[00:00:10.123,000] <inf> di: Initializing DI monitoring...
[00:00:10.125,000] <inf> di: DI1 initialized (P2.06)
[00:00:10.127,000] <inf> di: DI2 initialized (P2.07)
[00:00:10.129,000] <inf> di: DI monitoring initialized successfully
[00:00:15.000,000] <dbg> di: DI1: Alert signal detected, countdown: 3 sec
[00:00:18.000,000] <wrn> di: DI1 Alert!
```

## 參考文檔

- [docs/DI_1.md](../docs/DI_1.md) - 警報判定與通報流程
- [docs/DI_2.md](../docs/DI_2.md) - 處置步驟與重點
- [docs/DI_3.md](../docs/DI_3.md) - App 設定與停電保護
