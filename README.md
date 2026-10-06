# CH32H417 USB3.0 逻辑分析仪

基于 WCH CH32H417 的便携式逻辑分析仪/模拟采集设备。仓库包含双核固件、基于 PulseView/libsigrok 修改的 Windows 上位机源码、构建说明和验证日志。

> 当前推荐测试版本为 **V33**。该版本已在目标板实测 Type-C 两种插入方向均可建立 USB 3.0 SuperSpeed 链路。它仍是工程验证版本，不是量产发布；PSRAM、外部 Flash 和离线功能在此构建中保持禁用。

## 硬件资料

PCB、原理图与开源工程统一托管在嘉立创开源硬件平台：

[CH32H417 逻辑分析仪硬件工程](https://oshwhub.com/marxing/project_uhubfqat?jspm=hub.zy.zp.gc1___hub.ss.zp&jlc_vid=QgAIU1NfE1kNBAcERgMMUwVVQANWUVcCE1ZZVwYFQ1AxVlNfR1ZaUlNUQlhZXztWKA4dDxMOAgNABAsL)

本仓库不重复维护 PCB 文件，硬件版本与焊接变更请以上述链接为准。

可打印/二次修改的 STEP 外壳模型位于 [`mechanical/`](mechanical/README.md)。打印前请根据实际 PCB、接口和屏幕装配复核尺寸。

## 当前能力

- CH32H417 V3F/V5F 双核固件；
- 数字逻辑采集和 HSADC 模拟采集；
- USB 2.0 High-Speed 与 USB 3.0 SuperSpeed 数据通路；
- FSW3820 Type-C 高速 MUX 自动判向；
- 1.14 英寸 TFT、三按键/波轮输入相关代码；
- PSRAM/外部 Flash 的可选支持框架；未焊器件不会作为本版本的必需启动条件；
- 基于 PulseView/libsigrok 的 Windows 上位机及协议解码。

## 快速开始

1. 阅读 [使用指引](docs/QUICK_START.md)。
2. 设备仍能枚举且 IAP 可用时，升级使用 `firmware/releases/V33_CC_ADC_FIX_INTERNAL_ONLY_IAP_0x6000.bin`。
3. 若 IAP 无法工作，断开设备 USB 后使用 WCH-Link 烧录完整恢复镜像。
4. 重插设备，在 USB Device Tree Viewer 中确认 `Device Connection Speed`。

## 目录结构

```text
firmware/
  app/                 CH32H417 应用固件源码与构建脚本
  SRC/                 WCH 芯片支持库/启动代码
  releases/            V33 IAP 与 WCH-Link 完整恢复镜像
host/                   修改后的 PulseView/libsigrok 上位机源码
tools/                  USB3 MUX/CC 诊断工具
docs/                   使用、构建、硬件、进度和故障排查文档
mechanical/             STEP 外壳模型与机械说明
validation/logs/        精选构建、静态分析和压力测试原始日志
```

## 已知状态

- V19 在同一物理 USB2 连接下完成逻辑 100 轮、ADC CH1 100 轮、ADC CH2 100 轮、返回逻辑 100 轮，共 400 轮采集；每轮 65536 点，未记录短写、乱序或零样本。
- V33 已在目标板实测 Type-C 两种插入方向均可建立 USB 3.0 SuperSpeed 链路；当前连接面的诊断为 `CC2=415 mV`、`ADC_ready=1`、`EN_N=0`、`SEL=1`、Port B。
- V33 修复了旧版 CC ADC 转换超时返回 `0xFFFF`、方向逻辑退回固定 Port B 的问题。
- 实板已焊 PSRAM、未焊外部 Flash；离线存储完整流程尚未验收。

完整矩阵见 [当前进度](docs/STATUS.md) 和 [验证记录](docs/VALIDATION.md)。

## 许可证说明

上位机包含 GPL 授权的 PulseView、libsigrok 和 libsigrokdecode，原始许可证文件保留在对应目录。固件及 WCH SDK 文件的授权状态请分别核对源文件头和厂商条款，详见 [LICENSES.md](LICENSES.md)。在许可证确认前请勿假设整个仓库可按单一许可证再授权。

