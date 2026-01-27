#ifndef LED_H
#define LED_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    void led_init(void);
    void led_on(void);
    void led_off(void);
    void led_toggle(void);
    void led_get_state(bool *is_on);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */
