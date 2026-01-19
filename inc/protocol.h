#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Op Codes
#define PROTOCOL_OP_LOG 0xC0
#define PROTOCOL_OP_MQTT 0xC1
#define PROTOCOL_OP_SETTINGS_PUSH 0xC2
#define PROTOCOL_OP_SETTINGS_READ 0xD0
#define PROTOCOL_OP_NET_STATUS 0xD1
#define PROTOCOL_OP_RTC_SYNC 0xD2

    // Function prototypes
    int protocol_init(void);

    // Command senders
    void protocol_send_log(const char *fmt, ...);
    void protocol_send_mqtt(const char *topic, const char *json_content);
    void protocol_send_settings(const char *json_settings);

    // Handlers for received commands (weak or callback based, implemented in main or logic)
    void protocol_handle_settings_read(void);
    void protocol_handle_net_status(uint8_t status);
    void protocol_handle_rtc_sync(uint64_t timestamp);

#ifdef __cplusplus
}
#endif

#endif // PROTOCOL_H
