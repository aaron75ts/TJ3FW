#include <zephyr/kernel.h>
#include "atm90e26.h"
#include "led.h"
#include "protocol.h"
#include "mqtt.h"

int main(void)
{
        led_init();
        protocol_init();
        mqtt_init();

        protocol_send_log("[MAIN] System Started");

        while (1)
        {
                led_toggle();
                // protocol_send_log("[MAIN] LED Toggled");

                // Example MQTT publish
                // mqtt_publish_generic("GW0001", "PE0002", "AD", "01", "02", "11");

                k_msleep(1000);
        }

        return 0;
}
