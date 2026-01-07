# README

- SDK: v3.2.1
- Toolchain: v3.2.1

## 編譯

**在專案根目錄執行**

```shell
# 乾淨重建（第一次或改了 TF‑M/overlay/Kconfig 之後用）
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -d build_dev -p always --sysbuild

# 增量重建（已經有 build_dev）
west build -d build_dev --sysbuild

# 指定用哪個設定檔（例如 prj_dev.conf / prj_prod.conf）
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -S . -d build_dev -p always -- -DCONF_FILE=prj.conf
```

- --sysbuild .：啟用 sysbuild（TF‑M/多 image 會需要）
- -d build_dev：指定輸出目錄
- -p always：強制 pristine 重建（等同乾淨 build）

## 燒錄

```shell
> west flash -d build_dev
```