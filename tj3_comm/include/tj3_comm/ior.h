#ifndef TJ3_COMM_IOR_H_
#define TJ3_COMM_IOR_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum tj3_comm_ior_phase_mode
    {
        /* Default/unset value (matches docs/TJ3_IOR.txt MODE_AUTO). */
        TJ3_COMM_IOR_MODE_AUTO = 0xFF,
        TJ3_COMM_IOR_MODE_1_PHASE = 1,
        TJ3_COMM_IOR_MODE_3_PHASE = 3,
    };

    /* MODE_AUTO is the default/unset value. When MODE_AUTO is active,
     * calculations follow the "safe default" approach (treat as 3-phase)
     * unless higher-level logic overrides it.
     *
     * Thread-safety: intended to be set during init or rarely.
     */
    void tj3_comm_ior_set_phase_mode(enum tj3_comm_ior_phase_mode mode);

    enum tj3_comm_ior_phase_mode tj3_comm_ior_get_phase_mode(void);

    /*
     * IOR calculation aligned with docs/TJ3_IOR.txt:
     *  - Ior = Io * cos(theta + alpha)
     *  - when only PF is available, PF == cos(theta); alpha assumed 0
     *  - 3-phase correction: divide by cos(30deg) = 0.866025
     *
     * Inputs:
     *  - io_ma: leakage current in mA
     *  - pf: power factor in [-1.0, 1.0]
     *
     * Output:
     *  - ior current in mA
     */
    float tj3_comm_ior_calc_ma(float io_ma, float pf);

    /* Optional helper when phase angle is available (degrees). */
    float tj3_comm_ior_calc_ma_angle(float io_ma, float angle_deg);

#ifdef __cplusplus
}
#endif

#endif /* TJ3_COMM_IOR_H_ */
