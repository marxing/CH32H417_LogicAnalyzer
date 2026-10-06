# 构建说明

## 固件

固件采用 WCH RISC-V Embedded GCC12，分别构建 V3F 和 V5F。

Windows PowerShell：

```powershell
cd firmware/app
.\build.ps1 `
  -Toolchain 'D:\path\to\WCH\Toolchain\RISC-V Embedded GCC12\bin' `
  -BuildDir build_release `
  -OfflineInternalOnly
```

输出位于：

```text
firmware/app/build_release/V3F/
firmware/app/build_release/V5F/
firmware/app/build_release/analyzer_offline_internal_only_IAP_0x6000.bin
```

构建脚本要求 `firmware/app/../SRC` 存在，本发布目录已保持该相对结构。脚本只生成 IAP 应用镜像，不会自动烧录硬件。

V5F 链接还依赖 `firmware/app/Common/ch32h417_uhsif_it.o`。这是现有工程中的预编译对象，当前包没有对应源代码，因此固件并非百分之百可从公开 C 源码重建。

其他隔离构建：

```powershell
.\build.ps1 -Toolchain '...' -BuildDir build_usb_baseline -UsbBaseline
.\build.ps1 -Toolchain '...' -BuildDir build_lcd_keys -LcdKeysOnly
.\build.ps1 -Toolchain '...' -BuildDir build_offline_internal -OfflineInternalOnly
```

V33 发布二进制使用 `-OfflineInternalOnly`，不要用默认完整外存构建冒充 V33。

## Windows 上位机

要求 Windows 10/11 与 MSYS2 MINGW64。在 MINGW64 终端进入 `host`：

```bash
./setup_env.sh
./build.sh --clean
./build.sh --package
```

便携版输出到 `host/dist/LogicAnalyzer/`，通过其中的 `run.bat` 启动。若链接阶段报告 `LogicAnalyzer.exe: Permission denied`，先关闭正在运行的程序及其残留进程，再重新构建。

运行时所需的 WCH `CH375DLL64.DLL` 不随本仓库分发，应从合法的 WCH 驱动/SDK来源取得并使用与系统架构匹配的版本。

完整依赖和手动安装命令见 `host/README_CH32H417.md`。

## 构建与实机的边界

编译成功只能证明语法、链接和镜像布局满足当前脚本，不证明 USB、ADC、MUX、屏幕或外存实机正确。发布新固件时至少记录：固件版本、二进制 SHA-256、USB 速率、线缆方向、模式、采样率、通道、量程、轮数和失败次数。

