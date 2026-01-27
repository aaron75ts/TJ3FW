import serial
import time

class ATM90E26_Driver:
    def __init__(self, port='COM3', baudrate=9600):
        # 8-bit data, no parity, 1 stop bit [4, 5]
        self.ser = serial.Serial(port, baudrate, timeout=0.05)
        time.sleep(0.5)

    def _calc_host_checksum(self, addr, lsb, msb):
        """計算寫入電文的 Checksum: Addr + LSB + MSB [6]"""
        return (addr + lsb + msb) & 0xFF

    def write_reg(self, addr, value):
        """寫入暫存器 (Addr Bit7 = 0) [4, 7]
        返回 True 表示成功，False 表示失敗"""
        lsb = value & 0xFF
        msb = (value >> 8) & 0xFF
        chk = self._calc_host_checksum(addr, lsb, msb)
        payload = bytes([0xFE, addr, lsb, msb, chk])
        
        hex_tx = " ".join(f"{b:02X}" for b in payload)
        print(f"TX: {hex_tx}  (寫入 0x{addr:02X} = 0x{value:04X})")
        self.ser.write(payload)
        
        # 晶片確認正確後回傳 1 byte Checksum [7]
        res = self.ser.read(1)
        if res and len(res) == 1:
            print(f"RX: {res[0]:02X}  (OK)")
            time.sleep(0.025) # 交易間隔需 > 20ms [8]
            return True
        else:
            print(f"RX: Timeout 或失敗")
            time.sleep(0.025)
            return False

    def read_reg(self, addr):
        """讀取暫存器 (Addr Bit7 = 1) [4, 9]
        返回讀取到的值，失敗返回 None"""
        read_addr = addr | 0x80
        payload = bytes([0xFE, read_addr, read_addr])
        
        hex_tx = " ".join(f"{b:02X}" for b in payload)
        print(f"TX: {hex_tx}  讀取 0x{addr:02X}")
        self.ser.write(payload)
        
        # 晶片回傳 LSB + MSB + Checksum [9]
        res = self.ser.read(3)
        if len(res) == 3:
            val = (res[1] << 8) | res[0]
            print(f"RX: {res[0]:02X} {res[1]:02X} {res[2]:02X}  值=0x{val:04X}, checksum=0x{res[2]:02X}")
            time.sleep(0.025)
            return val
        else:
            print("RX: Timeout")
            time.sleep(0.025)
            return None

    def calculate_cs1(self, regs_21_to_2B):
        """計算 CS1 (2CH): 21H~2BH 的 Sum 與 XOR [3]"""
        sum_l = 0
        xor_h = 0
        for val in regs_21_to_2B:
            h, l = (val >> 8) & 0xFF, val & 0xFF
            sum_l = (sum_l + h + l) & 0xFF
            xor_h = xor_h ^ h ^ l
        return (xor_h << 8) | sum_l

    def soft_reset(self):
        """執行軟體重置，特殊處理：可能無回應或回應不可靠"""
        addr = 0x00
        value = 0x789A
        lsb = value & 0xFF
        msb = (value >> 8) & 0xFF
        chk = self._calc_host_checksum(addr, lsb, msb)
        payload = bytes([0xFE, addr, lsb, msb, chk])
        
        hex_tx = " ".join(f"{b:02X}" for b in payload)
        print(f"TX: {hex_tx}  (軟體重置 0x{addr:02X} = 0x{value:04X})")
        self.ser.write(payload)
        self.ser.flush()
        
        # 軟體重置可能不會回應，或晶片立即重置
        # 嘗試讀取但不視為錯誤
        res = self.ser.read(1)
        if res and len(res) == 1:
            print(f"RX: {res[0]:02X}  (收到回應)")
        else:
            print(f"RX: 無回應 (正常，晶片正在重置)")
        
        time.sleep(0.2)  # 等待 POR 完成，建議 >100ms 較保險
        return True  # 軟體重置總是返回成功

    def init_sequence(self):
        """初始化序列，失敗時返回 False"""
        print("--- [10] 系統重置 (Soft Reset) ---")
        # 軟體重置使用特殊方法，因為可能無回應
        self.soft_reset()

        print("\n--- [11] 讀取 System Status Register (0x01) ---")
        status_val = self.read_reg(0x01)
        
        if status_val is None:
            print("❌ 讀取 System Status 失敗！")
            return False
        
        print(f"✓ System Status: 0x{status_val:04X}")

        print("\n--- [12] 計量校正初始化 (Metering Config) ---")
        # 寫入 5678H 到 CalStart (20H) 開啟校正模式 [2, 15]
        if not self.write_reg(0x20, 0x5678):
            print("❌ 開啟校正模式失敗！")
            return False
        
        # 設定關鍵參數 (以下為式樣書範例值) [16, 17]
        if not self.write_reg(0x21, 0x0015): # PLconstH
            print("❌ 寫入 PLconstH 失敗！")
            return False
        if not self.write_reg(0x22, 0xD174): # PLconstL
            print("❌ 寫入 PLconstL 失敗！")
            return False
        if not self.write_reg(0x2B, 0x7C22): # MMode
            print("❌ 寫入 MMode 失敗！")
            return False
        
        # 填入 CS1 校驗和 (此處簡化計算，實際需包含 21H~2BH) [3, 18]
        # 範例僅計算 21H, 22H, 2BH 其餘為 0
        cs1 = self.calculate_cs1([0x0015, 0xD174, 0, 0, 0, 0, 0, 0, 0, 0, 0x7C22])
        if not self.write_reg(0x2C, cs1):
            print("❌ 寫入 CS1 失敗！")
            return False
        
        # 驗證計量設定: 寫入 8765H [2, 18]
        if not self.write_reg(0x20, 0x8765):
            print("❌ 驗證計量設定失敗！")
            return False
        
        print("✓ 初始化完成！\n")
        return True

    def main_loop(self):
        print("\n--- [19] 執行循環讀寫指令 ---")
        try:
            while True:
                self.read_reg(0x48)          # 讀取 0x48 (Irms)
                self.read_reg(0x49)          # 讀取 0x49 (Urms)
                self.write_reg(0x30, 0x5678) # 進入量測校正模式 [20]
                self.write_reg(0x20, 0x5678) # 再次進入計量模式 [2]
                self.write_reg(0x33, 0x0693) # IgainN
                self.write_reg(0x31, 0x7A22) # Ugain
                self.write_reg(0x32, 0x0CB4) # IgainL
                self.read_reg(0x3B)          # 讀取 0x3B (CS2)
                self.write_reg(0x3B, 0x75F5) # 寫入校驗和 CS2 [21]
                self.write_reg(0x30, 0x8765) # 驗證量測設定 [20]
                self.read_reg(0x68)          # 讀取 0x68 (Irms2)
                
                print("-" * 40)
                time.sleep(2)
        except KeyboardInterrupt:
            print("Stop.")

if __name__ == "__main__":
    meter = ATM90E26_Driver(port='/dev/cu.usbserial-AG0KBCCV')
    
    while True:
        print(f"\n{'='*50}")
        print(f"🔄 重新嘗試初始化)")
        
        if meter.init_sequence():
            print("✅ 初始化成功，開始主循環\n")
            meter.main_loop()
            break
        else:
            print(f"\n⚠️ 初始化失敗，等待 1 秒後重試...\n")
            time.sleep(1)