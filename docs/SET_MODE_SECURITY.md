# BLE 設定模式安全機制

## 概述

為了防止未授權的 BLE 修改，TJ3FW 實現了基於按鈕長按的設定模式安全機制。

## 設計原則

**只有長按本機的 'S' 按鈕 3 秒以上，才能透過 BLE 修改設定值。**

### 按鈕功能

- **按鈕位置**: 設備上的 'S' 按鈕 (GPIO P1.02，Active Low)
- **長按觸發**: 持續按下 3 秒進入設定模式
- **視覺反饋**: LED 快速閃爍 (5Hz) 表示設定模式已啟用
- **自動超時**: 30 秒後自動退出設定模式，恢復正常 LED 模式

### 安全檢查位置

所有 BLE 設定寫入操作都會檢查設定模式狀態：

1. **Meter Config 特性 (UUID: 4d6f8c02...)**
   - WRITE handler: `write_meter_config()`
   - 檢查: `button_is_set_mode_enabled()`
   - 拒絕時返回: `BT_ATT_ERR_AUTHORIZATION (0x03)`

2. **Command 特性 - OP_BLE_SET_AI_CONFIG (0x21)**
   - Protocol handler: `protocol_handler()`
   - 檢查: `button_is_set_mode_enabled()`
   - 拒絕時返回: 錯誤碼 `0xFE` (授權錯誤)

## 實現架構

### 模組結構

```
inc/button.h              - 按鈕模組 API
src/button.c              - 按鈕狀態檢測和設定模式管理
src/ble_gatt.c            - BLE 寫入回調中的安全檢查
src/main.c                - 初始化按鈕模組
```

### 狀態管理

```c
/* 設定模式狀態 */
static bool set_mode_enabled = false;      /* 設定模式開關 */
static int32_t set_mode_timeout = 0;       /* 超時倒數 (秒) */

/* 按鈕狀態 */
static bool button_pressed = false;        /* 按鈕按下狀態 */
static int32_t press_duration = 0;         /* 按下持續時間 (100ms單位) */
```

### 定時器機制

1. **按鈕輪詢定時器**: 每 100ms 檢測一次按鈕狀態
   - 累計按下時間
   - 達到 3 秒時進入設定模式

2. **設定模式超時定時器**: 每 1 秒觸發
   - 倒數計時
   - 到 0 時自動退出設定模式

## API 使用

### 檢查設定模式狀態

```c
#include "button.h"

if (!button_is_set_mode_enabled())
{
    LOG_WRN("Set Mode not enabled");
    return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
}

/* 執行設定修改操作 */
```

### 手動控制 (測試用)

```c
/* 進入設定模式 30 秒 */
button_enter_set_mode(30);

/* 立即退出設定模式 */
button_exit_set_mode();
```

## BLE 錯誤碼

| 錯誤碼 | 名稱                       | 說明                    |
| ------ | -------------------------- | ----------------------- |
| 0x03   | BT_ATT_ERR_AUTHORIZATION   | Meter Config 寫入被拒絕 |
| 0xFE   | Custom Authorization Error | Command 封包寫入被拒絕  |

## 使用流程

### 正常修改設定流程

```
1. 用戶連接 BLE
2. 嘗試寫入設定 → 收到授權錯誤 (0x03 或 0xFE)
3. 用戶長按 'S' 按鈕 3 秒
4. LED 開始快速閃爍 (設定模式已啟用)
5. 再次嘗試寫入設定 → 成功
6. 30 秒後自動超時
7. LED 恢復正常閃爍
8. 再次嘗試寫入 → 收到授權錯誤
```

### 時序圖

```
用戶              設備                    BLE Client
 |                |                           |
 |                |<------ Write Config ------|
 |                |------- Error 0xFE ------->|
 |                |                           |
 | 長按 3 秒       |                           |
 |------------>   |                           |
 |             LED 快速閃爍                   |
 |                |<------ Write Config ------|
 |                |------- Success ---------->|
 |                |                           |
 | (30 秒後)      |                           |
 |             LED 正常閃爍                   |
 |                |<------ Write Config ------|
 |                |------- Error 0xFE ------->|
```

## 測試方法

### Python 測試腳本

使用提供的測試腳本驗證安全機制:

```bash
cd docs/sample_code
python3 test_set_mode_security.py
```

測試腳本會:
1. 嘗試未授權修改 → 確認被拒絕
2. 提示用戶按按鈕
3. 嘗試授權修改 → 確認成功
4. 測試 Meter Config 寫入

### 手動測試步驟

1. 連接 BLE (使用 nRF Connect 或自定義 App)
2. 嘗試寫入 Meter Config (4d6f8c02...) → 應收到錯誤 0x03
3. 長按設備上的 'S' 按鈕 3 秒
4. 觀察 LED 開始快速閃爍
5. 再次嘗試寫入 Meter Config → 應成功
6. 等待 30 秒
7. 觀察 LED 恢復正常閃爍
8. 再次嘗試寫入 → 應收到錯誤 0x03

## 配置參數

可在 [src/button.c](../src/button.c) 中修改:

```c
#define LONG_PRESS_DURATION 3   /* 長按秒數 (預設 3 秒) */
#define SET_MODE_TIMEOUT 30     /* 自動超時秒數 (預設 30 秒) */
```

## Device Tree 配置

按鈕 GPIO 目前硬編碼為 P1.02，未來需要在 DTS 中定義:

```dts
/ {
    buttons {
        compatible = "gpio-keys";
        set_button: set_button {
            gpios = <&gpio1 2 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
            label = "Set Mode Button";
        };
    };
};
```

## 安全性考量

### 優點
- ✅ 防止遠端未授權修改
- ✅ 需要物理訪問設備才能修改設定
- ✅ 自動超時防止長時間開放
- ✅ LED 視覺反饋讓用戶知道當前狀態

### 限制
- ⚠️ 如果攻擊者有物理訪問權限，仍可按按鈕
- ⚠️ BLE 連接本身沒有加密/配對要求

### 建議增強措施
1. 增加 BLE 配對要求 (Pairing/Bonding)
2. 實現 PIN 碼驗證
3. 記錄所有設定修改操作到日誌
4. 在超時前 10 秒加快 LED 閃爍提醒

## 相關文件

- [SETTINGS_1.md](SETTINGS_1.md) - 設定值說明
- [BLE_GATT_EXAMPLE.md](BLE_GATT_EXAMPLE.md) - BLE GATT 使用範例
- [test_set_mode_security.py](sample_code/test_set_mode_security.py) - 安全測試腳本
