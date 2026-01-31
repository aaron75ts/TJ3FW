#!/usr/bin/env python3
"""
ATM90E26 測試程式
用於測試 ATM90E26 晶片的初始化和暫存器讀取功能
"""

import serial
import time
import sys
import argparse

# ===== 預設配置 =====
DEFAULT_PORT = '/dev/cu.usbserial-AG0KBCCV'  # 預設串口
DEFAULT_BAUDRATE = 9600                       # 預設鮑率

class ATM90E26_Tester:
    def __init__(self, port, baudrate=9600, swap_endian=False):
        """初始化 UART 連接
        
        Args:
            port: 串口名稱
            baudrate: 鮑率
            swap_endian: 是否交換 MSB/LSB 位置 (預設 False = Big-Endian)
        """
        self.swap_endian = swap_endian
        try:
            self.ser = serial.Serial(port, baudrate, timeout=0.1)
            endian_mode = "Little-Endian (LSB first)" if swap_endian else "Big-Endian (MSB first)"
            print(f"✓ 已連接到 {port} @ {baudrate} baud [{endian_mode}]")
            time.sleep(0.5)
        except serial.SerialException as e:
            print(f"✗ 無法打開串口 {port}: {e}")
            sys.exit(1)

    def _calc_checksum(self, addr, lsb, msb):
        """計算寫入指令的校驗和"""
        return (addr + lsb + msb) & 0xFF

    def write_reg(self, addr, value, verbose=True):
        """寫入暫存器
        返回: (success, response_byte)
        """
        lsb = value & 0xFF
        msb = (value >> 8) & 0xFF
        chk = self._calc_checksum(addr, lsb, msb)
        
        # 字節序調整: 預設 MSB first (Big-Endian)
        if self.swap_endian:
            payload = bytes([0xFE, addr, lsb, msb, chk])  # LSB first (Little-Endian)
        else:
            payload = bytes([0xFE, addr, msb, lsb, chk])  # MSB first (Big-Endian)
        
        if verbose:
            hex_tx = " ".join(f"{b:02X}" for b in payload)
            endian_note = " [Little-Endian]" if self.swap_endian else " [Big-Endian]"
            print(f"  TX: {hex_tx}  → 寫入 Reg[0x{addr:02X}] = 0x{value:04X}{endian_note}")
        
        # 清空接收緩衝區
        self.ser.reset_input_buffer()
        
        self.ser.write(payload)
        time.sleep(0.01)  # 給晶片反應時間
        
        # 讀取回應（如果是 Soft Reset 可能沒有回應）
        res = self.ser.read(1)
        if res and len(res) == 1:
            if verbose:
                print(f"  RX: {res[0]:02X}  ✓")
            time.sleep(0.025)
            return True, res[0]
        else:
            if verbose:
                if addr == 0x00:  # Soft Reset
                    print(f"  RX: 無回應 (正常，Soft Reset)")
                else:
                    print(f"  RX: 超時 ✗")
            time.sleep(0.025)
            return addr == 0x00, None  # Soft Reset 沒回應也算成功

    def read_reg(self, addr, verbose=True):
        """讀取暫存器
        返回: (success, value)
        """
        read_addr = addr | 0x80
        payload = bytes([0xFE, read_addr, read_addr])
        
        if verbose:
            hex_tx = " ".join(f"{b:02X}" for b in payload)
            print(f"  TX: {hex_tx}  → 讀取 Reg[0x{addr:02X}]")
        
        # 清空接收緩衝區
        self.ser.reset_input_buffer()
        
        self.ser.write(payload)
        time.sleep(0.01)
        
        # 讀取回應: MSB, LSB, Checksum (或 LSB, MSB, Checksum 如果 swap_endian)
        res = self.ser.read(3)
        if len(res) == 3:
            if self.swap_endian:
                val = (res[1] << 8) | res[0]  # LSB first (Little-Endian)
            else:
                val = (res[0] << 8) | res[1]  # MSB first (Big-Endian)
            if verbose:
                endian_note = " [Little-Endian]" if self.swap_endian else " [Big-Endian]"
                print(f"  RX: {res[0]:02X} {res[1]:02X} {res[2]:02X}  → 0x{val:04X} ({val}){endian_note} ✓")
            time.sleep(0.025)
            return True, val
        else:
            if verbose:
                print(f"  RX: 超時 (收到 {len(res)}/3 bytes) ✗")
            time.sleep(0.025)
            return False, None

    def calculate_cs1(self, regs):
        """計算 CS1 校驗和 (暫存器 21H-2BH)
        
        公式: CS1 = (XOR_H << 8) | SUM_L
        其中:
          SUM_L = (所有高位 + 所有低位) & 0xFF
          XOR_H = (所有高位 XOR 所有低位) & 0xFF
        
        參數: regs - 11 個 16-bit 暫存器值 (21H-2BH)
        """
        sum_l = 0
        xor_h = 0
        for i, val in enumerate(regs):
            h = (val >> 8) & 0xFF  # 高位元組 (MSB)
            l = val & 0xFF         # 低位元組 (LSB)
            sum_l = (sum_l + h + l) & 0xFF
            xor_h = xor_h ^ h ^ l
        
        cs1 = (xor_h << 8) | sum_l
        return cs1

    def calculate_cs2(self, regs):
        """計算 CS2 校驗和 (暫存器 31H-3AH)
        
        公式: CS2 = (XOR_H << 8) | SUM_L
        其中:
          SUM_L = (所有高位 + 所有低位) & 0xFF
          XOR_H = (所有高位 XOR 所有低位) & 0xFF
        
        參數: regs - 10 個 16-bit 暫存器值 (31H-3AH)
        """
        sum_l = 0
        xor_h = 0
        for i, val in enumerate(regs):
            h = (val >> 8) & 0xFF  # 高位元組 (MSB)
            l = val & 0xFF         # 低位元組 (LSB)
            sum_l = (sum_l + h + l) & 0xFF
            xor_h = xor_h ^ h ^ l
        
        cs2 = (xor_h << 8) | sum_l
        return cs2

    def read_and_verify_status(self, verbose=True):
        """讀取 System Status 並用 LastData 驗證
        返回: (success, status_value)
        """
        # Step 1: 讀取 System Status
        success1, status = self.read_reg(0x01, verbose=verbose)
        if not success1:
            if verbose:
                print("  ✗ 無法讀取 System Status")
            return False, None
        
        # Step 2: 立即讀取 LastData 驗證
        success2, lastdata = self.read_reg(0x06, verbose=False)
        if not success2:
            if verbose:
                print("  ✗ 無法讀取 LastData 驗證")
            return False, status
        
        # Step 3: 比對數值
        if status != lastdata:
            if verbose:
                print(f"  ⚠ 數值不一致: Status=0x{status:04X}, LastData=0x{lastdata:04X}")
                print(f"  → 使用 LastData 值 0x{lastdata:04X} (較可靠)")
            return True, lastdata  # 使用 LastData 的值
        else:
            if verbose:
                print(f"  ✓ 數值一致驗證通過: 0x{status:04X}")
            return True, status

    def test_basic_communication(self):
        """測試基本通訊"""
        print("\n" + "="*60)
        print("測試 1: 基本通訊測試")
        print("="*60)
        
        # 讀取並驗證 System Status
        print("\n讀取並驗證 System Status (0x01 + 0x06):")
        success, val = self.read_and_verify_status()
        
        if success:
            print(f"\n✓ 通訊正常，System Status = 0x{val:04X}")
            # 解析狀態位
            if val & 0x8000:
                print("  ⚠ Bit 15: 系統錯誤 (綜合錯誤標誌)")
            if val & 0x4000:
                print("  ⚠ Bit 14: CS1 校驗錯誤 (計量參數)")
            if val & 0x2000:
                print("  ⚠ Bit 13: CS2 校驗錯誤 (測量參數)")
            return True
        else:
            print("✗ 通訊失敗")
            return False

    def test_soft_reset(self):
        """測試軟體重置"""
        print("\n" + "="*60)
        print("測試 2: 軟體重置 (Soft Reset)")
        print("="*60)
        
        print("\n執行 Soft Reset (寫入 0x789A 到 0x00):")
        success, _ = self.write_reg(0x00, 0x789A)
        
        if success:
            print("✓ Soft Reset 指令已發送")
            print("  等待 200ms 讓晶片完成重置...")
            time.sleep(0.2)
            
            # 驗證重置後狀態
            print("\n驗證重置後的 System Status (含 LastData 驗證):")
            success, val = self.read_and_verify_status()
            if success:
                print(f"\n✓ 重置成功，System Status = 0x{val:04X}")
                return True
            else:
                print("✗ 無法讀取或驗證 System Status")
                return False
        else:
            print("✗ Soft Reset 失敗")
            return False

    def verify_checksum_calculation(self):
        """驗證校驗和計算函數是否正確"""
        print("\n" + "="*60)
        print("測試 3: 驗證 CS1/CS2 校驗和計算")
        print("="*60)
        
        # 測試 CS1 (使用規格書預設值)
        metering_regs = [
            0x0015,  # PLconstH
            0xD174,  # PLconstL
            0x0000, 0x0000, 0x0000, 0x0000,
            0x08BD,  # PStartTh
            0x0000, 0x0000, 0x0000,
            0x9422,  # MMode
        ]
        cs1 = self.calculate_cs1(metering_regs)
        print(f"\nCS1 計算結果: 0x{cs1:04X}")
        print(f"  輸入: PLconstH=0x0015, PLconstL=0xD174, PStartTh=0x08BD, MMode=0x9422")
        
        # 手動驗證過程
        print(f"\n  詳細計算過程:")
        sum_all = 0
        xor_all = 0
        for i, val in enumerate(metering_regs):
            h = (val >> 8) & 0xFF
            l = val & 0xFF
            sum_all = (sum_all + h + l) & 0xFF
            xor_all = xor_all ^ h ^ l
            if val != 0:
                print(f"    [0x{0x21+i:02X}]=0x{val:04X}: H=0x{h:02X}, L=0x{l:02X} → sum=0x{sum_all:02X}, xor=0x{xor_all:02X}")
        manual_cs1 = (xor_all << 8) | sum_all
        print(f"  最終: SUM_L=0x{sum_all:02X}, XOR_H=0x{xor_all:02X} → CS1=0x{manual_cs1:04X}")
        
        # 測試 CS2 (使用規格書預設值)
        measurement_regs = [
            0x6720,  # Ugain (規格書預設)
            0x7A13,  # IgainL
            0x7A13,  # IgainN
            0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
        ]
        cs2 = self.calculate_cs2(measurement_regs)
        print(f"\nCS2 計算結果: 0x{cs2:04X}")
        print(f"  輸入: Ugain=0x6720, IgainL=0x7A13, IgainN=0x7A13")
        
        # 詳細計算過程
        print(f"\n  詳細計算過程:")
        sum_all = 0
        xor_all = 0
        for i, val in enumerate(measurement_regs):
            h = (val >> 8) & 0xFF
            l = val & 0xFF
            sum_all = (sum_all + h + l) & 0xFF
            xor_all = xor_all ^ h ^ l
            if val != 0:
                print(f"    [0x{0x31+i:02X}]=0x{val:04X}: H=0x{h:02X}, L=0x{l:02X} → sum=0x{sum_all:02X}, xor=0x{xor_all:02X}")
        manual_cs2 = (xor_all << 8) | sum_all
        print(f"  最終: SUM_L=0x{sum_all:02X}, XOR_H=0x{xor_all:02X} → CS2=0x{manual_cs2:04X}")
        
        # 結果判斷
        if cs1 == 0x0000:
            print("\n⚠ 警告: CS1 = 0x0000 不正常! (全零參數不應產生零校驗和)")
            return False
        else:
            print(f"\n✓ CS1 計算正常: 0x{cs1:04X}")
        
        if cs2 == 0x0000:
            print("⚠ 警告: CS2 = 0x0000 不正常!")
            return False
        else:
            print(f"✓ CS2 計算正常: 0x{cs2:04X}")
        
        return True

    def test_full_initialization(self):
        """測試完整初始化流程"""
        print("\n" + "="*60)
        print("測試 4: 完整初始化流程")
        print("="*60)
        
        # Step 1: Soft Reset
        print("\nStep 1: Soft Reset")
        if not self.write_reg(0x00, 0x789A)[0]:
            print("✗ Soft Reset 失敗")
            return False
        time.sleep(0.2)
        
        # Step 2: 讀取並驗證 System Status
        print("\nStep 2: 讀取並驗證 System Status (含 LastData)")
        success, status = self.read_and_verify_status()
        if not success:
            print("✗ 無法讀取或驗證 System Status")
            return False
        print(f"✓ System Status = 0x{status:04X}")
        
        # Step 3: 寫入 FuncEn (功能啟用)
        print("\nStep 3: 寫入 FuncEn (0x02) - 功能啟用寄存器")
        funcen_val = 0x000C  # 預設值：不啟用中斷功能
        print(f"  設定 FuncEn = 0x{funcen_val:04X} (禁用電壓驟降/方向變化中斷)")
        if not self.write_reg(0x02, funcen_val)[0]:
            print("✗ 寫入 FuncEn 失敗")
            return False
        print("✓ FuncEn 已設定")
        
        # Step 3.1: 寫入 SagTh (電壓驟降閾值)
        print("\nStep 3.1: 寫入 SagTh (0x03) - 電壓驟降閾值")
        sagth_val = 0x1F2F
        print(f"  設定 SagTh = 0x{sagth_val:04X}")
        if not self.write_reg(0x03, sagth_val)[0]:
            print("✗ 寫入 SagTh 失敗")
            return False
        print("✓ SagTh 已設定")
        
        # Step 4: 開啟計量校正模式
        print("\nStep 4: 開啟計量校正模式 (寫入 0x5678 到 0x20)")
        if not self.write_reg(0x20, 0x5678)[0]:
            print("✗ 開啟校正模式失敗")
            return False
        
        # Step 5: 設定計量參數 (使用規格書推薦的預設值)
        print("\nStep 5: 設定計量參數 (21H-2BH)")
        # 使用規格書中的預設值
        metering_regs = [
            0x0015, # 0x00B9, # 0x0015,  # 21H PLconstH - 脈衝常數高位
            0xD174, # 0xC1F3, # 0xD174,  # 22H PLconstL - 脈衝常數低位
            0x0000, # 0x1D39, # 0x0000,  # 23H Lgain - L線增益
            0x0000,  # 24H Lphi - L線相位
            0x0000,  # 25H Ngain - N線增益
            0x0000,  # 26H Nphi - N線相位
            0x08BD,  # 27H PStartTh - 有功功率起動閉值
            0x0000,  # 28H PNOLTh - 有功功率無負載閉值
            0x0AEC, # 0x0000,  # 29H QStartTh - 無功功率起動閉值
            0x0000,  # 2AH QNOLTh - 無功功率無負載閉值
            0x9422,  # 2BH MMode - 計量模式配置
        ]
        
        reg_addrs = [0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B]
        for addr, val in zip(reg_addrs, metering_regs):
            print(f"準備寫入 Reg[0x{addr:02X}] = 0x{val:04X}")
            if not self.write_reg(addr, val, verbose=False)[0]:
                print(f"✗ 寫入 Reg[0x{addr:02X}] 失敗")
                return False
        print("✓ 計量參數已寫入 (使用規格書預設值)")
        
        # Step 6: 計算並寫入 CS1
        print("\nStep 6: 計算並寫入 CS1")
        cs1 = self.calculate_cs1(metering_regs)
        print(f"  計算的 CS1 = 0x{cs1:04X}")
        if not self.write_reg(0x2C, cs1)[0]:
            print("✗ 寫入 CS1 失敗")
            return False
        
        # Step 7: 驗證計量設定
        print("\nStep 7: 驗證計量設定 (寫入 0x8765 到 0x20)")
        if not self.write_reg(0x20, 0x8765)[0]:
            print("✗ 驗證計量設定失敗")
            return False
        time.sleep(0.05)
        
        # Step 7.1: 讀取並驗證 System Status (確認 CS1 正確)
        print("\nStep 7.1: 驗證 CS1 - 讀取 System Status (含 LastData)")
        success, status = self.read_and_verify_status()
        if not success:
            print("✗ 無法讀取或驗證 System Status")
            return False
        print(f"✓ System Status = 0x{status:04X}")
        
        # 檢查 CS1 錯誤位
        if status & 0x4000:
            print("⚠ 偵測到 CS1 校驗錯誤 (Bit 14)")
            return False
        
        # Step 7.2: 讀回 CalStart (0x20) 驗證
        print("\nStep 7.2: 讀回 CalStart (0x20) 驗證")
        success, calstart = self.read_reg(0x20)
        if not success:
            print("✗ 無法讀取 CalStart")
            return False
        print(f"✓ CalStart = 0x{calstart:04X}")
        if calstart != 0x8765:
            print(f"⚠ CalStart 值不符預期 (應為 0x8765)")
        
        # Step 8: 開啟測量校正模式
        print("\nStep 8: 開啟測量校正模式 (寫入 0x5678 到 0x30)")
        if not self.write_reg(0x30, 0x5678)[0]:
            print("✗ 開啟測量校正模式失敗")
            return False
        
        # Step 9: 設定測量參數 (使用規格書預設值)
        print("\nStep 9: 設定測量參數 (31H-3AH)")
        measurement_regs = [
            0x6720,  # 31H Ugain - 電壓增益 (規格書預設值)
            0x0A13,  # 32H IgainL - L線電流增益
            0x0A13,  # 33H IgainN - N線電流增益
            0x0000,  # 34H Uoffset - 電壓偏移量
            0x0000,  # 35H IoffsetL - L線電流偏移量
            0x0000,  # 36H IoffsetN - N線電流偏移量
            0x0000,  # 37H PoffsetL - L線有功功率偏移量
            0x0000,  # 38H QoffsetL - L線無功功率偏移量
            0x0000,  # 39H PoffsetN - N線有功功率偏移量
            0x0000,  # 3AH QoffsetN - N線無功功率偏移量
        ]
        
        print("  注意: Ugain 使用規格書預設值 0x0F00")
        print("  若 Urms 仍為 0，請檢查硬體分壓電路 (VP/VN 應在 120uV-600mV 範圍)")
        
        reg_addrs = [0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A]
        for addr, val in zip(reg_addrs, measurement_regs):
            print(f"準備寫入 Reg[0x{addr:02X}] = 0x{val:04X}")
            if not self.write_reg(addr, val, verbose=False)[0]:
                print(f"✗ 寫入 Reg[0x{addr:02X}] 失敗")
                return False
        print("✓ 測量參數已寫入")
        
        # Step 10: 計算並寫入 CS2
        print("\nStep 10: 計算並寫入 CS2")
        cs2 = self.calculate_cs2(measurement_regs)
        print(f"  計算的 CS2 = 0x{cs2:04X}")
        if not self.write_reg(0x3B, cs2)[0]:
            print("✗ 寫入 CS2 失敗")
            return False
        
        # Step 11: 驗證測量設定
        print("\nStep 11: 驗證測量設定 (寫入 0x8765 到 0x30)")
        if not self.write_reg(0x30, 0x8765)[0]:
            print("✗ 驗證測量設定失敗")
            return False
        time.sleep(0.1)
        
        # Step 11.1: 讀取並驗證 System Status (確認 CS2 正確)
        print("\nStep 11.1: 驗證 CS2 - 讀取 System Status (含 LastData)")
        success, status = self.read_and_verify_status()
        if not success:
            print("✗ 無法讀取或驗證 System Status")
            return False
        print(f"✓ System Status = 0x{status:04X}")
        
        # 檢查 CS2 錯誤位
        if status & 0x2000:
            print("⚠ 偵測到 CS2 校驗錯誤 (Bit 13)")
            return False
        
        # Step 11.2: 讀回 AdjStart (0x30) 驗證
        print("\nStep 11.2: 讀回 AdjStart (0x30) 驗證")
        success, adjstart = self.read_reg(0x30)
        if not success:
            print("✗ 無法讀取 AdjStart")
            return False
        print(f"✓ AdjStart = 0x{adjstart:04X}")
        if adjstart != 0x8765:
            print(f"⚠ AdjStart 值不符預期 (應為 0x8765)")
        
        # Step 12: 最終驗證
        print("\nStep 12: 最終驗證 - 讀取並驗證 System Status (含 LastData)")
        success, status = self.read_and_verify_status()
        if not success:
            print("✗ 無法讀取或驗證最終狀態")
            return False
        
        print(f"\n✓ 最終 System Status = 0x{status:04X}")
        
        # 檢查錯誤位
        errors = []
        if status & 0x8000:
            errors.append("系統錯誤 (Bit 15)")
        if status & 0x4000:
            errors.append("CS1 校驗錯誤 (Bit 14)")
        if status & 0x2000:
            errors.append("CS2 校驗錯誤 (Bit 13)")
        
        if errors:
            print("⚠ 偵測到錯誤:")
            for err in errors:
                print(f"  - {err}")
            return False
        else:
            print("✓ 初始化完全成功！")
            return True

    def test_read_measurements(self):
        """測試讀取測量值"""
        print("\n" + "="*60)
        print("測試 4: 讀取測量暫存器")
        print("="*60)
        
        registers = {
            0x48: "Irms (L線電流)",
            0x49: "Urms (電壓)",
            0x4A: "Pmean (有效功率)",
            0x4B: "Qmean (無效功率)",
            0x4C: "Freq (頻率)",
            0x4D: "PowerF (功率因數)",
            0x4E: "Pangle (相位角)",
            0x4F: "Smean (視在功率)",
            0x68: "Irms2 (N線電流)",
            0x6A: "Pmean2 (N線有效功率)",
        }
        
        print()
        success_count = 0
        for addr, name in registers.items():
            success, val = self.read_reg(addr)
            if success:
                success_count += 1
                # 特殊格式化
                if addr == 0x49:  # 電壓
                    print(f"  {name:20s} = {val/100:.2f} V")
                elif addr in [0x48, 0x68]:  # 電流
                    print(f"  {name:20s} = {val/1000:.3f} A")
                elif addr == 0x4C:  # 頻率
                    print(f"  {name:20s} = {val/100:.2f} Hz")
                else:
                    print(f"  {name:20s} = {val} (0x{val:04X})")
        
        print(f"\n✓ 成功讀取 {success_count}/{len(registers)} 個暫存器")
        return success_count > 0

    def run_all_tests(self):
        """執行所有測試"""
        print("\n" + "🔧"*30)
        print("ATM90E26 完整測試程式")
        print("🔧"*30)
        
        results = []
        
        # 測試 1: 基本通訊
        results.append(("基本通訊", self.test_basic_communication()))
        
        if not results[-1][1]:
            print("\n❌ 基本通訊失敗，後續測試無法執行")
            print("請檢查:")
            print("  1. UART 連線是否正確")
            print("  2. ATM90E26 電源是否正常")
            print("  3. TX/RX 線是否對調")
            return
        
        # 測試 2: 軟體重置
        results.append(("軟體重置", self.test_soft_reset()))
        
        # 測試 3: 校驗和計算驗證
        results.append(("校驗和計算", self.verify_checksum_calculation()))
        
        # 測試 4: 完整初始化
        results.append(("完整初始化", self.test_full_initialization()))
        
        # 測試 5: 讀取測量值
        results.append(("讀取測量值", self.test_read_measurements()))
        
        # 總結
        print("\n" + "="*60)
        print("測試總結")
        print("="*60)
        for name, result in results:
            status = "✓ 通過" if result else "✗ 失敗"
            print(f"  {name:15s}: {status}")
        
        passed = sum(1 for _, r in results if r)
        print(f"\n總計: {passed}/{len(results)} 項測試通過")

    def close(self):
        """關閉串口"""
        self.ser.close()
        print("\n✓ 串口已關閉")


def main():
    parser = argparse.ArgumentParser(
        description='ATM90E26 測試程式',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
預設配置:
  串口: {DEFAULT_PORT}
  鮑率: {DEFAULT_BAUDRATE}

使用範例:
  python test_atm90e26.py                    # 使用預設串口執行所有測試
  python test_atm90e26.py -p COM3            # 指定串口
  python test_atm90e26.py -t comm            # 只執行通訊測試
  python test_atm90e26.py -t rreg 0x20       # 讀取寄存器 0x20
  python test_atm90e26.py -t wreg 0x20 0x5678  # 寫入 0x5678 到寄存器 0x20
  python test_atm90e26.py -p COM3 -b 115200  # 自訂串口和鮑率
        """)
    parser.add_argument('-p', '--port', default=DEFAULT_PORT,
                        help=f'串口名稱 (預設: {DEFAULT_PORT})')
    parser.add_argument('-b', '--baudrate', type=int, default=DEFAULT_BAUDRATE,
                        help=f'鮑率 (預設: {DEFAULT_BAUDRATE})')
    parser.add_argument('--swap-endian', action='store_true',
                        help='交換 MSB/LSB 字節序 (切換為 Little-Endian)')
    parser.add_argument('-t', '--test', nargs='*', default=['all'],
                        help='選擇要執行的測試: all, comm, reset, checksum, init, read, rreg <addr>, wreg <addr> <value>')
    
    args = parser.parse_args()
    
    tester = ATM90E26_Tester(args.port, args.baudrate, swap_endian=args.swap_endian)
    
    try:
        test_cmd = args.test[0] if args.test else 'all'
        
        if test_cmd == 'all':
            tester.run_all_tests()
        elif test_cmd == 'comm':
            tester.test_basic_communication()
        elif test_cmd == 'reset':
            tester.test_soft_reset()
        elif test_cmd == 'checksum':
            tester.verify_checksum_calculation()
        elif test_cmd == 'init':
            tester.test_full_initialization()
        elif test_cmd == 'read':
            tester.test_read_measurements()
        elif test_cmd == 'rreg':
            # 讀取寄存器: -t rreg 0x20
            if len(args.test) < 2:
                print("✗ 錯誤: rreg 需要指定寄存器地址")
                print("使用方式: -t rreg 0x20")
            else:
                addr_str = args.test[1]
                addr = int(addr_str, 16) if addr_str.startswith('0x') else int(addr_str)
                print(f"\n讀取寄存器 0x{addr:02X}:")
                success, value = tester.read_reg(addr)
                if success:
                    print(f"\n✓ 成功讀取: Reg[0x{addr:02X}] = 0x{value:04X} ({value})")
                else:
                    print(f"\n✗ 讀取失敗")
        elif test_cmd == 'wreg':
            # 寫入寄存器: -t wreg 0x20 0x5678
            if len(args.test) < 3:
                print("✗ 錯誤: wreg 需要指定寄存器地址和數值")
                print("使用方式: -t wreg 0x20 0x5678")
            else:
                addr_str = args.test[1]
                value_str = args.test[2]
                addr = int(addr_str, 16) if addr_str.startswith('0x') else int(addr_str)
                value = int(value_str, 16) if value_str.startswith('0x') else int(value_str)
                print(f"\n寫入寄存器 0x{addr:02X}:")
                success, _ = tester.write_reg(addr, value)
                if success:
                    print(f"\n✓ 成功寫入: Reg[0x{addr:02X}] = 0x{value:04X}")
                    # 讀回驗證
                    print(f"\n讀回驗證:")
                    success_r, value_r = tester.read_reg(addr)
                    if success_r:
                        if value_r == value:
                            print(f"✓ 驗證成功: 讀取值 = 0x{value_r:04X}")
                        else:
                            print(f"⚠ 驗證失敗: 寫入 0x{value:04X}, 讀取 0x{value_r:04X}")
                else:
                    print(f"\n✗ 寫入失敗")
        else:
            print(f"✗ 未知的測試命令: {test_cmd}")
            print("可用命令: all, comm, reset, checksum, init, read, rreg, wreg")
    except KeyboardInterrupt:
        print("\n\n⚠ 測試被中斷")
    finally:
        tester.close()


if __name__ == "__main__":
    main()
