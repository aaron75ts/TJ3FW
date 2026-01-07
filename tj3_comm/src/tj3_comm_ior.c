#include <tj3_comm/ior.h>

#include <math.h>

/* cos(30deg) */
#define TJ3_COMM_COS_30 0.866025f
#define TJ3_COMM_PI 3.14159265358979323846f

static enum tj3_comm_ior_phase_mode g_phase_mode = TJ3_COMM_IOR_MODE_AUTO;

void tj3_comm_ior_set_phase_mode(enum tj3_comm_ior_phase_mode mode)
{
    if (mode != TJ3_COMM_IOR_MODE_AUTO &&
        mode != TJ3_COMM_IOR_MODE_1_PHASE &&
        mode != TJ3_COMM_IOR_MODE_3_PHASE)
    {
        return;
    }

    g_phase_mode = mode;
}

enum tj3_comm_ior_phase_mode tj3_comm_ior_get_phase_mode(void)
{
    return g_phase_mode;
}

static float apply_phase_correction(float ior_ma)
{
    /* MODE_AUTO follows safe default behavior: treat as 3-phase. */
    if (g_phase_mode == TJ3_COMM_IOR_MODE_3_PHASE || g_phase_mode == TJ3_COMM_IOR_MODE_AUTO)
    {
        return ior_ma / TJ3_COMM_COS_30;
    }

    return ior_ma;
}

float tj3_comm_ior_calc_ma_angle(float io_ma, float angle_deg)
{
    if (!isfinite(io_ma) || !isfinite(angle_deg))
    {
        return 0.0f;
    }

    float ior_ma = io_ma * cosf(angle_deg * (TJ3_COMM_PI / 180.0f));
    return apply_phase_correction(ior_ma);
}

float tj3_comm_ior_calc_ma(float io_ma, float pf)
{
    if (!isfinite(io_ma) || !isfinite(pf))
    {
        return 0.0f;
    }

    if (pf > 1.0f)
    {
        pf = 1.0f;
    }
    else if (pf < -1.0f)
    {
        pf = -1.0f;
    }

    /* With alpha=0 and cos(theta)=PF */
    float ior_ma = io_ma * pf;
    return apply_phase_correction(ior_ma);
}
