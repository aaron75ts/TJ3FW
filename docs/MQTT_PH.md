根據開發式樣書（來源），MQTT **PH** 是用於 **「Polling 漏電情報要求」** 的識別代碼。雲端（AWS）透過此指令遠端取得裝置目前的漏電電流值與接點（DI）狀態。

以下為具體的電文格式與範例：

### 1. 雲端發送要求格式 (Polling Request)
當雲端欲索取漏電資訊時，發送此電文：

*   **Topic (主題)**：`kyosvr/<MainID>/<PeriID>/cmd`
*   **Payload (電文內容)**：
    `\x02[PeriID]PH[Seq][Num][MainID]\x03`

**欄位解析：**
*   **STX** (`\x02`)：電文頭。
*   **PeriID** (6 bytes)：子機名稱。
*   **PH** (2 bytes)：識別碼（漏電情報要求）。
*   **Seq** (2 bytes)：雲端序號（'00'~'99'）。
*   **Num** (2 bytes)：子機編號（'01'~'08'）。
*   **MainID** (6 bytes)：親機名稱。
*   **ETX** (`\x03`)：電文尾。

---

### 2. 設備回傳格式 (Device Response)
本機收到要求後，回傳包含 6 個通道的電流值與 2 個接點狀態：

*   **Topic (主題)**：`kyokuto/<MainID>/<PeriID>/PH`
*   **Payload (電文內容)**：
    `\x02[PeriID]PH[Seq][Num][MainID][Current1][Current2][Current3][Current4][Current5][Current6][DI1][DI2]\x03`

**欄位解析：**
*   **Current1 ~ 6** (每個 4 bytes ASCII)：各 CT 通道的漏電電流測量值（'0000' ~ '2000'，單位 mA）。
*   **DI1 / DI2** (每個 1 byte ASCII)：接點狀態（'0'：復歸，'1'：警報中）。

---

### 3. 具體範例
假設親機 ID 為 `GW0001`，子機 ID 為 `PE0002`，雲端要求序號為 `05`：

#### **A. 雲端發送要求 (Request)**
*   **Topic**: `kyosvr/GW0001/PE0002/cmd`
*   **Payload**: `\x02PE0002PH0502GW0001\x03`

#### **B. 設備回傳數據 (Response)**
假設測得 CT1 為 10mA，CT2 為 25mA，其餘通道為 0，且 DI2 處於警報狀態：
*   **Topic**: `kyokuto/GW0001/PE0002/PH`
*   **Payload**: `\x02PE0002PH0502GW000100100025000000000000000001\x03`
    *   *解析：`0010` (CT1=10mA), `0025` (CT2=25mA), `0` (DI1正常), `1` (DI2警報)*。
