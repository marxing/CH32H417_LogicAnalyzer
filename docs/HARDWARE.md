# 硬件摘要

完整 PCB/原理图请访问：

[嘉立创开源硬件工程](https://oshwhub.com/marxing/project_uhubfqat?jspm=hub.zy.zp.gc1___hub.ss.zp&jlc_vid=QgAIU1NfE1kNBAcERgMMUwVVQANWUVcCE1ZZVwYFQ1AxVlNfR1ZaUlNUQlhZXztWKA4dDxMOAgNABAsL)

## 已确认资源

| 功能 | MCU 资源 | 说明 |
|---|---|---|
| USB3 MUX SEL | PD8 | FSW3820 方向选择 |
| USB3 MUX EN_N | PF10 | 低有效使能 |
| CC1 检测 | PA1 / ADC1 Channel 1 | V33 通过 ADC 读取，映射已验证 |
| CC2 检测 | PA0 / ADC1 Channel 0 | V33 通过 ADC 读取，映射已验证 |
| CH1 HSADC | PC3 / HSADC Channel 3 | 模拟前端输出 |
| CH2 HSADC | PC2 / HSADC Channel 2 | 模拟前端输出 |
| TFT 背光 | PA8 | 低有效 |
| TFT GPIO | PB3–PB7 | 时钟、复位、数据、DC、CS |
| 按键 | PE3–PE5 | 上拉、低有效 |

## USB3 MUX 判向

V33 不把某个固定 CC 电压当成方向码，而是比较 CC1/CC2：

- 有效侧需在约 0.15–2.2 V；
- 两路至少相差约 0.15 V；
- 启动阶段连续 4 次一致才使能 MUX；
- 运行阶段连续 6 次确认相反方向才切换；
- ADC 超时或无效读数不会关闭已经工作的 SuperSpeed 链路。

已实测不同连接中的有效 CC 约 0.4 V 或 0.9 V。V33 修复 ADC 初始化后，两种 Type-C 插入方向均已建立 SuperSpeed 链路。

CC ADC 初始化必须遵循本型号已验证顺序：配置 ADC 时钟和完整单次转换字段、启用 LowPowerMode、使能、复位校准并校准。只有 CC1/CC2 两次转换均不为 `0xFFFF` 才能置 `ADC_ready`；超时时保留安全方向并暴露诊断状态。

## 外部存储

当前实板状态：PSRAM 已焊，外部 Flash 未焊。相关代码应继续采用探测成功后才启用的策略。离线采集、历史记录、掉电保存和外部 Flash 文件系统尚未完成整体验证。

