"""BLE Manager for TJ3 Device Communication"""

import asyncio
import logging
from typing import Callable, Optional, Dict
from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

from constants import DEVICE_NAME, CHAR_PROPERTIES

logger = logging.getLogger(__name__)


class TJ3BLEManager:
    """Manages BLE connection and communication with TJ3 device"""
    
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.device_address: Optional[str] = None
        self.characteristics: Dict[str, BleakGATTCharacteristic] = {}
        self.notification_callbacks: Dict[str, Callable] = {}
        self.last_error: str = ""
        
    async def scan_devices(self, timeout: float = 5.0) -> list:
        """
        Scan for all nearby BLE devices.
        Returns list of dicts: {"name", "address", "rssi", "is_tj3", "device"}
        sorted by RSSI descending (strongest first).
        """
        logger.info(f"Scanning for BLE devices (timeout: {timeout}s)...")
        result: list[dict] = []
        try:
            # bleak >= 0.20: return_adv gives AdvertisementData with rssi
            devices_adv: dict = await BleakScanner.discover(
                timeout=timeout, return_adv=True
            )
            for _addr, (device, adv) in devices_adv.items():
                result.append({
                    "name":    device.name or "(unknown)",
                    "address": device.address,
                    "rssi":    adv.rssi if adv.rssi is not None else -999,
                    "is_tj3":  bool(device.name and DEVICE_NAME in device.name),
                    "device":  device,
                })
        except TypeError:
            # Older bleak without return_adv
            devices = await BleakScanner.discover(timeout=timeout)
            for device in devices:
                result.append({
                    "name":    device.name or "(unknown)",
                    "address": device.address,
                    "rssi":    getattr(device, 'rssi', -999),
                    "is_tj3":  bool(device.name and DEVICE_NAME in device.name),
                    "device":  device,
                })

        result.sort(key=lambda x: x["rssi"], reverse=True)
        tj3_count = sum(1 for d in result if d["is_tj3"])
        logger.info(f"Found {len(result)} device(s) total, {tj3_count} TJ3 device(s)")
        return result
    
    async def connect(self, address: str) -> bool:
        """Connect to TJ3 device"""
        # Clean up any existing connection first
        if self.client:
            try:
                if self.client.is_connected:
                    await self.client.disconnect()
            except:
                pass
            self.client = None
        
        try:
            logger.info(f"Connecting to {address}...")
            
            # Create new client instance
            self.client = BleakClient(
                address,
                disconnected_callback=self._on_disconnect
            )
            
            # Connect with timeout
            await asyncio.wait_for(self.client.connect(), timeout=10.0)
            self.device_address = address
            
            # Small delay to ensure connection is stable
            await asyncio.sleep(0.5)
            
            # Discover and cache characteristics
            await self._discover_characteristics()
            
            logger.info(f"Connected successfully to {address}")
            return True
            
        except asyncio.TimeoutError:
            logger.error(f"Connection timeout to {address}")
            await self._cleanup_connection()
            return False
        except Exception as e:
            logger.error(f"Connection failed: {e}")
            await self._cleanup_connection()
            return False
    
    def _on_disconnect(self, client):
        """Handle unexpected disconnection"""
        logger.warning(f"Device disconnected unexpectedly")
        # Note: Don't call async cleanup here, will be handled by disconnect()
    
    async def disconnect(self):
        """Disconnect from device"""
        if self.client:
            try:
                if self.client.is_connected:
                    await self.client.disconnect()
                    logger.info("Disconnected successfully")
            except Exception as e:
                logger.warning(f"Disconnect error (ignoring): {e}")
        
        await self._cleanup_connection()
    
    async def _cleanup_connection(self):
        """Clean up connection state"""
        # Stop all notifications
        if self.client and self.client.is_connected:
            for uuid in list(self.notification_callbacks.keys()):
                try:
                    char = self.characteristics.get(uuid.lower())
                    if char:
                        await self.client.stop_notify(char)
                except:
                    pass
        
        # Clear all state
        self.notification_callbacks.clear()
        self.characteristics.clear()
        self.client = None
        self.device_address = None
        
        # Give device time to restart advertising
        await asyncio.sleep(0.5)
    
    def is_connected(self) -> bool:
        """Check if device is connected"""
        return self.client is not None and self.client.is_connected
    
    async def _discover_characteristics(self):
        """Discover and cache all characteristics"""
        if not self.client:
            return
        
        self.characteristics.clear()
        for service in self.client.services:
            for char in service.characteristics:
                uuid = char.uuid.lower()
                self.characteristics[uuid] = char
                logger.debug(f"Discovered: {uuid} - {char.properties}")
    
    def get_characteristics(self) -> Dict[str, dict]:
        """Get all discovered characteristics with their properties"""
        result = {}
        for uuid, char in self.characteristics.items():
            props = CHAR_PROPERTIES.get(uuid, {})
            result[uuid] = {
                "name": props.get("name", "Unknown"),
                "properties": char.properties,
                "handle": char.handle,
                "size": props.get("size"),
            }
        return result
    
    async def read_characteristic(self, uuid: str) -> Optional[bytes]:
        """Read value from characteristic"""
        if not self.is_connected():
            logger.error("Not connected")
            return None
        
        try:
            char = self.characteristics.get(uuid.lower())
            if not char:
                logger.error(f"Characteristic {uuid} not found")
                return None
            
            value = await self.client.read_gatt_char(char)
            logger.info(f"Read {uuid}: {value.hex()}")
            return value
        except Exception as e:
            logger.error(f"Read failed for {uuid}: {e}")
            return None
    
    async def write_characteristic(self, uuid: str, data: bytes, response: bool = True) -> bool:
        """Write value to characteristic"""
        self.last_error = ""
        if not self.is_connected():
            self.last_error = "Not connected"
            logger.error(self.last_error)
            return False

        try:
            char = self.characteristics.get(uuid.lower())
            if not char:
                self.last_error = f"Characteristic {uuid} not found"
                logger.error(self.last_error)
                return False

            await self.client.write_gatt_char(char, data, response=response)
            logger.info(f"Write {uuid}: {data.hex()}")
            return True
        except Exception as e:
            self.last_error = str(e)
            logger.error(f"Write failed for {uuid}: {e}")
            return False
    
    async def enable_notification(self, uuid: str, callback: Callable):
        """Enable notifications for characteristic"""
        if not self.is_connected():
            logger.error("Not connected")
            return False
        
        try:
            char = self.characteristics.get(uuid.lower())
            if not char:
                logger.error(f"Characteristic {uuid} not found")
                return False
            
            def notification_handler(sender: BleakGATTCharacteristic, data: bytearray):
                logger.debug(f"Notification from {uuid}: {bytes(data).hex()}")
                if callback:
                    callback(bytes(data))
            
            await self.client.start_notify(char, notification_handler)
            self.notification_callbacks[uuid] = callback
            logger.info(f"Enabled notifications for {uuid}")
            return True
        except Exception as e:
            logger.error(f"Enable notification failed for {uuid}: {e}")
            return False
    
    async def disable_notification(self, uuid: str):
        """Disable notifications for characteristic"""
        if not self.is_connected():
            return
        
        try:
            char = self.characteristics.get(uuid.lower())
            if char and uuid in self.notification_callbacks:
                await self.client.stop_notify(char)
                del self.notification_callbacks[uuid]
                logger.info(f"Disabled notifications for {uuid}")
        except Exception as e:
            logger.error(f"Disable notification failed for {uuid}: {e}")
