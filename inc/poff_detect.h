/**
 * @file poff_detect.h
 * @brief Power-off Detect (停電偵測) 模組
 *
 * 監測系統電壓狀態，在停電時觸發數據保存和警報通報
 */

#ifndef POFF_DETECT_H_
#define POFF_DETECT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 停電處理回調函數類型
     *
     * 在檢測到停電時，會依次調用所有註冊的回調函數
     * 以便各模組保存關鍵數據
     */
    typedef void (*poff_callback_t)(void);

    /**
     * @brief 初始化停電檢測模組
     * @return 0 成功, <0 錯誤
     */
    int poff_detect_init(void);

    /**
     * @brief 註冊停電處理回調函數
     *
     * 各模組可以註冊回調函數，在停電時保存關鍵數據
     *
     * @param callback 回調函數指標
     * @return 0 成功, <0 錯誤
     */
    int poff_detect_register_callback(poff_callback_t callback);

    /**
     * @brief 檢查當前是否處於停電狀態
     * @return true 停電, false 正常供電
     */
    bool poff_detect_is_power_off(void);

#ifdef __cplusplus
}
#endif

#endif /* POFF_DETECT_H_ */
