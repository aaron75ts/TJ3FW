#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/sensor.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

#include <drivers/sensor/m90e26.h>

#include <tj3_comm/ble.h>
#include <tj3_comm/ior.h>
#include <tj3_comm/nrf9151_link.h>

#include <zephyr/sys/printk.h>

#include <math.h>

#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* Minimal MQTT identity/config (spec placeholders)                            */
/* -------------------------------------------------------------------------- */

/* 6 bytes each per docs/MQTT.md examples. */
#define TJ3_MAIN_ID "GW0001"
#define TJ3_PERI_ID "PE0002"
/* 2 ASCII digits ("00".."99"). Example uses 02 for PE0002. */
#define TJ3_TERM_NUM "02"

/* FA publish interval (seconds). Spec says 1~60 minutes; we default to 60s for bring-up. */
#define TJ3_FA_PERIOD_S 60

static uint8_t g_mqtt_seq;
static char g_time_yymmddhhmm[11] = "0000000000";

static float g_last_currents_a[6];
static float g_last_angles_deg[6];
static bool g_last_snapshot_ok;

#define TJ3_IO_NODE DT_NODELABEL(tj3_io)
BUILD_ASSERT(DT_NODE_EXISTS(TJ3_IO_NODE), "tj3_io node missing in devicetree");
BUILD_ASSERT(DT_NODE_HAS_PROP(TJ3_IO_NODE, di1_1_gpios), "tj3_io missing di1_1_gpios");
BUILD_ASSERT(DT_NODE_HAS_PROP(TJ3_IO_NODE, di2_1_gpios), "tj3_io missing di2_1_gpios");

static const struct gpio_dt_spec g_led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec g_di1 = GPIO_DT_SPEC_GET(TJ3_IO_NODE, di1_1_gpios);
static const struct gpio_dt_spec g_di2 = GPIO_DT_SPEC_GET(TJ3_IO_NODE, di2_1_gpios);
static char g_di1_ascii = '0';
static char g_di2_ascii = '0';

static void tj3_di_update(void)
{
        if (!device_is_ready(g_di1.port) || !device_is_ready(g_di2.port))
        {
                return;
        }

        int v1 = gpio_pin_get_dt(&g_di1);
        int v2 = gpio_pin_get_dt(&g_di2);
        if (v1 >= 0)
        {
                g_di1_ascii = (v1 != 0) ? '1' : '0';
        }
        if (v2 >= 0)
        {
                g_di2_ascii = (v2 != 0) ? '1' : '0';
        }
}

static uint8_t next_mqtt_seq_ascii(char out[2])
{
        uint8_t seq = g_mqtt_seq;
        g_mqtt_seq = (uint8_t)((g_mqtt_seq + 1u) % 100u);
        out[0] = (char)('0' + (seq / 10u));
        out[1] = (char)('0' + (seq % 10u));
        return seq;
}

static void append_fixed_ascii(uint8_t *buf, size_t buf_sz, size_t *pos, const char *s, size_t width)
{
        if (buf == NULL || pos == NULL || buf_sz == 0u || width == 0u)
        {
                return;
        }

        for (size_t i = 0; i < width; i++)
        {
                char c = ' ';
                if (s != NULL && s[i] != '\0')
                {
                        c = s[i];
                }
                if (*pos < buf_sz)
                {
                        buf[*pos] = (uint8_t)c;
                        (*pos)++;
                }
        }
}

/* Format a fixed-width numeric field as "dddd.ddd" (8 bytes). */
static void format_4_3(char out[8], float value)
{
        if (value < 0.0f)
        {
                value = 0.0f;
        }

        int32_t scaled = (int32_t)lrintf(value * 1000.0f);
        if (scaled < 0)
        {
                scaled = 0;
        }
        int32_t ip = scaled / 1000;
        int32_t fp = scaled % 1000;
        if (ip > 9999)
        {
                ip = 9999;
                fp = 999;
        }

        char tmp[9];
        (void)snprintk(tmp, sizeof(tmp), "%04ld.%03ld", (long)ip, (long)fp);
        memcpy(out, tmp, 8);
}

/* Format a fixed-width mA field as 4 ASCII digits (0000..9999). */
static void format_ma4(char out[4], float value_ma)
{
        if (value_ma < 0.0f)
        {
                value_ma = 0.0f;
        }

        int32_t ma = (int32_t)lrintf(value_ma);
        if (ma < 0)
        {
                ma = 0;
        }
        if (ma > 9999)
        {
                ma = 9999;
        }

        char tmp[5];
        (void)snprintk(tmp, sizeof(tmp), "%04ld", (long)ma);
        memcpy(out, tmp, 4);
}

/* Format phase angle in 0.1deg as signed 5 ASCII bytes, e.g. "+0030", "-1800". */
static void format_phase_tenths(char out[5], float angle_deg)
{
        int32_t tenths = (int32_t)lrintf(angle_deg * 10.0f);
        if (tenths > 1800)
        {
                tenths = 1800;
        }
        else if (tenths < -1800)
        {
                tenths = -1800;
        }

        char tmp[6];
        (void)snprintk(tmp, sizeof(tmp), "%+05ld", (long)tenths);
        memcpy(out, tmp, 5);
}

static void format_mmddhhmm(char out[8])
{
        /* Prefer D2-provided time: YYMMDDhhmm -> MMDDhhmm (8 bytes). */
        if (g_time_yymmddhhmm[0] != '0' || g_time_yymmddhhmm[1] != '0')
        {
                memcpy(out, &g_time_yymmddhhmm[2], 8);
                return;
        }

        /* Fallback: derive HHMM from uptime and keep MMDD fixed to 0101. */
        int64_t uptime_ms = k_uptime_get();
        if (uptime_ms < 0)
        {
                uptime_ms = 0;
        }

        uint32_t minutes = (uint32_t)(uptime_ms / 60000);
        uint32_t hh = (minutes / 60u) % 24u;
        uint32_t mm = minutes % 60u;

        char tmp[9];
        (void)snprintk(tmp, sizeof(tmp), "0101%02u%02u", hh, mm);
        memcpy(out, tmp, 8);
}

static int mqtt_publish_ascii(const char *id2, const uint8_t *body, uint16_t body_len)
{
        char topic[64];
        int n = snprintk(topic, sizeof(topic), "kyokuto/%s/%s/%s", TJ3_MAIN_ID, TJ3_PERI_ID, id2);
        if (n <= 0 || (size_t)n >= sizeof(topic))
        {
                return -EINVAL;
        }

        return tj3_comm_nrf9151_send_mqtt_publish(topic, body, body_len);
}

static int build_payload_fa(const float currents_a[6], uint8_t *out, uint16_t out_sz, uint16_t *out_len)
{
        if (currents_a == NULL || out == NULL || out_len == NULL)
        {
                return -EINVAL;
        }

        size_t pos = 0;
        out[pos++] = 0x02;

        append_fixed_ascii(out, out_sz, &pos, TJ3_PERI_ID, 6);
        append_fixed_ascii(out, out_sz, &pos, "FA", 2);

        char seq2[2];
        (void)next_mqtt_seq_ascii(seq2);
        append_fixed_ascii(out, out_sz, &pos, seq2, 2);
        append_fixed_ascii(out, out_sz, &pos, TJ3_TERM_NUM, 2);
        append_fixed_ascii(out, out_sz, &pos, TJ3_MAIN_ID, 6);

        char mmddhhmm[8];
        format_mmddhhmm(mmddhhmm);
        append_fixed_ascii(out, out_sz, &pos, mmddhhmm, 8);

        for (int i = 0; i < 6; i++)
        {
                char f[8];
                format_4_3(f, currents_a[i]);
                append_fixed_ascii(out, out_sz, &pos, f, 8);
        }

        /* Ig value: not wired in current bring-up. Send 0000 (mA). */
        append_fixed_ascii(out, out_sz, &pos, "0000", 4);

        if (pos >= out_sz)
        {
                return -ENOMEM;
        }
        out[pos++] = 0x03;

        *out_len = (uint16_t)pos;
        return 0;
}

static int build_payload_pw_response(const char seq2[2], const char num2[2], const float currents_a[6],
                                     const float angles_deg[6], uint8_t *out, uint16_t out_sz, uint16_t *out_len)
{
        if (seq2 == NULL || num2 == NULL || currents_a == NULL || angles_deg == NULL || out == NULL || out_len == NULL)
        {
                return -EINVAL;
        }

        size_t pos = 0;
        out[pos++] = 0x02;

        append_fixed_ascii(out, out_sz, &pos, TJ3_PERI_ID, 6);
        append_fixed_ascii(out, out_sz, &pos, "PW", 2);

        /* Per docs/MQTT_PW.md response format: "99" + [Seq][Num] + [MainID] */
        append_fixed_ascii(out, out_sz, &pos, "99", 2);
        append_fixed_ascii(out, out_sz, &pos, seq2, 2);
        append_fixed_ascii(out, out_sz, &pos, num2, 2);
        append_fixed_ascii(out, out_sz, &pos, TJ3_MAIN_ID, 6);

        for (int ch = 0; ch < 6; ch++)
        {
                float io_ma = currents_a[ch] * 1000.0f;
                float ior_ma = tj3_comm_ior_calc_ma_angle(io_ma, angles_deg[ch]);

                char io_f[4];
                char ior_f[4];
                char ph[5];
                format_ma4(io_f, io_ma);
                format_phase_tenths(ph, angles_deg[ch]);
                format_ma4(ior_f, ior_ma);

                append_fixed_ascii(out, out_sz, &pos, io_f, 4);
                append_fixed_ascii(out, out_sz, &pos, ph, 5);
                append_fixed_ascii(out, out_sz, &pos, ior_f, 4);
        }

        if (pos >= out_sz)
        {
                return -ENOMEM;
        }
        out[pos++] = 0x03;
        *out_len = (uint16_t)pos;
        return 0;
}

static int build_payload_ph_response(const char seq2[2], const char num2[2], const float currents_a[6],
                                     char di1, char di2, uint8_t *out, uint16_t out_sz, uint16_t *out_len)
{
        /* Per docs/MQTT_PH.md response: 6 currents (mA, 4 ASCII each) + DI1 + DI2. */
        if (seq2 == NULL || num2 == NULL || currents_a == NULL || out == NULL || out_len == NULL)
        {
                return -EINVAL;
        }

        size_t pos = 0;
        out[pos++] = 0x02;

        append_fixed_ascii(out, out_sz, &pos, TJ3_PERI_ID, 6);
        append_fixed_ascii(out, out_sz, &pos, "PH", 2);

        append_fixed_ascii(out, out_sz, &pos, seq2, 2);
        append_fixed_ascii(out, out_sz, &pos, num2, 2);
        append_fixed_ascii(out, out_sz, &pos, TJ3_MAIN_ID, 6);

        for (int ch = 0; ch < 6; ch++)
        {
                float io_ma = currents_a[ch] * 1000.0f;
                char io_f[4];
                format_ma4(io_f, io_ma);
                append_fixed_ascii(out, out_sz, &pos, io_f, 4);
        }

        if (pos + 2u > out_sz)
        {
                return -ENOMEM;
        }

        out[pos++] = (uint8_t)di1;
        out[pos++] = (uint8_t)di2;

        if (pos >= out_sz)
        {
                return -ENOMEM;
        }
        out[pos++] = 0x03;
        *out_len = (uint16_t)pos;
        return 0;
}

struct mqtt_cmd_req
{
        char id2[2];
        char seq2[2];
        char num2[2];
};

static bool parse_mqtt_cmd_req(const uint8_t *body, uint16_t body_len, struct mqtt_cmd_req *out)
{
        /* Request payload: \x02[PeriID(6)] [ID(2)] [Seq(2)] [Num(2)] [MainID(6)]\x03 */
        if (body == NULL || out == NULL)
        {
                return false;
        }

        if (body_len < (uint16_t)(1u + 6u + 2u + 2u + 2u + 6u + 1u))
        {
                return false;
        }
        if (body[0] != 0x02 || body[body_len - 1u] != 0x03)
        {
                return false;
        }

        const uint8_t *p = &body[1];
        if (memcmp(p, TJ3_PERI_ID, 6) != 0)
        {
                return false;
        }
        p += 6;

        memcpy(out->id2, p, 2);
        p += 2;
        memcpy(out->seq2, p, 2);
        p += 2;
        memcpy(out->num2, p, 2);
        p += 2;

        if (memcmp(p, TJ3_MAIN_ID, 6) != 0)
        {
                return false;
        }
        return true;
}

static void on_incoming_mqtt_from_9151(const uint8_t *payload, uint16_t payload_len)
{
        /* Payload per docs/PROTOCOL.md: [TopicLen(1)][Topic][Body...] */
        if (payload == NULL || payload_len < 1u)
        {
                return;
        }

        uint8_t topic_len = payload[0];
        if ((uint16_t)(1u + topic_len) > payload_len)
        {
                return;
        }

        const char *topic = (const char *)&payload[1];
        const uint8_t *body = &payload[1u + topic_len];
        uint16_t body_len = (uint16_t)(payload_len - (1u + topic_len));

        /* Expect polling requests on: kyosvr/<MainID>/<PeriID>/cmd */
        char expected[64];
        int n = snprintk(expected, sizeof(expected), "kyosvr/%s/%s/cmd", TJ3_MAIN_ID, TJ3_PERI_ID);
        if (n <= 0 || (size_t)n >= sizeof(expected))
        {
                return;
        }
        if (strlen(expected) != topic_len || memcmp(topic, expected, topic_len) != 0)
        {
                return;
        }

        struct mqtt_cmd_req req;
        if (!parse_mqtt_cmd_req(body, body_len, &req))
        {
                return;
        }

        if (!g_last_snapshot_ok)
        {
                LOG_WRN("Ignore MQTT %c%c: no snapshot", req.id2[0], req.id2[1]);
                return;
        }

        uint8_t resp[256];
        uint16_t resp_len = 0;

        if (req.id2[0] == 'P' && req.id2[1] == 'H')
        {
                int err = build_payload_ph_response(req.seq2, req.num2, g_last_currents_a, g_di1_ascii, g_di2_ascii, resp, sizeof(resp),
                                                    &resp_len);
                if (err == 0)
                {
                        (void)mqtt_publish_ascii("PH", resp, resp_len);
                }
        }
        else if (req.id2[0] == 'P' && req.id2[1] == 'W')
        {
                int err = build_payload_pw_response(req.seq2, req.num2, g_last_currents_a, g_last_angles_deg, resp, sizeof(resp),
                                                    &resp_len);
                if (err == 0)
                {
                        (void)mqtt_publish_ascii("PW", resp, resp_len);
                }
        }
}

static const char *settings_json(void)
{
        /* Minimal boot-up settings payload for 0xC2 response.
         * Keep JSON small and stable while bring-up; expand later.
         */
        return "{"
               "\"main_id\":\"" TJ3_MAIN_ID "\","
               "\"peri_id\":\"" TJ3_PERI_ID "\","
               "\"term_num\":\"" TJ3_TERM_NUM "\","
               "\"fa_period_s\":" STRINGIFY(TJ3_FA_PERIOD_S) ""
                                                             "}";
}

#define ATM1_NODE DT_ALIAS(atm90e26_1)
#define ATM2_NODE DT_ALIAS(atm90e26_2)
#define ATM3_NODE DT_ALIAS(atm90e26_3)

BUILD_ASSERT(DT_NODE_HAS_STATUS(ATM1_NODE, okay), "atm90e26-1 alias not okay");
BUILD_ASSERT(DT_NODE_HAS_STATUS(ATM2_NODE, okay), "atm90e26-2 alias not okay");
BUILD_ASSERT(DT_NODE_HAS_STATUS(ATM3_NODE, okay), "atm90e26-3 alias not okay");

static void on_ble_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len, void *user_data)
{
        ARG_UNUSED(user_data);

        /* Pass-through: forward frames from APP to nRF9151. */
        (void)tj3_comm_nrf9151_send_frame(op, payload, payload_len);
}

static void on_nrf9151_frame(uint8_t op, const uint8_t *payload, uint16_t payload_len, void *user_data)
{
        ARG_UNUSED(user_data);

        /* Management: settings sync request */
        if (op == TJ3_COMM_OP_REQ_SETTINGS)
        {
                (void)tj3_comm_nrf9151_send_settings_json(settings_json());
                return;
        }

        if (op == TJ3_COMM_OP_RTC_SYNC)
        {
                /* Time sync format (ASCII): YYMMDDhhmm (10 bytes). */
                if (payload_len >= 10u)
                {
                        memcpy(g_time_yymmddhhmm, payload, 10u);
                        g_time_yymmddhhmm[10] = '\0';
                        LOG_INF("RTC sync: %s", g_time_yymmddhhmm);
                }
                return;
        }

        /* Handle incoming MQTT cmd forwarded from nRF9151. */
        if (op == TJ3_COMM_OP_MQTT_PUB)
        {
                on_incoming_mqtt_from_9151(payload, payload_len);
                return;
        }

        /* Forward nRF9151 frames to APP via BLE notify. */
        (void)tj3_comm_ble_notify_frame(op, payload, payload_len);
}

static int read_meter(const struct device *atm,
                      float *v_v,
                      float *i1_a,
                      float *i2_a,
                      float *angle1_deg,
                      float *angle2_deg,
                      float *p_w,
                      float *pf)
{
        struct sensor_value v;
        struct sensor_value i1;
        struct sensor_value i2;
        struct sensor_value a1;
        struct sensor_value a2;
        struct sensor_value p;
        struct sensor_value pf_val;

        int rc = sensor_sample_fetch(atm);
        if (rc != 0)
        {
                return rc;
        }

        rc = sensor_channel_get(atm, SENSOR_CHAN_VOLTAGE, &v);
        if (rc != 0)
        {
                return rc;
        }
        rc = sensor_channel_get(atm, SENSOR_CHAN_CURRENT, &i1);
        if (rc != 0)
        {
                return rc;
        }

        rc = sensor_channel_get(atm, SENSOR_CHAN_M90E26_CURRENT2, &i2);
        if (rc != 0)
        {
                return rc;
        }

        rc = sensor_channel_get(atm, SENSOR_CHAN_M90E26_PHASE_ANGLE, &a1);
        if (rc != 0)
        {
                return rc;
        }

        rc = sensor_channel_get(atm, SENSOR_CHAN_M90E26_PHASE_ANGLE2, &a2);
        if (rc != 0)
        {
                return rc;
        }
        rc = sensor_channel_get(atm, SENSOR_CHAN_M90E26_ACTIVE_POWER, &p);
        if (rc != 0)
        {
                return rc;
        }
        rc = sensor_channel_get(atm, SENSOR_CHAN_M90E26_POWER_FACTOR, &pf_val);
        if (rc != 0)
        {
                return rc;
        }

        *v_v = sensor_value_to_float(&v);
        *i1_a = sensor_value_to_float(&i1);
        *i2_a = sensor_value_to_float(&i2);
        *angle1_deg = sensor_value_to_float(&a1);
        *angle2_deg = sensor_value_to_float(&a2);
        *p_w = sensor_value_to_float(&p);
        *pf = sensor_value_to_float(&pf_val);
        return 0;
}

int main(void)
{
        k_msleep(1000);
        LOG_INF("\nHello World! %s", CONFIG_BOARD_TARGET);

        int rc = tj3_comm_nrf9151_init(on_nrf9151_frame, NULL);
        if (rc != 0)
        {
                LOG_WRN("nRF9151 link init failed (%d)", rc);
        }
        else
        {
                (void)tj3_comm_nrf9151_send_log("[MCU] boot\r\n");
                (void)tj3_comm_nrf9151_send_settings_json(settings_json());
        }

        rc = tj3_comm_ble_init(on_ble_frame, NULL);
        if (rc != 0)
        {
                LOG_WRN("BLE init failed (%d)", rc);
        }

        const struct device *atm1 = DEVICE_DT_GET(ATM1_NODE);
        const struct device *atm2 = DEVICE_DT_GET(ATM2_NODE);
        const struct device *atm3 = DEVICE_DT_GET(ATM3_NODE);

        if (device_is_ready(g_di1.port))
        {
                (void)gpio_pin_configure_dt(&g_di1, GPIO_INPUT);
        }
        if (device_is_ready(g_di2.port))
        {
                (void)gpio_pin_configure_dt(&g_di2, GPIO_INPUT);
        }

        if (device_is_ready(g_led0.port))
        {
                (void)gpio_pin_configure_dt(&g_led0, GPIO_OUTPUT_ACTIVE);
                LOG_INF("LED0 initialized");
        }
        else
        {
                LOG_WRN("LED0 not ready");
        }

        while (1)
        {
                k_sleep(K_SECONDS(1));

                if (device_is_ready(g_led0.port))
                {
                        (void)gpio_pin_toggle_dt(&g_led0);
                }

                // tj3_di_update();

                // const struct device *atms[3] = {atm1, atm2, atm3};

                // float currents_a[6] = {0};
                // float angles_deg[6] = {0};
                // bool all_ok = true;

                // for (int idx = 0; idx < 3; idx++)
                // {
                //         float v_v = 0.0f;
                //         float i1_a = 0.0f;
                //         float i2_a = 0.0f;
                //         float angle1_deg = 0.0f;
                //         float angle2_deg = 0.0f;
                //         float p_w = 0.0f;
                //         float pf = 0.0f;

                //         if (!device_is_ready(atms[idx]))
                //         {
                //                 LOG_WRN("ATM%d not ready (skipping)", idx + 1);
                //                 all_ok = false;
                //                 continue;
                //         }

                //         rc = read_meter(atms[idx], &v_v, &i1_a, &i2_a, &angle1_deg, &angle2_deg, &p_w, &pf);
                //         if (rc != 0)
                //         {
                //                 LOG_WRN("Sensor%d read failed (%d)", idx + 1, rc);
                //                 all_ok = false;
                //                 continue;
                //         }

                //         currents_a[idx * 2 + 0] = i1_a;
                //         currents_a[idx * 2 + 1] = i2_a;
                //         angles_deg[idx * 2 + 0] = angle1_deg;
                //         angles_deg[idx * 2 + 1] = angle2_deg;

                //         float io_ma = i1_a * 1000.0f;
                //         float ior_ma = tj3_comm_ior_calc_ma(io_ma, pf);

                //         LOG_INF("ATM%d | V: %.6f V, I1: %.6f A, I2: %.6f A, A1: %.1f deg, A2: %.1f deg, P: %.6f W, PF: %.6f, IOR: %.3f mA",
                //                 idx + 1,
                //                 (double)v_v,
                //                 (double)i1_a,
                //                 (double)i2_a,
                //                 (double)angle1_deg,
                //                 (double)angle2_deg,
                //                 (double)p_w,
                //                 (double)pf,
                //                 (double)ior_ma);

                //         char line[160];
                //         int n = snprintk(line, sizeof(line),
                //                          "[MCU] ATM%d | V: %.6f V, I1: %.6f A, I2: %.6f A, A1: %.1f deg, A2: %.1f deg, P: %.6f W, PF: %.6f\r\n",
                //                          idx + 1,
                //                          (double)v_v,
                //                          (double)i1_a,
                //                          (double)i2_a,
                //                          (double)angle1_deg,
                //                          (double)angle2_deg,
                //                          (double)p_w,
                //                          (double)pf);
                //         if (n > 0)
                //         {
                //                 (void)tj3_comm_nrf9151_send_log(line);
                //         }
                // }

                // static int64_t last_fa_ms;
                // int64_t now_ms = k_uptime_get();

                // memcpy(g_last_currents_a, currents_a, sizeof(currents_a));
                // memcpy(g_last_angles_deg, angles_deg, sizeof(angles_deg));
                // g_last_snapshot_ok = all_ok;

                // if (all_ok && (now_ms - last_fa_ms) >= (int64_t)TJ3_FA_PERIOD_S * 1000)
                // {
                //         last_fa_ms = now_ms;

                //         uint8_t body[256];
                //         uint16_t body_len = 0;

                //         int err = build_payload_fa(currents_a, body, sizeof(body), &body_len);
                //         if (err == 0)
                //         {
                //                 err = mqtt_publish_ascii("FA", body, body_len);
                //                 if (err != 0)
                //                 {
                //                         LOG_WRN("MQTT FA publish failed (%d)", err);
                //                 }
                //         }
                //         else
                //         {
                //                 LOG_WRN("Build FA payload failed (%d)", err);
                //         }
                // }
        }
}
