# TJ3 Gateway - BLE Configuration Tool

TJ3 Gateway 是一個基於 Python 的 BLE 配置工具，使用 ttkbootstrap 構建現代化圖形界面，可以與 TJ3 能源監測設備進行通訊。

## 🚀 快速開始

### 前置需求
- Python 3.10 或更高版本
- macOS / Linux / Windows（需要 BLE 適配器）
- [uv](https://github.com/astral-sh/uv) 包管理器

### 立即啟動

1. **進入專案目錄**：
```bash
cd TJ3FW/docs/TJ3-GW
```

2. **安裝依賴**（首次使用）：
```bash
uv sync
```

3. **啟動應用**（兩種方式）：

#### 方式 A：使用 uv 運行（推薦）
```bash
uv run main.py
```

#### 方式 B：啟動虛擬環境後運行
```bash
source .venv/bin/activate  # macOS/Linux
# .venv\Scripts\activate   # Windows
python main.py
```

### 基本操作流程

1. 點擊 **"Scan for Devices"** 掃描附近的 TJ3 設備
2. 從下拉選單選擇設備，點擊 **"Connect"** 連接
3. 切換到 **"📋 Characteristics"** 標籤查看所有 BLE 特性
4. 點擊特性名稱自動讀取並解析數據
5. 使用右側編輯器修改配置，點擊 **"Write Value"** 寫入

> **提示**：首次使用建議先用 "📋 Characteristics" 標籤熟悉所有可用的特性！

## 功能特點

### 🔍 設備掃描與連接
- 自動掃描附近的 TJ3 BLE 設備
- 顯示設備名稱和 MAC 地址
- 一鍵連接/斷開功能
- 實時顯示連接狀態

### 📋 特性管理
- **完整的 GATT 特性列表**：自動發現並顯示所有 BLE 服務和特性
- **服務分類顯示**：
  - Energy Service（能源服務）
  - Diagnostics Service（診斷服務）
  - 其他標準服務
- **特性操作**：
  - **自動讀取**：點擊 READ 特性時自動讀取並解析數據
  - **智能解析**：Meter Config 等特性自動拆分為各個設定值
  - **可視化編輯**：WRITE 特性提供直觀的輸入表單
  - **一鍵寫入**：編輯完成後直接寫入設備
  - 啟用通知/指示

### 📊 實時監控
- **能源數據**：
  - Meter Snapshot（電表快照）
  - Power Pulse（功率脈衝，mHz）
  - Alarm Status（告警狀態）
- **診斷日誌流**：
  - 實時日誌串流顯示
  - 可啟動/停止日誌接收
  - **支援文字選取和複製**

### ⚙️ 設備配置
- **電表配置**：讀取/寫入 20 字節配置數據
- **日誌級別設置**：OFF、ERROR、WARN、INFO、DEBUG

### 📋 日誌功能（新增）
- **可複製的日誌輸出**：所有文字區域（應用日誌、設備日誌流、能源數據）均支援選取和複製
- **快捷鍵支援**：`Ctrl+C`/`Cmd+C` 複製、`Ctrl+A`/`Cmd+A` 全選
- **右鍵選單**：提供複製、全選等便捷操作
- **只讀保護**：防止誤編輯，但允許自由選取複製

## 安裝

### 依賴套件

依賴包括：
- `bleak` - BLE 通訊庫
- `ttkbootstrap` - 現代化 Tkinter UI 主題
- `pillow` - 圖像處理支援

使用 `uv sync` 命令會自動安裝所有依賴。

## 詳細使用方法

### 操作流程

1. **掃描設備**
   - 點擊 "Scan for Devices" 按鈕
   - 等待 5 秒掃描完成
   - 從下拉列表選擇目標設備

2. **連接設備**
   - 點擊 "Connect" 按鈕
   - 等待連接成功（狀態變為綠色）
   - 系統會自動發現所有特性

3. **查看特性**
   - 切換到 "📋 Characteristics" 標籤
   - 瀏覽服務和特性樹狀結構
   - **點擊 READ 特性自動讀取並解析數據**
   - 右側顯示詳細的設定值

4. **編輯配置（Meter Config）**
   - 在 Characteristics 列表中點擊 "Meter Config"
   - 系統自動讀取當前配置
   - 右側顯示可編輯的表單：
     - CT Ratio（6 個通道）
     - CH Igain（6 個通道）
   - 修改需要的值
   - 點擊 "Write Value" 按鈕寫入設備
   - 系統自動重新讀取以驗證修改

5. **實時監控**
   - 切換到 "📊 Live Monitor" 標籤
   - 啟用所需的通知功能
   - 查看實時數據更新

6. **配置管理**
   - 切換到 "⚙️ Configuration" 標籤
   - 讀取當前配置
   - 修改並寫入新配置
   - 調整日誌級別

## 💡 常用操作示例

### 讀取電表配置
1. 連接設備
2. 切換到 **"⚙️ Configuration"** 標籤
3. 點擊 **"Read Config"**
4. 配置數據顯示在 Hex Data 欄位

### 修改日誌級別
1. 連接設備
2. 切換到 **"⚙️ Configuration"** 標籤
3. 選擇 Log Level（如 DEBUG）
4. 點擊 **"Set Log Level"**

### 監控實時日誌
1. 連接設備
2. 切換到 **"📊 Live Monitor"** 標籤
3. 點擊 **"Start Log Stream"**
4. 日誌將實時顯示在下方文本框
5. **複製日誌內容**：
   - 方法 1：選擇文字後按 `Ctrl+C` (Windows/Linux) 或 `Cmd+C` (Mac)
   - 方法 2：右鍵點擊 → 選擇「複製 (Copy)」
   - 方法 3：`Ctrl+A` / `Cmd+A` 全選後複製

### 手動讀寫特性
1. 連接設備
2. 切換到 **"📋 Characteristics"** 標籤
3. 展開 Energy Service 或 Diagnostics Service
4. 選擇一個特性
5. 使用 Read/Write/Notify 按鈕操作

### 數據格式參考

#### Meter Config (24 bytes)
```
格式：CT Ratio (12 bytes) + CH Igain (12 bytes)
示例：根據 TJ3 韌體規格定義
```

#### Log Level
```
0 = OFF
1 = ERROR
2 = WARN
3 = INFO (預設)
4 = DEBUG
```

#### Power Pulse (4 bytes, little-endian)
```
uint32_le，單位：milli-Hz (1/1000 Hz)
例：0x00007530 = 30000 mHz = 30 Hz
```

## TJ3 BLE 服務架構

### Energy Service
**UUID**: `4d6f8c00-4b9a-4c1b-9a61-112233445500`

| 特性           | UUID 後綴 | 屬性             | 說明                                                        |
| -------------- | --------- | ---------------- | ----------------------------------------------------------- |
| Meter Snapshot | ...01     | READ + NOTIFY    | 電表快照數據（20 bytes）                                    |
| Meter Config   | ...02     | READ + WRITE     | 電表配置（24 bytes：CT Ratio 12 bytes + CH Igain 12 bytes） |
| Command        | ...03     | WRITE + INDICATE | 命令傳送與回應                                              |
| Power Pulse    | ...04     | READ + NOTIFY    | 功率脈衝（uint32_le mHz）                                   |
| Alarm Status   | ...05     | READ + NOTIFY    | 告警狀態（uint32_le bitmask）                               |

### Diagnostics Service
**UUID**: `4d6f8c10-4b9a-4c1b-9a61-112233445500`

| 特性       | UUID 後綴 | 屬性             | 說明                  |
| ---------- | --------- | ---------------- | --------------------- |
| Log Count  | ...11     | READ             | 日誌數量（uint16_le） |
| Log Fetch  | ...12     | WRITE + INDICATE | 讀取指定索引的日誌    |
| Log Level  | ...13     | READ + WRITE     | 日誌級別（1 byte）    |
| Clear Logs | ...14     | WRITE + INDICATE | 清除日誌並返回狀態    |
| Log Stream | ...15     | NOTIFY           | 實時日誌串流          |

## 開發

### 專案結構

```
TJ3-GW/
├── main.py                  # 主應用程式（GUI）
├── ble_manager.py          # BLE 通訊管理器
├── constants.py            # UUID 和常數定義
├── pyproject.toml          # 專案配置和依賴
├── uv.lock                 # 依賴鎖定文件
├── .gitignore              # Git 忽略文件
├── README.md               # 本文件（完整文檔）
├── CHARACTERISTIC_EDITOR.md  # 特性編輯器功能說明
├── LOG_COPY_FEATURE.md     # 日誌複製功能說明
└── TROUBLESHOOTING.md      # 故障排除指南
```

### 相關文檔

- **[CHARACTERISTIC_EDITOR.md](CHARACTERISTIC_EDITOR.md)** - 特性編輯器詳細說明（自動讀取、解析和編輯功能）
- **[LOG_COPY_FEATURE.md](LOG_COPY_FEATURE.md)** - 日誌複製功能詳解
- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** - 常見問題與解決方案

### 擴展開發

如需添加新功能，可以修改：

1. **constants.py** - 添加新的 UUID 定義
2. **ble_manager.py** - 實現新的 BLE 通訊邏輯
3. **main.py** - 更新 GUI 界面和事件處理

### UI 主題自訂

應用使用 **flatly** 主題（清新藍色），可在 `main.py` 的 `main()` 函數中修改：

```python
# 可用主題：
# flatly, cosmo, litera, minty, pulse, sandstone, 
# united, yeti, morph, journal, darkly, cyborg, superhero
root = ttk_bs.Window(themename="flatly")
```

### 偵錯模式

如需更詳細的日誌，修改 `main.py` 頂部：
```python
logging.basicConfig(
    level=logging.DEBUG,  # 改為 DEBUG
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
```

### BLE 通訊示例

```python
from ble_manager import TJ3BLEManager
import asyncio

async def example():
    manager = TJ3BLEManager()
    
    # 掃描設備
    devices = await manager.scan_devices(timeout=5.0)
    
    # 連接第一個設備
    if devices:
        await manager.connect(devices[0].address)
        
        # 讀取特性
        value = await manager.read_characteristic(ENERGY_CONFIG_UUID)
        print(f"Config: {value.hex()}")
        
        # 寫入特性
        await manager.write_characteristic(ENERGY_CMD_UUID, b'\x01\x02\x03')
        
        # 啟用通知
        def callback(data):
            print(f"Notification: {data.hex()}")
        
        await manager.enable_notification(ENERGY_PULSE_UUID, callback)
        
        # 保持連接
        await asyncio.sleep(10)
        
        # 斷開連接
        await manager.disconnect()

asyncio.run(example())
```

## 故障排除

### macOS 藍牙權限
如果無法掃描設備，請確認：
1. 系統設定 → 隱私權與安全性 → 藍牙
2. 允許 Terminal 或 Python 使用藍牙

### Windows BLE 支援
- 需要 Windows 10 版本 1703 或更高
- 確保藍牙適配器支援 BLE 4.0+
- 可能需要安裝額外的藍牙驅動

### Linux 依賴
```bash
# Ubuntu/Debian
sudo apt-get install bluez python3-dev libbluetooth-dev

# 添加用戶到 bluetooth 群組
sudo usermod -a -G bluetooth $USER
```

### 常見問題

**Q: 掃描不到設備**
- 確認設備已開機且在廣播範圍內
- 檢查電腦藍牙是否開啟
- 嘗試使用手機 nRF Connect 確認設備可見

**Q: 連接失敗**
- 檢查設備是否已被其他應用連接
- 重啟設備和電腦藍牙
- 查看日誌輸出詳細錯誤訊息

**Q: 讀取/寫入失敗**
- 確認特性屬性（某些特性只能讀取或寫入）
- 檢查數據格式是否正確
- 確認設備韌體版本

## 授權

本專案為 TJ3 韌體的配套工具。

## 相關連結

- [TJ3 韌體專案](../../../)
- [Bleak 文檔](https://bleak.readthedocs.io/)
- [ttkbootstrap 文檔](https://ttkbootstrap.readthedocs.io/)
