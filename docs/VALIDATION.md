# 验证记录

## V33 USB3 双面与 CC ADC 验证

目标板烧录 V33 后，用户在同一硬件上翻转设备端 Type-C 插头进行验证，两种方向均成功建立 USB 3.0 SuperSpeed 链路。

当前 Port B 连接面的固件诊断输出：

```text
status=0 payload_version=1 mode=auto
CC1: raw=0 mv=0 active=False (PA1/ADC1_CH1)
CC2: raw=515 mv=415 active=True (PA0/ADC1_CH0)
ADC_ready=True EN_N=0 SEL=1 orientation_valid=True applied_port=B
```

这验证了当前连接面的 ADC 转换、CC2 判定和 Port B 选择。另一插入方向由用户确认达到 SuperSpeed，但本记录没有保存该方向的完整诊断输出，不能虚构其 raw/mV 数值。

V32 的同一诊断曾显示两路 `raw=65535`、`mv=65535`，即转换超时值 `0xFFFF`。V33 按 CH32H417 官方 ADC 初始化顺序补全时钟、单次转换字段、LowPowerMode、使能和校准，并要求两路真实转换均完成后才允许自动判向。双面 SuperSpeed 是该修复的实机结果。

## V19 USB2 累计压力测试

在同一物理 USB 连接下依次执行：

| 阶段 | 轮数 | 每轮样本 | 结果 |
|---|---:|---:|---|
| 逻辑采集 | 100 | 65536 | PASS |
| ADC CH1 | 100 | 65536 | PASS |
| ADC CH2 | 100 | 65536 | PASS |
| ADC 后再次逻辑采集 | 100 | 65536 | PASS |

四阶段合计 400 轮，日志中每轮 HEADER/END 各 1、`bad_order=0`，未记录短写、超时或零样本。原始日志位于 `validation/logs/`。

该结果只覆盖 V19 当时的 USB2 场景，不自动等价于 V33 USB3 的长时间稳定性。

## 历史撤回记录

- V26、V27、V28、V30：目标板存在无法枚举，禁止作为发布固件；
- V25/V29/V31：用于分层恢复和定位；
- V32：能够运行，但 CC ADC 超时使方向保持 Port B，仅一面 SuperSpeed；
- V33：当前推荐测试版本，双面 SuperSpeed 已实测。

## 软件验证边界

- V33 双核构建：PASS；
- WCH-Link 完整恢复镜像已逐段核对；
- 硬件资源锁：PASS；
- spec 完整性：PASS；
- 外存、离线功能、完整 UI、最终 ADC 标定和 USB3 长时间压力测试仍未验收。
