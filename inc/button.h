/**
 * @file button.h
 * @brief 設定模式按鈕 (S Button) 控制模組
 *
 * 實現 "S" 按鈕長按 3 秒的設定模式解鎖功能
 * 按鈕功能：
 * - 長按 3 秒：進入設定模式 (允許 BLE 修改設定)
 * - 自動超時：30 秒後自動退出設定模式
 */

#ifndef BUTTON_H_
#define BUTTON_H_

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * @brief 初始化按鈕模組
 *
 * 配置 GPIO 和定時器
 *
 * @return 0 成功，負值為錯誤碼
 */
int button_init(void);

/**
 * @brief 檢查是否處於設定模式
 *
 * @return true 處於設定模式 (允許修改設定)
 * @return false 未處於設定模式 (拒絕修改設定)
 */
bool button_is_set_mode_enabled(void);

/**
 * @brief 手動進入設定模式 (用於測試)
 *
 * @param duration_sec 持續時間 (秒)，0 表示立即退出
 */
void button_enter_set_mode(uint16_t duration_sec);

/**
 * @brief 手動退出設定模式
 */
void button_exit_set_mode(void);

#endif /* BUTTON_H_ */
