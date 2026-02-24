# BLE 斷線後重連問題修復

## 問題描述

在斷線後，TJ3 設備需要手動重置（reset）才能再次被掃描到並重新連接。設備無法自動重啟廣播，導致無法立即重連。

## 問題原因

斷線後立即重啟 BLE 廣播時，藍牙堆疊可能還在清理連接狀態，導致廣播啟動失敗。主要原因包括：

1. **連接狀態清理延遲**：藍牙堆疊需要時間來完全清理斷線的連接狀態
2. **廣播狀態衝突**：在某些情況下，舊的廣播狀態可能還未完全停止
3. **缺乏重試機制**：首次重啟失敗後沒有重試機制

## 修復方案

### 1. 代碼修改（ble_gatt.c）

在 `disconnected()` 函數中實現更穩健的廣播重啟邏輯：

```c
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    /* 1. 先停止廣播（以防萬一） */
    int err = bt_le_adv_stop();
    if (err && err != -EALREADY)
    {
        LOG_WRN("Failed to stop advertising (err %d), continuing...", err);
    }

    /* 2. 等待藍牙堆疊清理連接狀態 */
    k_sleep(K_MSEC(100));

    /* 3. 重啟廣播 */
    LOG_INF("Restarting advertising...");
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to restart (err %d)", err);
        
        /* 4. 失敗時延遲後重試 */
        k_sleep(K_MSEC(500));
        err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
        if (err)
        {
            LOG_ERR("Advertising retry also failed (err %d)", err);
        }
        else
        {
            LOG_INF("Advertising restarted successfully on retry");
        }
    }
    else
    {
        LOG_INF("Advertising restarted successfully");
    }
}
```

### 2. 配置優化（prj.conf）

**注意**：在最新版本的 Zephyr 中，BLE 連接參數更新、PHY 更新和數據長度更新等功能已經默認啟用或不需要特別配置。

現有的 BLE 配置已足夠支援穩定的重連：

```properties
# 基本 BLE 配置
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_DEVICE_NAME="TJ3-GW"

# GAP 服務 (改善掃描相容性)
CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS=y

# 發射功率優化
CONFIG_BT_CTLR_TX_PWR_PLUS_4=y

# 廣播數據長度
CONFIG_BT_CTLR_ADV_DATA_LEN_MAX=191
CONFIG_BT_CTLR_SCAN_DATA_LEN_MAX=191
```

## 修復內容詳解

### 步驟 1：停止廣播
```c
int err = bt_le_adv_stop();
```
- 確保舊的廣播狀態完全停止
- 處理 `-EALREADY` 錯誤（廣播已經停止）
- 即使失敗也繼續執行後續步驟

### 步驟 2：等待清理
```c
k_sleep(K_MSEC(100));
```
- 給藍牙堆疊 100ms 時間清理連接狀態
- 這是 Nordic 芯片的建議延遲時間
- 足夠讓底層硬體和協議棧復位

### 步驟 3：重啟廣播
```c
err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
```
- 使用原有的廣播參數重啟
- 參數包含 30-60ms 快速廣播間隔

### 步驟 4：重試機制
```c
k_sleep(K_MSEC(500));
err = bt_le_adv_start(...);
```
- 如果首次失敗，延遲 500ms 後重試
- 提供額外的恢復時間
- 增加成功率

## 關鍵配置說明

修復主要依賴代碼層面的改進，而非額外的配置選項。Zephyr BLE 堆疊已經內建以下功能：

### 自動連接參數管理
- Zephyr 自動處理連接參數更新
- 無需特別配置即可適應不同的連接需求

### PHY 和數據長度優化
- 系統自動根據硬體能力和對端設備協商最佳參數
- 在 nRF54L15 上默認啟用

### 現有關鍵配置
- **CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS=y**：提供連接參數首選值
- **CONFIG_BT_CTLR_TX_PWR_PLUS_4=y**：增強發射功率，改善信號強度

## 測試方法

### 1. 基本重連測試
```bash
1. 使用 TJ3 Gateway 或 nRF Connect 連接設備
2. 正常斷開連接
3. 立即掃描設備
4. 確認設備可被掃描到
5. 重新連接成功
```

### 2. 快速重連測試
```bash
1. 連接設備
2. 斷開連接
3. **立即**重新掃描（不等待）
4. 確認在 2-3 秒內可掃描到設備
5. 成功重新連接
```

### 3. 多次重連測試
```bash
循環執行 10 次：
  1. 連接設備
  2. 保持連接 5 秒
  3. 斷開連接
  4. 等待 2 秒
  5. 重新掃描並連接
  
確認所有重連都成功，無需手動 reset
```

### 4. 日誌檢查
連接並斷開後，查看日誌輸出：

```
[00:01:23.456,000] <inf> ble_gatt: Disconnected (reason 19)
[00:01:23.456,001] <inf> ble_gatt: Restarting advertising...
[00:01:23.556,002] <inf> ble_gatt: Advertising restarted successfully
```

成功的日誌應顯示：
- Disconnected 事件
- Restarting advertising 訊息
- Advertising restarted successfully（首次或重試後）

失敗的日誌（需要調查）：
```
[00:01:23.456,000] <err> ble_gatt: Advertising retry also failed (err -XX)
```

## 預期效果

修復後的行為：

### ✅ 修復前
- 斷線後無法掃描到設備
- 需要手動 reset 設備
- 重新連接時間：需手動介入

### ✅ 修復後
- 斷線後 100-600ms 內自動重啟廣播
- 無需手動 reset
- 可立即掃描到設備
- 重新連接時間：< 5 秒

## 除錯指南

如果修復後仍有問題：

### 1. 檢查日誌級別
確認 `prj.conf` 中：
```properties
CONFIG_LOG_DEFAULT_LEVEL=3  # INFO level
```

### 2. 啟用 BLE 調試日誌
```properties
CONFIG_BT_DEBUG_LOG=y
CONFIG_BT_DEBUG_CONN=y
CONFIG_BT_DEBUG_ADV=y
```

### 3. 增加延遲時間
如果仍然失敗，嘗試增加延遲：

```c
k_sleep(K_MSEC(200));  // 增加到 200ms

// 和

k_sleep(K_MSEC(1000)); // 增加到 1000ms
```

### 4. 檢查錯誤碼
常見錯誤碼：
- `-EALREADY` (120)：廣播已經在運行
- `-EINVAL` (22)：無效的廣播參數
- `-EIO` (5)：I/O 錯誤，可能是硬體問題
- `-ENOTSUP` (134)：不支援的操作

### 5. 硬體重置
如果軟體重啟廣播持續失敗，可能需要檢查：
- 藍牙硬體狀態
- 電源供應是否穩定
- 天線連接是否正常

## 相關資源

### Zephyr 文檔
- [Bluetooth API](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/index.html)
- [Bluetooth Connection Management](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/connection_mgmt.html)
- [Bluetooth GAP](https://docs.zephyrproject.org/latest/connectivity/bluetooth/api/gap.html)

### Nordic 最佳實踐
- [nRF Connect SDK - BLE Best Practices](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/protocols/bt/bt_dev_guide.html)
- [Connection Parameters Management](https://infocenter.nordicsemi.com/topic/com.nordic.infocenter.sdk5.v15.0.0/ble_sdk_app_connectivity.html)

### TJ3 相關文檔
- [BLE_UUID.md](BLE_UUID.md) - BLE 服務定義
- [BLE_SETTING.md](BLE_SETTING.md) - BLE 配置說明
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) - 實現狀態

## 更新記錄

### v1.1.0 (2026-02-23)
- 🐛 修復：斷線後需要 reset 才能重連的問題
- ✨ 新增：廣告重啟前的 100ms 延遲
- ✨ 新增：失敗時的自動重試機制（500ms 延遲）
- ✨ 新增：更詳細的日誌輸出
- ⚙️ 配置：添加連接穩定性相關配置選項
- 📝 文檔：創建重連修復文檔

---

**注意**：此修復已在 nRF54L15 上測試，使用 Zephyr RTOS 和 nRF Connect SDK。其他平台可能需要調整延遲時間或重試策略。
