# 许可证与第三方代码说明

本目录是一个整合工程，不声明所有文件使用同一个许可证。

- `host/libsigrok/COPYING`：libsigrok 的许可证文本。
- `host/libsigrokdecode/COPYING`：libsigrokdecode 的许可证文本。
- `host/pulseview/COPYING`：PulseView 的许可证文本。
- `firmware/SRC/`：WCH 提供的芯片支持库、启动文件及外设库；使用和再分发前请核对文件头及 WCH 的最新授权条款。
- `firmware/app/`：项目固件及其历史来源代码。当前包未附加新的统一开源许可证。
- `firmware/app/Common/ch32h417_uhsif_it.o`：现有工程依赖的预编译对象，当前资料中没有对应源码；其授权和可再分发范围需向原始提供方确认。
- `firmware/releases/`：上述固件源码的构建产物，仅用于对应硬件的测试和恢复。
- `mechanical/`：项目提供的 STEP 外壳模型；当前未附加独立许可证，公开再分发、修改和商用权限由项目维护者决定并补充声明。

如果计划公开发布或接受外部贡献，建议项目维护者在确认原始作者授权后，为自有代码补充明确的 `LICENSE` 和版权声明。

