"""TJ3 BLE Service and Characteristic UUIDs"""

# Energy Service
ENERGY_SERVICE_UUID = "4d6f8c00-4b9a-4c1b-9a61-112233445500"
ENERGY_SNAPSHOT_UUID = "4d6f8c01-4b9a-4c1b-9a61-112233445500"  # READ + NOTIFY
ENERGY_CONFIG_UUID = "4d6f8c02-4b9a-4c1b-9a61-112233445500"    # READ + WRITE
ENERGY_CMD_UUID = "4d6f8c03-4b9a-4c1b-9a61-112233445500"       # WRITE + INDICATE
ENERGY_PULSE_UUID = "4d6f8c04-4b9a-4c1b-9a61-112233445500"     # READ + NOTIFY
ENERGY_ALARM_UUID = "4d6f8c05-4b9a-4c1b-9a61-112233445500"     # READ + NOTIFY

# Diagnostics Service
DIAG_SERVICE_UUID = "4d6f8c10-4b9a-4c1b-9a61-112233445500"
DIAG_LOG_COUNT_UUID = "4d6f8c11-4b9a-4c1b-9a61-112233445500"   # READ
DIAG_LOG_FETCH_UUID = "4d6f8c12-4b9a-4c1b-9a61-112233445500"   # WRITE + INDICATE
DIAG_LOG_LEVEL_UUID = "4d6f8c13-4b9a-4c1b-9a61-112233445500"   # READ + WRITE
DIAG_CLEAR_LOGS_UUID = "4d6f8c14-4b9a-4c1b-9a61-112233445500"  # WRITE + INDICATE
DIAG_LOG_STREAM_UUID = "4d6f8c15-4b9a-4c1b-9a61-112233445500"  # NOTIFY

# Characteristic properties mapping
CHAR_PROPERTIES = {
    ENERGY_SNAPSHOT_UUID: {"read": True, "notify": True, "name": "Meter Snapshot", "size": 20},
    ENERGY_CONFIG_UUID: {"read": True, "write": True, "name": "Meter Config", "size": 24},
    ENERGY_CMD_UUID: {"write": True, "indicate": True, "name": "Command", "size": None},
    ENERGY_PULSE_UUID: {"read": True, "notify": True, "name": "Power Pulse", "size": 4},
    ENERGY_ALARM_UUID: {"read": True, "notify": True, "name": "Alarm Status", "size": 4},
    DIAG_LOG_COUNT_UUID: {"read": True, "name": "Log Count", "size": 2},
    DIAG_LOG_FETCH_UUID: {"write": True, "indicate": True, "name": "Log Fetch", "size": None},
    DIAG_LOG_LEVEL_UUID: {"read": True, "write": True, "name": "Log Level", "size": 1},
    DIAG_CLEAR_LOGS_UUID: {"write": True, "indicate": True, "name": "Clear Logs", "size": None},
    DIAG_LOG_STREAM_UUID: {"notify": True, "name": "Log Stream", "size": None},
}

# Device name to scan for
DEVICE_NAME = "TJ3-GW"
