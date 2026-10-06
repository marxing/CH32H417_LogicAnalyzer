# 故障排查

## 有插入提示，但上位机找不到设备

1. 在设备管理器确认硬件 ID 为 `USB\VID_1A86&PID_5537`。
2. 确认安装的是 CH372/CH375 驱动，不是 CH341/CH343 串口驱动。
3. x64 检查 `CH375W64.SYS`，ARM64 检查 `CH375M64.SYS`。
4. 确认上位机目录或系统搜索路径中存在匹配架构的 `CH375DLL64.DLL`。

## 只显示 USB2 High-Speed

驱动不决定 SuperSpeed 链路训练。检查：

- 线缆是否真实支持 USB3；
- 主机端口是否为 SuperSpeed；
- PF10/EN_N 是否为 0 V；
- PD8/SEL 是否随设备端 Type-C 翻面而改变；
- CC1/CC2 是否只有一路处于有效电压；
- USB Device Tree Viewer 的连接速度，而不是产品字符串。

## WCH-Link 无法连接

本板 SWD 与 USB 复用。断开设备 USB，只保留 WCH-Link，重新上电后再连接。不要同时让应用固件进入 USB 工作状态。

## IAP 更新后不枚举或屏幕闪烁

停止反复 IAP 尝试，断开 USB，使用 `FULL_RECOVERY_*_WCHLINK_0x00000000.bin`，在 WCH-LinkUtility 地址栏填写 `0x08000000` 后完整恢复。文件名中的 `0x00000000` 是芯片 Flash 内部偏移，不是工具地址栏的绝对地址。恢复后先验证 USB 枚举，再逐步启用屏幕、按键和外存功能。

## USB3 只有一面可用

1. 运行 `python tools/usb3_mux_control.py status`；
2. 若 CC1/CC2 均为 `raw=65535`，说明 CC ADC 转换超时，不要继续调整方向阈值；
3. 正常状态应为 `ADC_ready=True`，且有效 CC 与 `applied_port`、PD8/SEL对应；
4. 确认固件为 V33 或更新版本，并分别冷启动验证两种方向的 SuperSpeed；
5. `EN_N` 应为低电平。强制切换 MUX 会中断当前 USB3 链路。

## ADC 采集失败

1. 先验证逻辑模式是否稳定。
2. 只开启一个 ADC 通道，以较低采样率短采集。
3. 测量 `VCM_1V65`、`BIAS_P`、`SUM_N`、运放输入/输出和 `CHx_AFE_OUT`。
4. 核对量程反馈电阻实装值；历史上曾发生 28 kΩ 档位误焊。
5. 保存失败前后的上位机日志，区分命令短写、无 ADC 数据和 USB 断联。

