import argparse

def crc16_ccitt(data: bytes) -> int:
    """
    Calculates CRC16-CCITT (False/XModem)
    Poly: 0x1021, Init: 0xFFFF, RefIn: False, RefOut: False, XorOut: 0x0000
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

def main():
    parser = argparse.ArgumentParser(description="Calculate Protocol CRC16")
    parser.add_argument("hex_string", nargs="+", help="Hex bytes string (e.g. 02 03 00 E1 00 03)")
    args = parser.parse_args()

    # Parse and concatenate input arguments
    hex_str = "".join(args.hex_string).replace(" ", "")
    
    try:
        data = bytes.fromhex(hex_str)
    except ValueError:
        print("Error: Invalid hex string")
        return

    # Calculate CRC (should include LEN_L...ETX)
    # If the user provides the full raw packet starting with 02, we should skip STX (1 byte)
    # The protocol expectation is calculating CRC on the part AFTER [STX].
    # But for flexibility, let's just calculate on whatever is provided.
    
    # Check if STX is present at the start
    calc_data = data
    if len(data) > 0 and data[0] == 0x02:
        print("Note: STX (0x02) detected at start. Skipping STX for CRC calculation.")
        calc_data = data[1:]
    
    crc = crc16_ccitt(calc_data)
    
    print(f"\nData: {calc_data.hex(' ').upper()}")
    print(f"CRC16: 0x{crc:04X} (Little Endian: {crc & 0xFF:02X} {(crc >> 8) & 0xFF:02X})")
    
    # Construct Full Packet Suggestion
    full_packet = bytearray([0x02] if (len(data) > 0 and data[0] != 0x02) else [])
    full_packet.extend(data)
    full_packet.append(crc & 0xFF)
    full_packet.append((crc >> 8) & 0xFF)
    
    print(f"Full Packet: {full_packet.hex().upper()}")

if __name__ == "__main__":
    main()
