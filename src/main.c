#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "led.h"
#include "ble_gatt.h"
#include "spi_flash.h"
#include "fs_handler.h"
#include "ext_comm.h"
#include "atm90e26.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static void read_atm90e26(const struct device *dev, const char *name)
{
        struct sensor_value volt, curr, pwr, pwr_react, pwr_app, pf, freq, energy;

        if (!device_is_ready(dev))
        {
                LOG_ERR("Device %s is not ready", name);
                return;
        }

        if (sensor_sample_fetch(dev) < 0)
        {
                LOG_ERR("Failed to fetch sample from %s", name);
                return;
        }

        sensor_channel_get(dev, SENSOR_CHAN_VOLTAGE, &volt);
        sensor_channel_get(dev, SENSOR_CHAN_CURRENT, &curr);
        sensor_channel_get(dev, SENSOR_CHAN_POWER, &pwr);
        sensor_channel_get(dev, SENSOR_CHAN_ATM90E26_REACTIVE_POWER, &pwr_react);
        sensor_channel_get(dev, SENSOR_CHAN_ATM90E26_APPARENT_POWER, &pwr_app);
        sensor_channel_get(dev, SENSOR_CHAN_ATM90E26_POWER_FACTOR, &pf);
        sensor_channel_get(dev, SENSOR_CHAN_FREQUENCY, &freq);
        sensor_channel_get(dev, SENSOR_CHAN_ATM90E26_ENERGY, &energy);

        LOG_INF("[%s] V:%d.%02dV I:%d.%03dA P:%dW Q:%dVAr S:%dVA PF:%d.%03d Freq:%d.%02dHz E:%dWh",
                name,
                volt.val1, volt.val2 / 10000,
                curr.val1, curr.val2 / 1000,
                pwr.val1,
                pwr_react.val1,
                pwr_app.val1,
                pf.val1, pf.val2 / 1000,
                freq.val1, freq.val2 / 10000,
                energy.val1);
}

int main(void)
{
        const struct device *atm1 = DEVICE_DT_GET(DT_NODELABEL(atm1));
        const struct device *atm2 = DEVICE_DT_GET(DT_NODELABEL(atm2));
        const struct device *atm3 = DEVICE_DT_GET(DT_NODELABEL(atm3));
        int count = 0;

        led_init();
        ext_comm_init();
        spi_flash_init();
        fs_handler_init();
        fs_handler_test(); // Test FS on boot
        ble_gatt_init();

        while (1)
        {
                led_toggle();

                if (count % 5 == 0)
                {
                        read_atm90e26(atm1, "ATM1");
                        read_atm90e26(atm2, "ATM2");
                        read_atm90e26(atm3, "ATM3");
                }
                count++;

                k_sleep(K_SECONDS(1));
        }
        return 0;
}
