# TJ3 Gateway 快速啟動指南

## 🚀 立即開始

### 1. 進入專案目錄
```bash
cd TJ3FW/docs/TJ3-GW
```

### 2. 啟動應用（兩種方式）

#### 方式 A：使用 uv 運行
```bash
uv run main.py
```

#### 方式 B：啟動虛擬環境後運行
```bash
source .venv/bin/activate
python main.py
```

## 📱 應用功能一覽

### 📋 Characteristics
- **功能**：查看所有 BLE 服務和特性
- **操作**：
  - 選擇特性 → 點擊 "Read Value" 讀取
  - 選擇特性 → 點擊 "Write Value" → 輸入 Hex → 寫入
  - 選擇特性 → 點擊 "Enable Notify" → 啟用通知

### 📊 Characteristic
- **Energy Meter**：
  - Meter Snapshot（20 bytes 原始數據）
  - Pulse (mHz)：功率脈衝頻率
  - Alarm Status：告警位元遮罩
- **Diagnostics**：
  - 實時日誌串流
  - Start/Stop Log Stream 控制

### ⚙️ Configuration
- **Meter Config**：
  - Read Config：讀取 20 bytes 配置
  - Write Config：寫入新配置（Hex 格式）
- **Log Level**：
  - 選擇日誌級別：OFF / ERROR / WARN / INFO / DEBUG
  - Set Log Level：應用到設備

## 🔧 常用操作範例

### 讀取電表配置
1. 連接設備
2. 切換到 "⚙️ Configuration" tab
3. 點擊 "Read Config"
4. 配置數據顯示在 Hex Data 欄位

### 修改日誌級別
1. 連接設備
2. 切換到 "⚙️ Configuration" tab
3. 選擇 Log Level（如 DEBUG）
4. 點擊 "Set Log Level"

### 監控實時日誌
1. 連接設備
2. 切換到 "📊 Live Monitor" tab
3. 點擊 "Start Log Stream"
4. 日誌將實時顯示在下方文本框
5. **複製日誌內容**：
   - 方法 1：選擇文字後按 `Ctrl+C` (Windows/Linux) 或 `Cmd+C` (Mac)
   - 方法 2：右鍵點擊 → 選擇「複製 (Copy)」
   - 方法 3：`Ctrl+A` / `Cmd+A` 全選後複製

### 底部日誌區域（可複製）
- 應用底部的 "📋 Logs (可複製)" 區域顯示所有應用操作日誌
- **支援複製**：可選取任意日誌內容並複製，方便除錯和分享
- **快捷鍵**：
  - `Ctrl+C` / `Cmd+C`：複製選取的內容
  - `Ctrl+A` / `Cmd+A`：全選所有日誌
- **右鍵選單**：右鍵點擊日誌區域可使用複製選單

### 手動讀寫特性
1. 連接設備
2. 切換到 "📋 Characteristics" tab
3. 展開 Energy Service 或 Diagnostics Service
4. 選擇一個特性
5. 使用 Read/Write/Notify 按鈕操作

## 🎨 UI 主題

應用使用 **flatly** 主題（清新藍色），可在 `main.py` 的 `main()` 函數中修改：

```python
# 可用主題：
# flatly, cosmo, litera, minty, pulse, sandstone, 
# united, yeti, morph, journal, darkly, cyborg, superhero
root = ttk_bs.Window(themename="flatly")
```

## 📊 數據格式提示

### Meter Config (20 bytes)
```
示例 Hex: 0102030405060708090a0b0c0d0e0f10111213
格式：根據 TJ3 韌體規格定義
```

### Log Level
```
0 = OFF
1 = ERROR
2 = WARN
3 = INFO (預設)
4 = DEBUG
```

### Power Pulse (4 bytes, little-endian)
```
uint32_le，單位：milli-Hz (1/1000 Hz)
例：0x00007530 = 30000 mHz = 30 Hz
```

## 🐛 偵錯模式

如需更詳細的日誌，修改 `main.py` 頂部：
```python
logging.basicConfig(
    level=logging.DEBUG,  # 改為 DEBUG
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
```

## 📝 文件說明

| 文件             | 說明              |
| ---------------- | ----------------- |
| `main.py`        | 主應用程式（GUI） |
| `ble_manager.py` | BLE 通訊管理      |
| `constants.py`   | UUID 常數定義     |
| `run.sh`         | 快速啟動腳本      |
| `README.md`      | 完整文檔          |
| `QUICKSTART.md`  | 本文件            |

## 🆘 需要幫助？

1. 查看底部日誌輸出
2. 啟動 DEBUG 模式查看詳細訊息
3. 參考 `README.md` 故障排除章節
4. 檢查 TJ3 設備是否正常廣播（用手機 nRF Connect 驗證）

---

**提示**：首次使用建議先用 "📋 Characteristics" tab 熟悉所有可用的特性！
