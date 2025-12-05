# nRF52840 32位定时器使用说明

## 问题背景

nRF52840使用32位循环计数器，不支持64位 `k_cycle_get_64()` 函数。

### 错误信息

```
ASSERTION FAIL [0] @ kernel.h:1944
64-bit cycle counter not enabled on this platform
```

## 解决方案

### 使用32位计数器

```c
// ❌ 错误 - 不支持
uint64_t now = k_cyc_to_us_floor64(k_cycle_get_64());

// ✅ 正确 - 使用32位
uint32_t now = k_cyc_to_us_floor32(k_cycle_get_32());
```

## 32位溢出处理

### 问题

32位微秒计数器会在约71.5分钟后溢出：

```
UINT32_MAX / 1000000 / 60 = 71.58 分钟
```

### 解决方案

```c
uint32_t last_time = /* 之前的时间 */;
uint32_t now_time = k_cyc_to_us_floor32(k_cycle_get_32());

uint32_t duration;

if (now_time >= last_time) {
    // 正常情况
    duration = now_time - last_time;
} else {
    // 溢出情况
    duration = (UINT32_MAX - last_time) + now_time + 1;
}
```

## IR应用中的影响

### 红外信号特点

* **单个信号持续时间** : 通常 < 200ms
* **最长信号** : 约 1 秒
* **溢出周期** : 71.5 分钟

→  **结论** : IR信号远小于溢出周期，32位足够使用！

### 实际限制

即使发生溢出，只要两次测量间隔 < 71分钟，溢出处理代码就能正确计算时长。

对于IR接收：

* 信号之间通常间隔 < 1秒
* 完全不会遇到溢出问题

## 已修复的代码位置

### 1. ir_hal.c - GPIO中断处理

```c
// 修复前
uint64_t now_us = k_cyc_to_us_floor64(k_cycle_get_64());

// 修复后
uint32_t now_us = k_cyc_to_us_floor32(k_cycle_get_32());

// 添加溢出处理
if (now_us >= rx_state.last_edge_us) {
    duration = now_us - rx_state.last_edge_us;
} else {
    duration = (UINT32_MAX - rx_state.last_edge_us) + now_us + 1;
}
```

### 2. ir_learning.c - 学习模块

```c
// 修复前
learn_state.start_time_us = k_cyc_to_us_floor64(k_cycle_get_64());

// 修复后
learn_state.start_time_us = k_cyc_to_us_floor32(k_cycle_get_32());
```

### 3. 数据类型变更

```c
// 修复前
uint64_t last_edge_us;
uint64_t start_time_us;

// 修复后
uint32_t last_edge_us;
uint32_t start_time_us;
```

## 性能影响

### 精度

* **32位** : 1μs 精度
* **64位** : 1μs 精度
  → **无差异**

### 内存

* **32位** : 4字节
* **64位** : 8字节
  → **节省50%内存**

### 处理速度

* **32位** : 更快（原生硬件支持）
* **64位** : 需要软件模拟
  → **32位更快**

## 测试验证

### 测试1: 正常时序

```c
// 测试代码
uint32_t t1 = k_cyc_to_us_floor32(k_cycle_get_32());
k_busy_wait(1000);  // 等待1ms
uint32_t t2 = k_cyc_to_us_floor32(k_cycle_get_32());

uint32_t diff = t2 - t1;
printk("Duration: %u us (expected ~1000)\n", diff);
```

预期输出: `Duration: 1000 us`

### 测试2: 溢出处理

```c
// 模拟溢出
uint32_t last = UINT32_MAX - 500;  // 溢出前500us
uint32_t now = 500;                 // 溢出后500us

uint32_t duration;
if (now >= last) {
    duration = now - last;
} else {
    duration = (UINT32_MAX - last) + now + 1;
}

printk("Duration: %u us (expected 1000)\n", duration);
```

预期输出: `Duration: 1000 us`

## 最佳实践

### DO ✅

1. **使用32位API**
   ```c
   k_cycle_get_32()
   k_cyc_to_us_floor32()
   ```
2. **处理溢出**
   ```c
   if (now >= last) {
       delta = now - last;
   } else {
       delta = (UINT32_MAX - last) + now + 1;
   }
   ```
3. **使用相对时间**
   ```c
   uint32_t start = get_time();
   // ... do work ...
   uint32_t elapsed = get_time() - start;  // 相对时间
   ```

### DON'T ❌

1. **不要使用64位API**
   ```c
   k_cycle_get_64()      // ❌ 不支持
   k_cyc_to_us_floor64() // ❌ 不支持
   ```
2. **不要假设时间单调递增**
   ```c
   // ❌ 错误 - 未处理溢出
   duration = now - last;
   ```
3. **不要存储绝对时间**
   ```c
   // ❌ 不好 - 会溢出
   uint32_t absolute_time = k_cyc_to_us_floor32(k_cycle_get_32());

   // ✅ 好 - 使用相对时间
   uint32_t elapsed = k_uptime_get_32();
   ```

## 其他平台支持

### 支持64位的平台

* ESP32-C3/S3
* STM32H7
* 某些ARM Cortex-A

### 只支持32位的平台

* nRF52840 ✅ (本项目)
* nRF52832
* STM32F4
* 大多数 Cortex-M4

## 参考资料

* [Zephyr Timing API](https://docs.zephyrproject.org/latest/kernel/services/timing/clocks.html)
* [nRF52840 Product Specification](https://infocenter.nordicsemi.com/pdf/nRF52840_PS_v1.1.pdf)
* [Handling Timer Overflow](https://embeddedgurus.com/stack-overflow/2011/02/efficient-c-tips-13-use-the-modulo-operator-with-caution/)

## 总结

✅ **所有32位定时器问题已修复**
✅ **IR功能完全不受影响**
✅ **性能更好，内存更省**

重新编译即可正常使用！
