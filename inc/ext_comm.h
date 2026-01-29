#ifndef EXT_COMM_H
#define EXT_COMM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    /**
     * @brief Initialize External Communication (Protocol over UART30).
     * @return 0 on success.
     */
    int ext_comm_init(void);

    /**
     * @brief Send formatted log message via UART30 protocol
     * @param log_msg 格式化的 LOG 訊息字串
     * @return 0 on success, negative on error
     */
    int ext_comm_send_log(const char *log_msg);

#ifdef __cplusplus
}
#endif

#endif
