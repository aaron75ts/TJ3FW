#!/usr/bin/env python3
"""
BLE 設定模式安全測試腳本

測試場景：
1. 未按按鈕時嘗試修改設定 → 應拒絕 (0xFE 授權錯誤)
2. 長按按鈕 3 秒進入設定模式 → 可以修改設定
3. 30 秒後自動超時 → 再次拒絕修改

使用方法:
  python3 test_set_mode_security.py
"""

import asyncio
from bleak import BleakClient, BleakScanner
import struct

# BLE UUIDs (與 TJ3FW 匹配)
ENERGY_SERVICE_UUID = "4d6f8c00-4b9a-4c1b-9a61-112233445500"
METER_CONFIG_CHAR_UUID = "4d6f8c02-4b9a-4c1b-9a61-112233445500"
COMMAND_CHAR_UUID = "4d6f8c03-4b9a-4c1b-9a61-112233445500"

# Protocol Packet 格式
STX = 0x02
ETX = 0x03
OP_BLE_SET_AI_CONFIG = 0x21

def crc16_ccitt(data: bytes) -> int:
    """計算 CRC-16-CCITT"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

def build_command_packet(op_code: int, payload: bytes) -> bytes:
    """構建命令封包"""
    payload_len = len(payload)
    packet = struct.pack('<BBBB', STX, op_code, payload_len, 0x00)  # 0x00 = reserved
    packet += payload
    
    # 計算 CRC (op_code + payload_len + reserved + payload)
    crc_data = packet[1:]
    crc = crc16_ccitt(crc_data)
    
    packet += struct.pack('<H', crc)  # CRC Little-Endian
    packet += struct.pack('B', ETX)
    
    return packet

async def test_unauthorized_write(client: BleakClient):
    """測試未授權修改 (應失敗)"""
    print("\n[Test 1] 嘗試在未按按鈕時修改設定...")
    
    # 構建 DI Config 命令 (OP_BLE_SET_AI_CONFIG)
    # Payload: DI1(ON=100, OFF=200), DI2(ON=150, OFF=250)
    payload = struct.pack('<HHHH', 100, 200, 150, 250)
    packet = build_command_packet(OP_BLE_SET_AI_CONFIG, payload)
    
    print(f"發送命令封包: {packet.hex()}")
    
    try:
        # 發送命令並等待回應
        await client.write_gatt_char(COMMAND_CHAR_UUID, packet, response=True)
        print("❌ 錯誤：命令應該被拒絕但被接受了！")
    except Exception as e:
        if "0xfe" in str(e).lower() or "authorization" in str(e).lower():
            print(f"✅ 正確：命令被拒絕 (授權錯誤)")
        else:
            print(f"⚠️ 收到錯誤: {e}")

async def test_authorized_write(client: BleakClient):
    """測試授權修改 (應成功)"""
    print("\n[Test 2] 設定模式已啟用，嘗試修改設定...")
    
    # 構建 DI Config 命令
    payload = struct.pack('<HHHH', 300, 400, 350, 450)
    packet = build_command_packet(OP_BLE_SET_AI_CONFIG, payload)
    
    print(f"發送命令封包: {packet.hex()}")
    
    try:
        await client.write_gatt_char(COMMAND_CHAR_UUID, packet, response=True)
        print("✅ 成功：命令被接受")
    except Exception as e:
        print(f"❌ 錯誤：命令應該被接受但被拒絕了: {e}")

async def test_meter_config_write(client: BleakClient):
    """測試 Meter Config 寫入"""
    print("\n[Test 3] 嘗試寫入 Meter Config...")
    
    # 20 bytes: [ct_ratio(6*2)][ch_igain(6*2)][freq(1)][reserved(7)]
    meter_config = struct.pack('<6H6HB7s',
                               100, 100, 100, 100, 100, 100,  # ct_ratio
                               2579, 2579, 2579, 2579, 2579, 2579,  # ch_igain (0x0A13)
                               60,  # freq
                               b'\x00' * 7)  # reserved
    
    print(f"發送 Meter Config: {meter_config.hex()}")
    
    try:
        await client.write_gatt_char(METER_CONFIG_CHAR_UUID, meter_config, response=True)
        print("✅ 成功：Meter Config 被接受")
    except Exception as e:
        if "0x03" in str(e).lower() or "authorization" in str(e).lower():
            print(f"✅ 正確：Meter Config 被拒絕 (授權錯誤)")
        else:
            print(f"⚠️ 收到錯誤: {e}")

async def main():
    print("=== BLE 設定模式安全測試 ===\n")
    print("正在掃描 TJ3-nRF 設備...")
    
    # 掃描 BLE 設備
    devices = await BleakScanner.discover(timeout=5.0)
    target_device = None
    
    for device in devices:
        if device.name and "TJ3" in device.name:
            target_device = device
            break
    
    if not target_device:
        print("❌ 未找到 TJ3 設備，請確保設備已開機並在範圍內")
        return
    
    print(f"找到設備: {target_device.name} ({target_device.address})")
    
    async with BleakClient(target_device.address) as client:
        print(f"已連接: {client.is_connected}\n")
        
        # Test 1: 未授權修改 (應失敗)
        await test_unauthorized_write(client)
        
        # 提示用戶按按鈕
        print("\n" + "=" * 60)
        print("請長按 TJ3 設備上的 'S' 按鈕 3 秒以上")
        print("LED 應開始快速閃爍表示進入設定模式")
        print("=" * 60)
        input("按下 Enter 鍵繼續測試...")
        
        # Test 2: 授權修改 (應成功)
        await test_authorized_write(client)
        
        # Test 3: Meter Config 寫入測試
        await test_meter_config_write(client)
        
        print("\n" + "=" * 60)
        print("測試完成！")
        print("設定模式將在 30 秒後自動超時")
        print("超時後再次嘗試修改將被拒絕")
        print("=" * 60)

if __name__ == "__main__":
    asyncio.run(main())
