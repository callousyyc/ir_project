/**
 * @file ir_hal.c
 * @brief IR硬件抽象层实现 - nRF52840
 */

#include "ir_hal.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(ir_hal, LOG_LEVEL_INF);

/* PWM设备 */
static const struct pwm_dt_spec pwm_ir = PWM_DT_SPEC_GET(IR_TX_NODE);

/* GPIO设备和引脚 */
static const struct device *gpio_dev = DEVICE_DT_GET(IR_RX_PIN_NODE);
static struct gpio_callback gpio_cb;

/* 接收状态 */
static struct {
  ir_rx_callback_t callback;
  void *user_data;
  uint64_t last_edge_us;
  bool last_state;
  bool active;
} rx_state;

/* GPIO中断处理 - 接收边沿检测 */
static void gpio_callback_handler(const struct device *dev,
                                  struct gpio_callback *cb, uint32_t pins) {
  if (!rx_state.active) {
    return;
  }

  uint64_t now_us = k_cyc_to_us_floor64(k_cycle_get_64());
  int current_state = gpio_pin_get(gpio_dev, IR_RX_PIN);

  if (current_state < 0) {
    return;
  }

  if (rx_state.last_edge_us > 0) {
    uint64_t duration = now_us - rx_state.last_edge_us;

    if (duration > 0 && duration < IR_MAX_PULSE_US) {
      ir_pulse_t pulse = {
          .duration_us = (uint32_t)duration,
          .is_mark = !rx_state.last_state // 上一个状态
      };

      if (rx_state.callback) {
        rx_state.callback(&pulse, rx_state.user_data);
      }
    }
  }

  rx_state.last_edge_us = now_us;
  rx_state.last_state = (current_state != 0);
}

/* HAL初始化 */
int ir_hal_init(void) {
  int ret;

  /* 检查PWM设备 */
  if (!device_is_ready(pwm_ir.dev)) {
    LOG_ERR("PWM device not ready");
    return -ENODEV;
  }

  /* 检查GPIO设备 */
  if (!device_is_ready(gpio_dev)) {
    LOG_ERR("GPIO device not ready");
    return -ENODEV;
  }

  /* 配置RX GPIO为输入，带上拉 */
  ret = gpio_pin_configure(gpio_dev, IR_RX_PIN, GPIO_INPUT | GPIO_PULL_UP);
  if (ret < 0) {
    LOG_ERR("Failed to configure RX pin: %d", ret);
    return ret;
  }

  /* 配置GPIO中断 */
  ret = gpio_pin_interrupt_configure(gpio_dev, IR_RX_PIN, GPIO_INT_DISABLE);
  if (ret < 0) {
    LOG_ERR("Failed to configure interrupt: %d", ret);
    return ret;
  }

  gpio_init_callback(&gpio_cb, gpio_callback_handler, BIT(IR_RX_PIN));
  ret = gpio_add_callback(gpio_dev, &gpio_cb);
  if (ret < 0) {
    LOG_ERR("Failed to add callback: %d", ret);
    return ret;
  }

  /* 初始化接收状态 */
  memset(&rx_state, 0, sizeof(rx_state));

  /* 确保PWM初始关闭 */
  pwm_set_dt(&pwm_ir, 0, 0);

  LOG_INF("IR HAL initialized");
  return 0;
}

/* 启动发送 - 配置载波频率 */
int ir_hal_tx_start(uint32_t carrier_freq) {
  uint32_t period_ns = NSEC_PER_SEC / carrier_freq;
  uint32_t pulse_ns = period_ns * IR_PWM_DUTY / 100;

  int ret = pwm_set_dt(&pwm_ir, period_ns, pulse_ns);
  if (ret < 0) {
    LOG_ERR("Failed to start PWM: %d", ret);
    return ret;
  }

  LOG_DBG("TX started: %u Hz", carrier_freq);
  return 0;
}

/* 停止发送 */
int ir_hal_tx_stop(void) {
  pwm_set_dt(&pwm_ir, 0, 0);
  LOG_DBG("TX stopped");
  return 0;
}

/* 发送单个脉冲 */
int ir_hal_tx_pulse(uint32_t duration_us, bool is_mark) {
  if (is_mark) {
    /* Mark: 恢复PWM输出 - 不需要重新配置，PWM保持运行 */
    /* nRF52840 PWM特性：一旦配置就持续输出 */
  } else {
    /* Space: 暂时禁用PWM */
    pwm_set_dt(&pwm_ir, 0, 0);
  }

  k_busy_wait(duration_us);

  /* 如果是space后，恢复PWM为下一个mark做准备 */
  if (!is_mark) {
    uint32_t period_ns = NSEC_PER_SEC / IR_CARRIER_FREQ;
    uint32_t pulse_ns = period_ns * IR_PWM_DUTY / 100;
    pwm_set_dt(&pwm_ir, period_ns, pulse_ns);
  }

  return 0;
}

/* 启动接收 */
int ir_hal_rx_start(ir_rx_callback_t callback, void *user_data) {
  if (!callback) {
    return -EINVAL;
  }

  rx_state.callback = callback;
  rx_state.user_data = user_data;
  rx_state.last_edge_us = 0;
  rx_state.last_state = false;
  rx_state.active = true;

  /* 启用双边沿中断 */
  int ret =
      gpio_pin_interrupt_configure(gpio_dev, IR_RX_PIN, GPIO_INT_EDGE_BOTH);
  if (ret < 0) {
    LOG_ERR("Failed to enable RX interrupt: %d", ret);
    return ret;
  }

  LOG_INF("RX started");
  return 0;
}

/* 停止接收 */
int ir_hal_rx_stop(void) {
  rx_state.active = false;
  gpio_pin_interrupt_configure(gpio_dev, IR_RX_PIN, GPIO_INT_DISABLE);

  LOG_INF("RX stopped");
  return 0;
}