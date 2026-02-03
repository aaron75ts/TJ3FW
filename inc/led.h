#ifndef LED_H
#define LED_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    /* LED 模式定義 */
    typedef enum
    {
        LED_PATTERN_NORMAL = 0,     /* 正常模式：1Hz 閃爍 */
        LED_PATTERN_FAST_BLINK = 1, /* 快速閃爍：5Hz (設定模式) */
        LED_PATTERN_SLOW_BLINK = 2, /* 慢速閃爍：0.5Hz */
        LED_PATTERN_ON = 3,         /* 常亮 */
        LED_PATTERN_OFF = 4         /* 常滅 */
    } led_pattern_t;

    void led_init(void);
    void led_on(void);
    void led_off(void);
    void led_toggle(void);
    void led_get_state(bool *is_on);

    /* 設定 LED 模式 */
    void led_set_pattern(led_pattern_t pattern);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */
