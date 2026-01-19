#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Initialize MQTT module (if needed)
    void mqtt_init(void);

    /**
     * @brief Publish a generic MQTT message following the project format.
     *
     * Topic: kyokuto/[MainID]/[PeriID]/[Code]
     * Payload: \x02[PeriID][Code][Seq][Num][MainID][Data]\x03
     *
     * @param main_id Main Gateway ID (6 chars)
     * @param peri_id Peripheral ID (6 chars)
     * @param code    Command Code (2 chars, e.g. "AD", "PA")
     * @param seq     Sequence Number string (2 chars, e.g. "01")
     * @param num     Device Number string (2 chars, e.g. "02")
     * @param data    Additional data content string (optional, can be NULL or empty)
     * @return 0 on success, negative on error
     */
    int mqtt_publish_generic(const char *main_id, const char *peri_id, const char *code,
                             const char *seq, const char *num, const char *data);

#ifdef __cplusplus
}
#endif

#endif // MQTT_H
