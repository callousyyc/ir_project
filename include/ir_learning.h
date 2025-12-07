/**
 * @file ir_learning.h
 * @brief IR自学习模块 - 录制和重放任意红外信号
 */

#ifndef IR_LEARNING_H
#define IR_LEARNING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 学习的信号最大长度 */
#define IR_LEARNING_MAX_EDGES 512
#define IR_LEARNING_MAX_DURATION_US 100000

/* 学习信号结构 */
typedef struct {
  char name[32];              // 信号名称
  uint32_t *timings;          // 时序数据数组 (微秒)
  uint16_t timing_count;      // 时序数量
  uint32_t carrier_freq;      // 检测到的载波频率
  uint32_t total_duration_us; // 总时长
  bool valid;                 // 是否有效
} ir_learned_signal_t;

/* 学习回调 - 学习过程中的状态通知 */
typedef enum {
  IR_LEARN_IDLE,      // 空闲
  IR_LEARN_WAITING,   // 等待信号
  IR_LEARN_RECEIVING, // 接收中
  IR_LEARN_COMPLETED, // 完成
  IR_LEARN_TIMEOUT,   // 超时
  IR_LEARN_ERROR      // 错误
} ir_learn_status_t;

typedef void (*ir_learn_callback_t)(ir_learn_status_t status,
                                    const ir_learned_signal_t *signal,
                                    void *user_data);

/* 初始化学习模块 */
int ir_learning_init(void);

/* 开始学习模式 */
int ir_learning_start(const char *signal_name, ir_learn_callback_t callback,
                      void *user_data, uint32_t timeout_ms);

/* 停止学习 */
int ir_learning_stop(void);

/* 重放学习的信号 */
int ir_learning_replay(const ir_learned_signal_t *signal,
                       uint32_t repeat_count);

/* 保存学习的信号到存储 */
int ir_learning_save(const ir_learned_signal_t *signal, const char *name);

/* 从存储加载信号 */
int ir_learning_load(ir_learned_signal_t *signal, const char *name);

/* 删除已保存的信号 */
int ir_learning_delete(const char *name);

/* 列出所有已保存的信号 */
int ir_learning_list(char *buf, size_t buf_size);

/* 导出为原始格式 (用于调试) */
int ir_learning_export_raw(const ir_learned_signal_t *signal, char *buf,
                           size_t buf_size);

/* 从原始格式导入 */
int ir_learning_import_raw(ir_learned_signal_t *signal, const char *raw_data);

void test_ir_learning(void);

/* 分析信号特征 */
typedef struct {
  uint32_t avg_mark;       // 平均mark时长
  uint32_t avg_space;      // 平均space时长
  uint32_t min_pulse;      // 最短脉冲
  uint32_t max_pulse;      // 最长脉冲
  uint32_t pulse_count;    // 脉冲数量
  uint32_t estimated_freq; // 估计的载波频率
} ir_signal_analysis_t;

int ir_learning_analyze(const ir_learned_signal_t *signal,
                        ir_signal_analysis_t *analysis);

/* 比较两个信号的相似度 (0-100%) */
int ir_learning_compare(const ir_learned_signal_t *sig1,
                        const ir_learned_signal_t *sig2, uint8_t *similarity);

#endif /* IR_LEARNING_H */