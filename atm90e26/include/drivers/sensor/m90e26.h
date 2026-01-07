/*
 * Copyright (c) 2023, Your Name
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_M90E26_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_M90E26_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* M90E26 custom channels */
    enum m90e26_channel
    {
        /* Active Power (L-Line) */
        SENSOR_CHAN_M90E26_ACTIVE_POWER = SENSOR_CHAN_PRIV_START,
        /* Reactive Power (L-Line) */
        SENSOR_CHAN_M90E26_REACTIVE_POWER,
        /* Apparent Power (L-Line) */
        SENSOR_CHAN_M90E26_APPARENT_POWER,
        /* Power Factor (L-Line) */
        SENSOR_CHAN_M90E26_POWER_FACTOR,

        /* Current RMS (N-Line / 2nd channel) */
        SENSOR_CHAN_M90E26_CURRENT2,

        /* Voltage/Current phase angle (L-Line). Unit: degrees. */
        SENSOR_CHAN_M90E26_PHASE_ANGLE,

        /* Voltage/Current phase angle (N-Line / 2nd channel). Unit: degrees. */
        SENSOR_CHAN_M90E26_PHASE_ANGLE2,
    };

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_M90E26_H_ */
