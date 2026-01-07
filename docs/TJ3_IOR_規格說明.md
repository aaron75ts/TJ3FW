關於 ATM90E26 的初始化設定、數據取得方式及相關範例，根據開發式樣書（以下簡稱「這些來源」）的規範整理如下：

### 一、 ATM90E26 初始化設定
根據硬體架構，nRF54L15 透過 **3 組獨立的 UART** 與三顆 ATM90E26 連結。系統啟動後，必須透過軟體進行以下關鍵參數的初始化設定：

1.  **監測模式選擇**：需設定各通道（CH1~6）為 **Io**、**Ior 自動**或 **Ior 手動**模式。
2.  **變壓器與相位補正 ($\alpha$)**：
    *   根據電源接入相與測量對象的結線方式（如單相 2 線、三相 3 線 Y-Δ 或 Δ-Δ），設定補正角度（0°, 30°, 60°, 90°, 120°, 150°）。
    *   這是計算抵抗分漏電電流 (Ior) 的核心參數，計算公式為 $Ior = Io \cdot \cos(\theta + \alpha)$ (單相) 或 $Ior = Io \cdot \sin(\theta + \alpha) / \cos 30^\circ$ (三相)。
3.  **硬體參數定義**：
    *   **ZCT 比率**：設定為 4500:1 或 2000:1。
    *   **CT 係數**：若用於負荷電流測量，需設定 CT 變比係數。
4.  **警報閥值與判定時間**：
    *   設定輕漏電/重漏電的電流閾值（25~2000mA）及判定時間（1~999秒）。

### 二、 數據取得方式
數據由 nRF54L15 定期從 ATM90E26 讀取並處理，外部可透過以下管道取得：

1.  **BLE 藍牙取得 (APP 端)**：
    *   **Meter Snapshot**：透過 UUID `...8c01` 進行 READ 或 NOTIFY，取得固定 20 bytes 的原始數據。
    *   **特定指令**：發送 OP_CODE `0x66` (Io/Ior 資訊) 或 `0xF6` (即時電流)。
2.  **MQTT 雲端取得 (AWS 端)**：
    *   **定期通報 (FA)**：系統依設定間隔（如 1~60 分鐘）主動推送 6 通道電流值與地絡電流。
    *   **輪詢要求 (PH/PW)**：雲端發送 `PH` (漏電即時資訊) 或 `PW` (相位/Io 資訊) 指令要求立即回傳。
3.  **內部存儲**：數據會以 CSV 格式儲存於 Flash 中（1 分鐘值保存 90 天，30 分鐘值保存 1 年），可事後讀取。

### 三、 數據範例

#### 1. BLE 指令回傳範例 (OP_CODE: 0x66)
當 APP 要求取得通道 1 的 Io/Ior 資訊時，回傳格式如下：
*   **STX**: `0x02`
*   **Payload**: `0E 00 66 01 [Io_L][Io_H] [Ior_L][Ior_H] [Limit_L][Limit_H] [Sign] [Angle_L][Angle_H]`
*   **ETX**: `0x03`
*   **CRC**: 2 Bytes

#### 2. MQTT 定期通報範例 (識別碼: FA)
發送到雲端的 6 通道電流數據 Payload 範例：
`\x02 [PeriID] FA [Seq] [Num] [MainID] [MMDDHHMM] [Current1] [Current2] [Current3] [Current4] [Current5] [Current6] [Ig_Value] \x03`
*   **Current1~6 格式**：`aaaa.aaa` (7 bytes ASCII，含 3 位小數)。
*   **Ig_Value 格式**：`0000` (4 bytes ASCII mA)。

#### 3. MQTT 相位資訊要求回應 (識別碼: PW)
包含 6 個通道完整的 Io, Phase, Ior 數據：
`\x02 [PeriID] PW [Seq] [Num] [MainID] [C1_Io] [C1_Phase] [C1_Ior] ... [C6_Ior] \x03`
*   **相位值範例**：`-1800` ~ `+1800` (代表 -180.0° 到 +180.0°)。
