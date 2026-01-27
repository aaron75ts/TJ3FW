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

#ifdef __cplusplus
}
#endif

#endif
