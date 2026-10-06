# 使用指引

## 1. 驱动

设备硬件 ID：`USB\VID_1A86&PID_5537`，使用 WCH CH372/CH375 通用设备驱动。

- Windows x64：已见驱动 `CH375W64.SYS`。
- Windows ARM64：已见原生驱动 `CH375M64.SYS`。
- 上位机动态加载 `CH375DLL64.DLL`；便携包运行时需要能够找到该 DLL。

不要安装 CH341/CH343 串口驱动代替 CH372/CH375 驱动。

## 2. IAP 更新 V33

使用文件：

```text
firmware/releases/V33_CC_ADC_FIX_INTERNAL_ONLY_IAP_0x6000.bin
```

1. 关闭正在采集的上位机窗口。
2. 使用已验证可升级的原版或本项目上位机进入 IAP 更新。
3. 选择上述 IAP 文件，等待写入完成。
4. 完全拔出设备，等待屏幕和 USB 供电消失，再重新插入。
5. 查询固件版本，V33 软件版本字节应为 `0x33`。

IAP 更新过程中不要断电。若更新失败后应用无法枚举，使用下面的完整恢复方式。

## 3. WCH-Link 完整恢复

使用文件：

```text
firmware/releases/FULL_RECOVERY_V33_CC_ADC_FIX_INTERNAL_ONLY_WCHLINK_0x00000000.bin
```

1. 断开设备 USB，仅连接 WCH-Link。
2. 本板 SWD 与 USB 有复用关系，USB 同时连接可能导致 WCH-Link 无法连接。
3. 在 WCH-LinkUtility 中选择 CH32H41X/RISC-V，地址填写 `0x08000000`。文件名中的 `0x00000000` 表示镜像内部从芯片 Flash 偏移 0 开始布局。
4. 执行全擦、下载、校验和复位运行。
5. 完成后断开 WCH-Link，再连接设备 USB。

不要把 IAP 文件当作 `0x00000000` 完整镜像烧录。

## 4. USB3 双面验证

保持主机端 Type-C 插头方向不动，只翻转设备端：

1. 每个方向都冷启动一次；
2. 使用 USB Device Tree Viewer 查看：
   - `USB Version: 3.0 (5 Gbit/s)`；
   - `Device maximum Speed: SuperSpeed`；
   - `Device Connection Speed: SuperSpeed`；
3. 记录 CC1、CC2、PD8/SEL 和 PF10/EN_N。

V33 已实测两种方向均为 SuperSpeed。某次 Port B 诊断值为：CC1=0 V、CC2=415 mV、`ADC_ready=1`、SEL=3.3 V、EN_N=0 V。翻面后预期有效 CC 与 SEL 翻转，EN_N 仍为低电平；CC 电压会随主机和线缆变化，不应把 0.4 V 或 0.9 V 当作固定方向码。

可用以下命令读取实时诊断状态：

```powershell
python tools/usb3_mux_control.py status
```

## 5. 基本采集

1. 启动上位机并选择 `USB3.0 (CH32H417)` 设备。
2. 先用逻辑模式、单次短采集验证通信。
3. 切换 ADC 时先只启用一个模拟通道，并从较低采样率验证。
4. 停止采集后再切换模式、通道或量程。
5. 若出现采集失败，先保存日志，再冷启动设备；不要连续快速点击开始/停止。

模拟输入不得超过硬件前端允许范围，具体量程和保护能力以 PCB 工程及实测校准为准。

## 6. 本版本限制

V33 使用 `InternalOnly` 构建：屏幕、按键和在线采集代码保留，但 PSRAM、外部 Flash 与离线存储路径禁用。不要以 V33 判断外部存储功能已经完成。

