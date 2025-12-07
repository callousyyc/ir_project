/**
 * @file ir_hal.h
 * @brief IR硬件抽象层 - nRF52840
 */

#ifndef IR_HAL_H
#define IR_HAL_H

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

/* 配置参数 */
#define IR_TX_NODE DT_NODELABEL(tx_pwm)
#define IR_RX_PIN 12           // P0.14 用于IR接收
#define IR_CARRIER_FREQ 38000  // 38kHz载波
#define IR_PWM_DUTY 33         // 33%占空比
#define IR_MAX_PULSE_US 100000 // 最大脉冲宽度100ms
#define IR_TIMER_FREQ 1000000  // 1MHz定时器频率

/* IR脉冲结构 */
typedef struct {
  uint32_t duration_us; // 持续时间(微秒)
  bool is_mark;         // true=mark(载波), false=space(无载波)
} ir_pulse_t;

/* IR接收回调 */
typedef void (*ir_rx_callback_t)(ir_pulse_t *pulse, void *user_data);

/* HAL初始化 */
int ir_hal_init(void);

/* 发送接口 */
int ir_hal_tx_start(uint32_t carrier_freq);
int ir_hal_tx_stop(void);
int ir_hal_tx_pulse(uint32_t duration_us, bool is_mark);

/* 接收接口 */
int ir_hal_rx_start(ir_rx_callback_t callback, void *user_data);
int ir_hal_rx_stop(void);

#endif /* IR_HAL_H */