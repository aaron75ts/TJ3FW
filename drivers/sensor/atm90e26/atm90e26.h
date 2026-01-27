#ifndef ZEPHYR_DRIVERS_SENSOR_ATM90E26_ATM90E26_H_
#define ZEPHYR_DRIVERS_SENSOR_ATM90E26_ATM90E26_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

    /* Custom sensor channels for ATM90E26 */
    enum atm90e26_channel
    {
        SENSOR_CHAN_ATM90E26_REACTIVE_POWER = SENSOR_CHAN_PRIV_START,
        SENSOR_CHAN_ATM90E26_APPARENT_POWER,
        SENSOR_CHAN_ATM90E26_POWER_FACTOR,
        SENSOR_CHAN_ATM90E26_PHASE_ANGLE,
        SENSOR_CHAN_ATM90E26_ENERGY,
    };

/* Status and Special Registers */
#define ATM90E26_REG_SOFTRESET 0x00
#define ATM90E26_REG_SYSSTATUS 0x01
#define ATM90E26_REG_FUNCEN 0x02
#define ATM90E26_REG_SAGTH 0x03
#define ATM90E26_REG_SMALLPMOD 0x04
#define ATM90E26_REG_LASTDATA 0x06

/* Metering Calibration & Configuration */
#define ATM90E26_REG_LSB 0x08
#define ATM90E26_REG_CALSTART 0x20
#define ATM90E26_REG_PLCONSTH 0x21
#define ATM90E26_REG_PLCONSTL 0x22
#define ATM90E26_REG_LGAIN 0x23
#define ATM90E26_REG_LPHI 0x24
#define ATM90E26_REG_NGAIN 0x25
#define ATM90E26_REG_NPHI 0x26
#define ATM90E26_REG_PSTARTTH 0x27
#define ATM90E26_REG_PNOLTH 0x28
#define ATM90E26_REG_QSTARTTH 0x29
#define ATM90E26_REG_QNOLTH 0x2A
#define ATM90E26_REG_MMODE 0x2B
#define ATM90E26_REG_CS1 0x2C

/* Measurement Calibration */
#define ATM90E26_REG_ADJSTART 0x30
#define ATM90E26_REG_UGAIN 0x31
#define ATM90E26_REG_IGAINL 0x32
#define ATM90E26_REG_IGAINN 0x33
#define ATM90E26_REG_UOFFSET 0x34
#define ATM90E26_REG_IOFFSETL 0x35
#define ATM90E26_REG_IOFFSETN 0x36
#define ATM90E26_REG_POFFSETL 0x37
#define ATM90E26_REG_QOFFSETL 0x38
#define ATM90E26_REG_POFFSETN 0x39
#define ATM90E26_REG_QOFFSETN 0x3A
#define ATM90E26_REG_CS2 0x3B

/* Energy & Measurement Registers */
#define ATM90E26_REG_APENERGY 0x40
#define ATM90E26_REG_ANENERGY 0x41
#define ATM90E26_REG_ATENERGY 0x42
#define ATM90E26_REG_RPENERGY 0x43
#define ATM90E26_REG_RNENERGY 0x44
#define ATM90E26_REG_RTENERGY 0x45
#define ATM90E26_REG_ENSTATUS 0x46
#define ATM90E26_REG_IRMS 0x48
#define ATM90E26_REG_URMS 0x49
#define ATM90E26_REG_PMEAN 0x4A
#define ATM90E26_REG_QMEAN 0x4B
#define ATM90E26_REG_FREQ 0x4C
#define ATM90E26_REG_POWERF 0x4D
#define ATM90E26_REG_PANGLE 0x4E
#define ATM90E26_REG_SMEAN 0x4F
#define ATM90E26_REG_IRMS2 0x68
#define ATM90E26_REG_PMEAN2 0x6A
#define ATM90E26_REG_QMEAN2 0x6B
#define ATM90E26_REG_POWERF2 0x6C
#define ATM90E26_REG_PANGLE2 0x6D
#define ATM90E26_REG_SMEAN2 0x6E

/* Magic values */
#define ATM90E26_SOFTRESET_MAGIC 0x789A
#define ATM90E26_CALSTART_MAGIC 0x5678
#define ATM90E26_CALCHECK_MAGIC 0x8765
#define ATM90E26_ADJSTART_MAGIC 0x5678
#define ATM90E26_ADJCHECK_MAGIC 0x8765

/* Default Calibration Values (from docs) */
#define PLCONSTH_DEFAULT 0x0015
#define PLCONSTL_DEFAULT 0xD174
#define MMODE_DEFAULT 0x7C22
#define PSTARTTH_DEFAULT 0x08BD

    struct atm90e26_config
    {
        const struct device *uart_dev;
    };

    struct atm90e26_data
    {
        uint16_t voltage;         /* Urms (49H) */
        uint16_t current;         /* Irms (48H) - L line */
        uint16_t current2;        /* Irms2 (68H) - N line */
        uint16_t power;           /* Pmean (4AH) - Active Power */
        uint16_t power2;          /* Pmean2 (6AH) - Active Power N line */
        uint16_t reactive_power;  /* Qmean (4BH) - Reactive Power */
        uint16_t reactive_power2; /* Qmean2 (6BH) - Reactive Power N line */
        uint16_t apparent_power;  /* Smean (4FH) - Apparent Power */
        uint16_t apparent_power2; /* Smean2 (6EH) - Apparent Power N line */
        uint16_t power_factor;    /* PowerF (4DH) */
        uint16_t power_factor2;   /* PowerF2 (6CH) */
        uint16_t freq;            /* Freq (4CH) */
        uint16_t phase_angle;     /* Pangle (4EH) */
        uint16_t phase_angle2;    /* Pangle2 (6DH) */
        uint16_t energy_active_p; /* APenergy (40H) - Active Positive Energy */
        uint16_t energy_active_n; /* ANenergy (41H) - Active Negative Energy */
    };

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SENSOR_ATM90E26_ATM90E26_H_ */
