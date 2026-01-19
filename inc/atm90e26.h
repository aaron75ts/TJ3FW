#ifndef ATM90E26_H
#define ATM90E26_H

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Status and Special Registers */
#define ATM90E26_REG_SOFTRESET 0x00
#define ATM90E26_REG_SYSSTATUS 0x01
#define ATM90E26_REG_FUNCEN 0x02
#define ATM90E26_REG_SAGTH 0x03
#define ATM90E26_REG_SMALLPMOD 0x04
#define ATM90E26_REG_LASTDATA 0x06
#define ATM90E26_REG_LSB 0x08

/* Metering Calibration & Configuration Registers */
#define ATM90E26_REG_CALSTART 0x20
#define ATM90E26_REG_PLCONSTH 0x21
#define ATM90E26_REG_PLCONSTL 0x22
#define ATM90E26_REG_LGAIN 0x23
#define ATM90E26_REG_LPHI 0x24
#define ATM90E26_REG_NGAIN 0x25
#define ATM90E26_REG_NPHI 0x26
#define ATM90E26_REG_PSTARTTH 0x27
#define ATM90E26_REG_PNOLTH 0x28
#define ATM90E26_REG_QSTARTTH 0x29
#define ATM90E26_REG_QNOLTH 0x2A
#define ATM90E26_REG_MMODE 0x2B
#define ATM90E26_REG_CS1 0x2C

/* Measurement Calibration Registers */
#define ATM90E26_REG_ADJSTART 0x30
#define ATM90E26_REG_UGAIN 0x31
#define ATM90E26_REG_IGAINL 0x32
#define ATM90E26_REG_IGAINN 0x33
#define ATM90E26_REG_UOFFSET 0x34
#define ATM90E26_REG_IOFFSETL 0x35
#define ATM90E26_REG_IOFFSETN 0x36
#define ATM90E26_REG_POFFSETL 0x37
#define ATM90E26_REG_QOFFSETL 0x38
#define ATM90E26_REG_POFFSETN 0x39
#define ATM90E26_REG_QOFFSETN 0x3A
#define ATM90E26_REG_CS2 0x3B

/* Energy & Measurement Registers */
#define ATM90E26_REG_APENERGY 0x40
#define ATM90E26_REG_ANENERGY 0x41
#define ATM90E26_REG_ATENERGY 0x42
#define ATM90E26_REG_RPENERGY 0x43
#define ATM90E26_REG_RNENERGY 0x44
#define ATM90E26_REG_RTENERGY 0x45
#define ATM90E26_REG_ENSTATUS 0x46
#define ATM90E26_REG_IRMS 0x48
#define ATM90E26_REG_URMS 0x49
#define ATM90E26_REG_PMEAN 0x4A
#define ATM90E26_REG_QMEAN 0x4B
#define ATM90E26_REG_FREQ 0x4C
#define ATM90E26_REG_POWERF 0x4D
#define ATM90E26_REG_PANGLE 0x4E
#define ATM90E26_REG_SMEAN 0x4F
#define ATM90E26_REG_IRMS2 0x68
#define ATM90E26_REG_PMEAN2 0x6A
#define ATM90E26_REG_QMEAN2 0x6B
#define ATM90E26_REG_POWERF2 0x6C
#define ATM90E26_REG_PANGLE2 0x6D
#define ATM90E26_REG_SMEAN2 0x6E

/* Constants */
#define ATM90E26_SOFT_RESET_CMD 0x789A
#define ATM90E26_CAL_START_CMD 0x5678
#define ATM90E26_CAL_CHECK_CMD 0x8765
#define ATM90E26_ADJ_START_CMD 0x5678
#define ATM90E26_ADJ_CHECK_CMD 0x8765

    /**
     * @brief Configuration structure for system calibration (Registgers 0x21 - 0x2B)
     *        1. 電表脈衝常數 (Energy Pulse Constant)
     *           pl_const_h (0x21), pl_const_l (0x22):
     *           決定了電能计量與脈衝輸出 (CF) 之間的關係。
     *           也就是設定 imp/kWh (每度電對應多少脈衝)。這個值需要根據您的分流器/互感器參數及目標脈衝常數計算得出。
     *        2. 線路校正參數 (Line Calibration)
     *           用於補償外部電路（如分流器、變壓器）的誤差：
     *           l_gain (0x23), n_gain (0x25): L線/N線 增益校正。用於微調測量到的電壓/電流幅度，確保數值準確。
     *           l_phi (0x24), n_phi (0x26): L線/N線 相位校正。用於補償電壓與電流採樣之間的相位延遲（通常由互感器引起），這對功率因數 (PF) 非 1.0 的負載測量非常重要。
     *        3. 測量閥值 (Measurement Thresholds)
     *           設定電表開始計量或忽略雜訊的界線：
     *           p_start_th (0x27), q_start_th (0x29): 有功/無功 啟動閥值。
     *           當測得的功率低於此值時，晶片不進行電能累積。這就是俗稱的「啟動電流」，確保極微小的待機負載是否計入。
     *           p_nol_th (0x28), q_nol_th (0x2A): 有功/無功 防潛動 (No-Load) 閥值。
     *           當僅有電壓而無電流（空載）時，若測得的微小功率低於此值，則強制歸零，防止電表在無負載時「偷跑」計數。
     *        4. 計量模式 (Metering Mode)
     *           m_mode (0x2B): 計量模式配置。
     *           這是最重要的控制暫存器之一。
     *           設定 頻率 (50Hz/60Hz)。
     *           設定 PGA 增益 (電流通道放大倍率)。
     *           設定 計量方式 (如：絕對值累加、僅正向累加、代數和等)。
     */
    typedef struct
    {
        uint16_t pl_const_h; /* 0x21 PLconstH*/
        uint16_t pl_const_l; /* 0x22 PLconstL */
        uint16_t l_gain;     /* 0x23 Lgain */
        uint16_t l_phi;      /* 0x24 Lphi */
        uint16_t n_gain;     /* 0x25 Ngain */
        uint16_t n_phi;      /* 0x26 Nphi */
        uint16_t p_start_th; /* 0x27 PStartTh */
        uint16_t p_nol_th;   /* 0x28 PNolTh */
        uint16_t q_start_th; /* 0x29 QStartTh */
        uint16_t q_nol_th;   /* 0x2A QNolTh */
        uint16_t m_mode;     /* 0x2B MMode */
    } atm90e26_sys_config_t;

    /**
     * @brief Initialize the ATM90E26 device
     *
     * This function performs the software reset and initializes the system calibration registers.
     *
     * @param dev Pointer to the UART device structure
     * @return 0 on success, negative error code on failure
     */
    int atm90e26_init(const struct device *dev);

    /**
     * @brief Calculate CS1 checksum
     *
     * @param config Pointer to the configuration structure
     * @return Calculated CS1 value
     */
    uint16_t atm90e26_calculate_cs1(const atm90e26_sys_config_t *config);

    int atm90e26_uart_read_reg(const struct device *dev, uint8_t addr, uint16_t *val);
    int atm90e26_uart_write_reg(const struct device *dev, uint8_t addr, uint16_t val);

#ifdef __cplusplus
}
#endif

#endif /* ATM90E26_H */
