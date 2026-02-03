#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include "led.h"
#include "ble_gatt.h"
#include "spi_flash.h"
#include "fs_handler.h"
#include "ext_comm.h"
#include "atm90e26.h"
#include "di.h"
#include "button.h" /* 加入按鈕模組 */
#include "settings.h"
#include "measure.h"
#include "pulse.h"
#include "poff_detect.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#if 0 // 保留舊的單獨讀取方式作為參考
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
#endif

int main(void)
{
        int count = 0;
        int ret;

        led_init();
        ext_comm_init();

        /* 初始化 SPI Flash，並等待就緒 */
        ret = spi_flash_init();
        if (ret < 0)
        {
                LOG_ERR("SPI Flash init failed, continuing without flash storage");
        }
        k_sleep(K_MSEC(100)); /* 給 flash 時間穩定 */

        /* 初始化檔案系統 */
        ret = fs_handler_init();
        if (ret < 0)
        {
                LOG_ERR("Filesystem init failed: %d", ret);
        }

        /* 測試檔案系統（可選） */
        if (ret == 0)
        {
                fs_handler_test();
        }

        ble_gatt_init();
        di_init();

        /* 初始化設定按鈕 (安全機制) */
        ret = button_init();
        if (ret < 0)
        {
                LOG_ERR("Button init failed: %d", ret);
        }

        /* 初始化設定值、測電模組、脈衝監測模組、停電檢測 */
        settings_init();
        measure_init();
        pulse_init();
        poff_detect_init();

        while (1)
        {
                led_toggle();

                if (count % 5 == 0)
                {
                        /* 新的測電計算週期 */
                        measure_perform_cycle();

                        /* 發送 LOG 輸出 (假設為單相，使用脈衝計數) */
                        uint32_t pulse_count = pulse_get_count();
                        measure_send_log_output(PHASE_SINGLE, pulse_count);

                        /* 記錄電量資訊 */
                        LOG_INF("Pulse: count=%u, kWh=%.4f, Power=%.3f kW",
                                pulse_count,
                                (double)pulse_get_kwh(),
                                (double)pulse_get_current_power());
                }
                count++;

                k_sleep(K_SECONDS(1));
        }
        return 0;
}
