#include "mqtt.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

#define MQTT_TOPIC_PREFIX "kyokuto"
#define MQTT_ID_LEN 6
#define MQTT_CODE_LEN 2
#define MQTT_SEQ_LEN 2
#define MQTT_NUM_LEN 2
#define MQTT_STX "\x02"
#define MQTT_ETX "\x03"

void mqtt_init(void)
{
    // Nothing special to init for now
}

int mqtt_publish_generic(const char *main_id, const char *peri_id, const char *code,
                         const char *seq, const char *num, const char *data)
{
    if (!main_id || !peri_id || !code || !seq || !num)
    {
        protocol_send_log("[ERR] Invalid arguments for mqtt_publish_generic");
        return -1;
    }

    if (strlen(main_id) != MQTT_ID_LEN || strlen(peri_id) != MQTT_ID_LEN)
    {
        protocol_send_log("[ERR] ID length mismatch (expected 6)");
        return -1;
    }

    char topic[128];
    // Format: kyokuto/[MainID]/[PeriID]/[Code]
    int ret = snprintf(topic, sizeof(topic), "%s/%s/%s/%s",
                       MQTT_TOPIC_PREFIX, main_id, peri_id, code);

    if (ret < 0 || ret >= sizeof(topic))
    {
        protocol_send_log("[ERR] Topic construction failed or truncated");
        return -1;
    }

    // Construct Payload content
    // Format: \x02[PeriID][Code][Seq][Num][MainID][Data]\x03
    // Note: The STX and ETX are part of the "Content" sent via protocol.c logic?
    // protocol_send_mqtt takes (topic, json_content).
    // The "json_content" param name in protocol.c is misleading based on new docs.
    // It should be just "content".

    char payload[512];
    int data_len = data ? strlen(data) : 0;

    // Check buffer size
    // 1(STX) + 6(Peri) + 2(Code) + 2(Seq) + 2(Num) + 6(Main) + DataLen + 1(ETX) + 1(Null)
    if (1 + MQTT_ID_LEN + MQTT_CODE_LEN + MQTT_SEQ_LEN + MQTT_NUM_LEN + MQTT_ID_LEN + data_len + 1 + 1 > sizeof(payload))
    {
        protocol_send_log("[ERR] Payload buffer too small");
        return -1;
    }

    strcpy(payload, MQTT_STX);
    strcat(payload, peri_id);
    strcat(payload, code);
    strcat(payload, seq);
    strcat(payload, num);
    strcat(payload, main_id);
    if (data)
    {
        strcat(payload, data);
    }
    strcat(payload, MQTT_ETX);

    // Send via protocol
    protocol_send_mqtt(topic, payload);

    return 0;
}
