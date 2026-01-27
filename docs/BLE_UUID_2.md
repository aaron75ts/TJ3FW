根據 TJ3-nRF 開發式樣書與 BLE UUID 對照表（the sources），系統要求的 BLE 服務、特徵值及其完整 UUID 和對應的 OP Code 說明如下：

### 一、 能源監控服務 (Energy Service)
*   **服務 UUID**: `4d6f8c00-4b9a-4c1b-9a61-112233445500`

| 特徵值名稱            | 完整 UUID                              | 權限與格式                          |
| :-------------------- | :------------------------------------- | :---------------------------------- |
| **Meter Snapshot**    | `4d6f8c01-4b9a-4c1b-9a61-112233445500` | READ + NOTIFY (20 bytes raw)        |
| **Meter Config**      | `4d6f8c02-4b9a-4c1b-9a61-112233445500` | READ + WRITE (20 bytes raw)         |
| **Command**           | `4d6f8c03-4b9a-4c1b-9a61-112233445500` | WRITE + INDICATE (1–32 bytes)       |
| **Power Meter Pulse** | `4d6f8c04-4b9a-4c1b-9a61-112233445500` | READ + NOTIFY (uint32_le, milli-Hz) |
| **Alarm Status**      | `4d6f8c05-4b9a-4c1b-9a61-112233445500` | READ + NOTIFY (uint32_le bitmask)   |

---

### 二、 系統診斷服務 (Diagnostics Service)
*   **服務 UUID**: `4d6f8c10-4b9a-4c1b-9a61-112233445500`

| 特徵值名稱     | 完整 UUID                              | 權限與格式                           |
| :------------- | :------------------------------------- | :----------------------------------- |
| **Log Count**  | `4d6f8c11-4b9a-4c1b-9a61-112233445500` | READ (uint16_le)                     |
| **Log Fetch**  | `4d6f8c12-4b9a-4c1b-9a61-112233445500` | WRITE (idx) + INDICATE (<= 80 bytes) |
| **Log Level**  | `4d6f8c13-4b9a-4c1b-9a61-112233445500` | READ + WRITE (1 byte)                |
| **Clear Logs** | `4d6f8c14-4b9a-4c1b-9a61-112233445500` | WRITE + INDICATE (Status)            |
| **Log Stream** | `4d6f8c15-4b9a-4c1b-9a61-112233445500` | NOTIFY                               |

---

### 三、 Command 特徵值下定義的 OP Code
這些指令主要透過 **Command (`...8c03`)** 特徵值進行發送與接收，用於讀取量測值或變更系統設定。

| OP Code (Hex) | 功能說明                         | 類型  | 來源 |
| :------------ | :------------------------------- | :---- | :--- |
| **0x66**      | Io/Ior 資訊取得                  | READ  |      |
| **0xF6**      | AI 現在量測數據取得              | READ  |      |
| **0xD8**      | 最大電流 (MAX_CURRENT) 資訊取得  | READ  |      |
| **0xF9**      | 需量 (Power Demand) 現在數據取得 | READ  |      |
| **0xD5**      | 藍牙名稱 (Device Name) 設定變更  | WRITE |      |
| **0xD6**      | 藍牙名稱 (Device Name) 資訊取得  | READ  |      |
| **0xE5**      | 通訊與需量參數設定變更           | WRITE |      |
| **0xE6**      | 通訊與需量參數資訊取得           | READ  |      |
| **0xB5**      | AI 監控設定資訊變更              | WRITE |      |
| **0xB6**      | AI 監控設定資訊取得              | READ  |      |
| **0xA5**      | MQTT 設定資訊變更                | WRITE |      |
| **0xA6**      | MQTT 設定資訊取得                | READ  |      |

### 重要安全性規則
根據來源定義，凡涉及 **WRITE (設定變更)** 的特徵值或指令，必須先在設備端長按 **"S" (Set Mode) 按鈕 3 秒以上**進入設定模式，藍牙端才能成功寫入參數。