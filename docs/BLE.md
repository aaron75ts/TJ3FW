
# BLE 透過模式指令 (OP_CODE)

## BLE GATT 服務架構

本系統採用 TJ3-nRF 規格書定義的 **能源監控服務 (Energy Service)**：

- **Service UUID**: `4d6f8c00-4b9a-4c1b-9a61-112233445500`
- **Command Characteristic UUID**: `4d6f8c03-4b9a-4c1b-9a61-112233445500`
  - **屬性**: WRITE + INDICATE
  - **功能**: 手機 APP 寫入指令，設備以 Indicate 方式回傳應答（Echo 確認機制）
  - **封包格式**: `[STX(0x02)] [LEN_L] [LEN_H] [OP_CODE] [DATA...] [ETX(0x03)] [CRC_L] [CRC_H]`

在 BLE 「透過模式」下，外部設備（如手機 APP）透過傳送固定結構的封包與本機通訊。

## BLE 指令 (OP_BLE_xxx)
這些指令用於手機 APP 與 nRF54L15 之間的 BLE 通訊。

| OP_CODE (Hex) | 常數名稱                     | 功能名稱          | 說明                                                   |
| :------------ | :--------------------------- | :---------------- | :----------------------------------------------------- |
| **0x66**      | `OP_BLE_GET_IOR_INFO`        | Io/Ior 資訊取得   | 讀取指定通道 (CH1-6) 的漏電電流、相位角與警報界限。    |
| **0xF6**      | `OP_BLE_GET_REALTIME_CURR`   | AI 即時電流取得   | 取得 6 個通道的即時電流數據與 DI 警報狀態位元。        |
| **0xD8**      | `OP_BLE_GET_MAX_CURRENT`     | 最高電流資訊取得  | 取得過去 3 個月內各通道的最高電流值及其發生時間。      |
| **0xF9**      | `OP_BLE_GET_DEMAND_DATA`     | 需量數據取得      | 取得目前電力、預測電力、30 分鐘需量值及各硬體版本。    |
| **0xD5**      | `OP_BLE_SET_DEVICE_NAME`     | 裝置名稱變更      | 設定藍牙顯示名稱（DeviceName）與設置場所（Location）。 |
| **0xD6**      | `OP_BLE_GET_DEVICE_NAME`     | 裝置名稱取得      | 讀取目前的藍牙顯示名稱與設置場所。                     |
| **0xE5**      | `OP_BLE_SET_COMM_DEMAND`     | 通訊/需量設定變更 | 修改 Server IP、LTE APN、需量目標值、脈衝係數等。      |
| **0xE6**      | `OP_BLE_GET_COMM_DEMAND`     | 通訊/需量設定取得 | 讀取目前的通訊與需量相關參數設定。                     |
| **0xB5**      | `OP_BLE_SET_AI_CONFIG`       | AI 設定變更       | 修改各通道的監測方式 (Io/Ior)、相位補正與判定時間。    |
| **0xB6**      | `OP_BLE_GET_AI_CONFIG`       | AI 設定取得       | 讀取目前的 AI 監測方式、相位與判定時間設定。           |
| **0xA5**      | `OP_BLE_SET_MQTT_CONFIG`     | MQTT 設定變更     | 設定雲端連線的 MQTT Client ID 與 Server URL 位址。     |
| **0xA6**      | `OP_BLE_GET_MQTT_CONFIG`     | MQTT 設定取得     | 讀取目前的 MQTT 連線參數設定。                         |
| **0xE0**      | `OP_BLE_TEST_ECHO`           | 測試回音          | 用於測試通訊，回傳接收到的資料。                       |
| **0xE1**      | `OP_BLE_TEST_FLASH`          | 測試檔案系統      | 測試外部 Flash 與 LittleFS 是否正常運作。              |
| **0xFB**      | `OP_BLE_FS_FAST_FORMAT`      | 快速格式化        | 使用 fs_mkfs 快速格式化 LittleFS（僅重建檔案系統）。   |
| **0xFC**      | `OP_BLE_FS_LOW_LEVEL_FORMAT` | 低階格式化        | 完整抹除 Flash 後重新格式化 LittleFS（耗時較久）。     |
