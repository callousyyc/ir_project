# nRF52840 红外收发系统

基于Zephyr RTOS的nRF52840红外(IR)收发驱动，支持 **IRDB (Infrared Remote Control Database)**协议数据库和**自学习功能** 。

## ✨ 主要特性

* 🌐 **IRDB支持** - 访问全球最大的IR遥控器数据库
* 🎓 **自学习功能** - 录制和重放任意红外信号
* 📡 **多协议** - 支持NEC、Sony、RC5、Samsung等主流协议
* 💾 **持久存储** - 学习的信号保存到Flash
* 🔄 **灵活加载** - 嵌入式/文件系统/网络多种数据加载方式
* 🛠️ **Shell命令** - 方便的命令行调试接口
* 🔌 **分层架构** - HAL/协议/服务清晰分层

## 系统架构

采用分层设计，从底层到上层依次为：

```
┌─────────────────────────────────────┐
│       应用层 (Application)          │
│  - 发送/接收控制                     │
│  - Shell命令接口                     │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│      服务层 (Service Layer)         │
│  - IRDB数据库管理                    │
│  - 缓存机制                          │
│  - 文件系统/网络加载                 │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│      协议层 (Protocol Layer)        │
│  - IRDB CSV解析                      │
│  - 协议编码/解码                     │
│  - 多协议支持                        │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│        HAL层 (Hardware Layer)       │
│  - PWM载波生成                       │
│  - GPIO中断接收                      │
│  - 定时器管理                        │
└─────────────────────────────────────┘
                 ↕
┌─────────────────────────────────────┐
│          硬件 (nRF52840)            │
│  - PWM外设                           │
│  - GPIO + GPIOTE                    │
│  - Timer                            │
└─────────────────────────────────────┘
```

## 功能特性

### HAL层 (ir_hal.c/h)

* **发送功能**
  * PWM生成38kHz载波（可配置）
  * 精确的脉冲时序控制
  * 支持mark/space调制
* **接收功能**
  * GPIO边沿检测
  * 高精度时间戳测量
  * 中断驱动接收

### IRDB协议层 (irdb_protocol.c/h)

* **协议支持**
  * NEC1/NEC2
  * Sony SIRC (12/15/20位)
  * RC5/RC6
  * Samsung 32/36位
  * 可扩展至更多协议
* **编解码**
  * Protocol,Device,Subdevice,Function格式
  * 自动时序生成
  * 智能信号解码

### IRDB加载器 (irdb_loader.c/h)

* **多种加载方式**
  * 嵌入式存储（编译时包含）
  * 文件系统加载（Flash/SD卡）
  * HTTP/HTTPS加载（从CDN动态获取）
  * 智能缓存机制

### IR服务层 (ir_service.c/h)

* 统一的高级API
* 自动协议识别
* 数据库管理
* 按功能名发送

### IR自学习模块 (ir_learning.c/h) 🆕

* **录制功能**
  * 捕获任意红外信号
  * 精确时序记录 (±10μs)
  * 自动检测信号结束
  * 噪声过滤
* **重放功能**
  * 完整信号重现
  * 支持重复发送
  * 自动载波检测
* **信号管理**
  * 保存到Flash存储
  * 命名和组织
  * 导入/导出
  * 相似度比较
* **分析工具**
  * 信号特征分析
  * 载波频率估算
  * 协议推断

## 支持的协议

* **NEC协议** : 32位，9ms引导码
* **Sony SIRC** : 12/15/20位
* **RC5** : 13位曼彻斯特编码
* **RC6** : 支持多种模式
* **Samsung** : 32/48位
* **原始(RAW)** : 自定义时序

## 硬件连接

### nRF52840DK开发板

```
IR发射端:
  P0.13 → IR LED (通过三极管驱动)
  
IR接收端:
  P0.14 → IR Receiver (TSOP38238等)
  
硬件电路:
  
  发射电路:
  P0.13 ──┬──[R1:100Ω]──┬──(Q1: NPN)
         │              │
        GND            IR LED+
                        │
                       GND
                     
  接收电路:
  P0.14 ───────── OUT (IR Receiver)
  VCC  ───────── VCC
  GND  ───────── GND
```

## 使用方法

### 1. 环境准备

```bash
# 安装Zephyr SDK
cd ~/zephyrproject
source zephyr-env.sh

# 编译项目
west build -b nrf52840dk_nrf52840
```

### 2. 烧录程序

```bash
west flash
```

### 3. 代码示例

#### 方式1: 使用嵌入式IRDB数据

```c
#include "ir_service.h"

// 嵌入式CSV数据
const char samsung_tv[] = 
    "Power,1,7,7,2\n"
    "Vol+,1,7,7,7\n"
    "Vol-,1,7,7,11\n";

// 初始化
ir_service_init();

// 加载数据库
ir_service_load_embedded_csv(samsung_tv, "Samsung", "TV");

// 发送命令
ir_service_send_command("Power", 1);
```

#### 方式2: 从文件系统加载

```c
// 配置加载参数
ir_service_config_t config = {
    .load_method = IRDB_LOAD_FILESYSTEM,
    .device = 7,
    .subdevice = 7
};
strcpy(config.manufacturer, "Samsung");
strcpy(config.device_type, "TV");

// 从 /lfs/irdb/Samsung/TV/7,7.csv 加载
ir_service_load_remote(&config);
```

#### 方式3: 从网络动态加载（需要WiFi/以太网）

```c
// 配置网络加载
ir_service_config_t config = {
    .load_method = IRDB_LOAD_HTTP,
    .device = 7,
    .subdevice = 7
};
strcpy(config.manufacturer, "Samsung");
strcpy(config.device_type, "TV");

// 从 CDN 下载: 
// https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes/Samsung/TV/7,7.csv
ir_service_load_remote(&config);
```

#### 接收IR信号

```c
void my_callback(const irdb_entry_t *entry, void *data)
{
    printk("Received: %s\n", entry->function_name);
    printk("Protocol: %u, Device: %u.%u, Function: %u\n",
           entry->protocol, entry->device, 
           entry->subdevice, entry->function);
}

// 启动接收
ir_service_start_receive(my_callback, NULL);

// ... 等待接收 ...

// 停止接收
ir_service_stop_receive();
```

### 4. Shell命令

#### IRDB命令

```bash
# 加载嵌入式数据库
ir load samsung
ir load sony

# 列出所有功能
ir list

# 发送命令
ir send Power
ir send Vol+ 3  # 重复3次

# 接收信号（10秒）
ir receive 10

# 从文件加载（需要文件系统支持）
ir loadfile Samsung TV 7,7
```

#### 自学习命令 🆕

```bash
# 学习新信号
irlearn learn Power           # 学习Power按键
irlearn learn VolumeUp 10000  # 10秒超时

# 重放学习的信号
irlearn replay Power          # 发送1次
irlearn replay Power 3        # 发送3次

# 管理学习的信号
irlearn list                  # 列出所有
irlearn delete Power          # 删除信号

# 分析信号
irlearn analyze Power         # 显示信号特征
irlearn compare Power1 Power2 # 比较相似度
irlearn export Power          # 导出为文本

# 典型工作流程
irlearn learn TV_Power        # 步骤1: 学习
irlearn replay TV_Power       # 步骤2: 测试
irlearn analyze TV_Power      # 步骤3: 分析
# 完成！信号已保存到Flash
```

## IRDB数据格式

IRDB使用简洁的CSV格式存储IR码：

```csv
function_name,protocol,device,subdevice,function
Power,1,7,7,2
Vol+,1,7,7,7
Vol-,1,7,7,11
```

### 协议编号对照表

| 协议ID | 名称      | 说明                |
| ------ | --------- | ------------------- |
| 1      | NEC1      | 标准NEC协议，32位   |
| 2      | NEC2      | NEC变体，16位设备码 |
| 4      | RC5       | Philips RC5，13位   |
| 5      | RC6       | Philips RC6         |
| 15     | Sony12    | Sony SIRC 12位      |
| 16     | Sony15    | Sony SIRC 15位      |
| 17     | Sony20    | Sony SIRC 20位      |
| 20     | Samsung32 | Samsung 32位        |

### IRDB在线资源

* **官方仓库** : https://github.com/probonopd/irdb
* **CDN访问** : https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes/
* **索引文件** : https://cdn.jsdelivr.net/gh/probonopd/irdb@master/codes/index

### 数据库示例路径

```
Samsung/TV/7,7.csv
Sony/TV/1,0.csv
LG/TV/56,56.csv
Philips/TV/0,0.csv
```

## 文件结构

```
ir_project/
├── CMakeLists.txt
├── prj.conf
├── nrf52840dk_nrf52840.overlay
├── include/
│   ├── ir_hal.h              # HAL层接口
│   ├── irdb_protocol.h       # IRDB协议定义
│   ├── irdb_loader.h         # 数据加载器
│   ├── ir_service.h          # 服务层接口
│   └── ir_learning.h         # 自学习模块 🆕
├── src/
│   ├── main.c                # 应用示例
│   ├── ir_hal.c              # HAL实现
│   ├── irdb_protocol.c       # 协议编解码
│   ├── irdb_loader.c         # 加载器实现
│   ├── ir_service.c          # 服务层实现
│   ├── ir_learning.c         # 自学习实现 🆕
│   └── ir_learning_app.c     # 学习Shell命令 🆕
├── configs/
│   └── irdb_samples/         # IRDB示例文件
│       ├── Samsung_TV_7_7.csv
│       ├── Sony_TV_1_0.csv
│       └── LG_TV_56_56.csv
└── README.md
```

## 配置选项

### prj.conf关键配置

```conf
# 基础功能
CONFIG_GPIO=y           # GPIO支持
CONFIG_PWM=y            # PWM支持
CONFIG_NRFX_TIMER=y     # 定时器支持
CONFIG_NRFX_GPIOTE=y    # GPIO中断支持

# Shell命令行
CONFIG_SHELL=y

# 文件系统支持（可选）
CONFIG_FILE_SYSTEM=y
CONFIG_FILE_SYSTEM_LITTLEFS=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y

# 网络支持（可选，用于HTTP加载）
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_HTTP_CLIENT=y
CONFIG_NET_IPV4=y
CONFIG_DNS_RESOLVER=y

# 内存配置
CONFIG_HEAP_MEM_POOL_SIZE=16384  # 增大堆内存用于数据库
```

### 设备树配置

在 `nrf52840dk_nrf52840.overlay` 中配置引脚:

```dts
ir_tx: ir-tx {
    gpios = <&gpio0 13 GPIO_ACTIVE_HIGH>;
};

ir_rx: ir-rx {
    gpios = <&gpio0 14 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
};
```

## 性能参数

* **发送精度** : ±5μs
* **接收精度** : ±10μs
* **最大脉冲宽度** : 100ms
* **支持载波频率** : 30kHz - 56kHz
* **最大按键数** : 受内存限制（通常>1000）
* **解码容差** : 20%（可配置）

## 调试技巧

### 1. 查看日志

```bash
# 连接串口
minicom -D /dev/ttyACM0 -b 115200

# 或使用
screen /dev/ttyACM0 115200
```

### 2. 测试发射

使用手机摄像头或数字相机可以看到IR LED发光

### 3. 测试接收

使用任意IR遥控器对准接收器

### 4. 调整时序

如果解码失败，可以调整LIRC配置中的时序参数

## 常见问题

**Q: 接收不到信号？**

* 检查接收器供电和接线
* 确认接收器型号和载波频率匹配
* 增大容差参数

**Q: 发送距离太短？**

* 增大IR LED驱动电流（不超过规格）
* 使用聚焦透镜
* 检查PWM占空比

**Q: 协议不匹配？**

* 确认LIRC配置正确
* 使用逻辑分析仪捕获真实信号
* 调整时序参数

## 扩展功能

### 1. 自学习功能 🆕

 **适用场景** :

* IRDB中不存在的遥控器型号
* 自定义或非标准协议
* 旧型号或工业设备遥控器

 **使用方法** :

```c
#include "ir_learning.h"

// 初始化
ir_learning_init();

// 学习信号（通过Shell或编程）
irlearn learn MyRemote_Power

// 重放
irlearn replay MyRemote_Power

// 自动保存到Flash: /lfs/ir_learned/MyRemote_Power.dat
```

 **特性** :

* ✅ 高精度时序捕获 (±10μs)
* ✅ 自动信号结束检测
* ✅ Flash持久化存储
* ✅ 信号分析和比较
* ✅ 导出/导入功能

详见：[自学习使用指南](https://claude.ai/chat/docs/learning_guide.md)

---

### 2. 添加新遥控器

从IRDB获取CSV文件并使用：

```c
// 方法1: 嵌入式
const char *my_remote = "Power,1,10,10,1\n...";
ir_service_load_embedded_csv(my_remote, "MyBrand", "TV");

// 方法2: 下载到Flash
irdb_download_and_cache("MyBrand", "TV", 10, 10);

// 方法3: 手动创建CSV文件放入SD卡或Flash
```

---

### 3. 文件系统集成

启用LittleFS从Flash加载IRDB：

```c
#include "irdb_filesystem_setup.h"

// 初始化文件系统
irdb_filesystem_init();

// 预装常用数据库
irdb_preload_databases();

// 列出可用数据库
irdb_list_available();

// 加载
ir_service_config_t config = {
    .load_method = IRDB_LOAD_FILESYSTEM,
    // ...
};
ir_service_load_remote(&config);
```

### 3. 网络动态加载

需要WiFi或以太网模块：

```c
// 启用网络协议栈
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y

// 配置网络加载
ir_service_config_t config = {
    .load_method = IRDB_LOAD_HTTP,
    // ...
};

// 自动从CDN下载
ir_service_load_remote(&config);
```

### 4. 多数据库切换

```c
// 切换到Samsung TV
ir_service_load_embedded_csv(samsung_data, "Samsung", "TV");
ir_service_send_command("Power", 1);

// 切换到Sony TV
ir_service_load_embedded_csv(sony_data, "Sony", "TV");
ir_service_send_command("Power", 1);
```

### 5. 自定义协议

如果IRDB不支持某个协议，可扩展协议参数表：

```c
// 在 irdb_protocol.c 中添加
{
    .protocol_id = IRDB_PROTOCOL_CUSTOM,
    .name = "MyProtocol",
    .frequency = 38000,
    .header_mark = 3000,
    // ...
}
```

## 参考资料

* [IRDB官方仓库](https://github.com/probonopd/irdb)
* [IRDB CDN访问](https://www.jsdelivr.com/)
* [IrScrutinizer](https://github.com/bengtmartensson/harctoolboxbundle) - IRDB数据分析工具
* [nRF52840规格书](https://www.nordicsemi.com/products/nrf52840)
* [Zephyr文档](https://docs.zephyrproject.org/)

## 贡献IRDB

如果你录制了新的遥控器码，欢迎贡献到IRDB：

1. 访问 https://github.com/probonopd/irdb
2. 按照格式创建CSV文件
3. 提交Pull Request

这样全世界的开发者都能使用你的贡献！

## 许可证

MIT License

## 贡献

欢迎提交Issue和Pull Request
