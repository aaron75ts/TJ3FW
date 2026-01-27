根據 TJ3-nRF 開發式樣書與 BLE UUID 對照表（the sources），系統對 **BLE GATT Service（服務）** 與 **Characteristic（特徵值）** 的具體要求如下：

### 一、 核心服務與特徵值定義
系統主要定義了兩組自定義服務，分別用於能源監控與系統診斷。

#### 1. 能源監控服務 (Energy Service)
*   **UUID**: `4d6f8c00-4b9a-4c1b-9a61-112233445500`
*   **關鍵特徵值要求**：
    *   **Meter Snapshot (`...8c01...`)**：
        *   **權限**：READ + NOTIFY。
        *   **格式**：固定 **20 bytes (raw)**，用於提供即時數據快照。
    *   **Meter Config (`...8c02...`)**：
        *   **權限**：READ + WRITE。
        *   **格式**：固定 **20 bytes (raw)**，用於讀取或設定計量參數。
    *   **Command (`...8c03...`)**：
        *   **權限**：WRITE + INDICATE。
        *   **功能**：手機 APP 寫入指令（1–32 bytes），設備會將原封包 **Echo 回傳 (Indicate)** 作為 ACK 確認。
    *   **Power Meter Pulse (`...8c04...`)**：
        *   **權限**：READ + NOTIFY。
        *   **格式**：`uint32_le`，單位為 **milli-Hz**（運算公式為 Hz * 1000）。
    *   **Alarm Status (`...8c05...`)**：
        *   **權限**：READ + NOTIFY。
        *   **格式**：`uint32_le` bitmask，用於表示各通道的警報狀態。

#### 2. 系統診斷服務 (Diagnostics Service)
*   **UUID**: `4d6f8c10-4b9a-4c1b-9a61-112233445500`
*   **關鍵特徵值要求**：
    *   **Log Count (`...8c11...`)**：READ，`uint16_le` 格式，顯示內部日誌總數。
    *   **Log Fetch (`...8c12...`)**：WRITE + INDICATE，寫入日誌索引後，以 **Indicate** 方式回傳編碼後的日誌內容（$\le$ 80 bytes）。
    *   **Log Level (`...8c13...`)**：READ + WRITE，1 byte，設定日誌記錄等級。

---

### 二、 BLE 運作機制與安全性要求
1.  **設備角色**：本機 (nRF54L15) 啟動後作為 **Peripheral (子機)** 進行廣播，接受手機 APP 或 PC 等 Central 設備的連線。
2.  **寫入授權限制**：為了安全起見，若要透過藍牙 APP 進行設定變更（WRITE），必須先**長按本機的 "S" (Set Mode) 按鈕 3 秒以上**，否則系統將拒絕寫入指令。
3.  **封包通訊格式**：所有透過特徵值傳輸的 Command 必須遵循固定封裝：`[STX(02)] [LEN_L] [LEN_H] [OP_CODE] [DATA...] [ETX(03)] [CRC1] [CRC2]`。
4.  **透過模式 (Transparent Mode)**：系統支持透過模式，當收到 BLE 指令後，可直接將數據內容導向至 **UART** 與通訊模組（nRF9151）或計量晶片進行交互。

### 三、 數據更新機制
*   **主動通知 (Notify)**：當測量值（如 Io/Ior）發生變化或警報觸發時，nRF54L15 會主動對已訂閱（Subscribe）的特徵值（如 `...8c01` 或 `...8c05`）發送 Notify 數據給 APP。
*   **讀取操作 (Read)**：APP 可隨時對 Snapshot 特徵值發起 Read 請求，取得最新一次採樣的電力數據。

**💡 總結**：規格書要求建立一組嚴謹的 UUID 對照表，將**量測、設定、指令與診斷**分離。所有寫入動作必須受到實體按鈕的安全保護，並透過 **Indicate/Echo** 機制確保通訊的可靠性。