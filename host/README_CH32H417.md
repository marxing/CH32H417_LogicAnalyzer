# CH32H417 LogicAnalyzer PC软件构建

该工程仅支持项目对应的 CH32H417 逻辑分析仪，支持 130 种协议解码（I²C/SPI/UART 等）， sigrok 自带的 78 个驱动已裁剪至 2 个，纯 CMake 一键构建。 

## 快速开始

```bash
./setup_env.sh        # 一键部署编译环境（仅首次）
./build.sh            # 编译
cd build_cmake/logicanalyzer_build && ./LogicAnalyzer.exe  # 运行
```

## 环境要求

**Windows 10/11 + MSYS2 MINGW64**

```bash
# 一键部署
./setup_env.sh

# 或手动安装:
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-{gcc,cmake,ninja,pkgconf,gdb} \
    mingw-w64-x86_64-{glib2,glibmm,libusb,hidapi,libzip} \
    mingw-w64-x86_64-{boost,qt5-static,python}
```

> 必须用 **MSYS2 MINGW64** shell（开始菜单 → MSYS2 MINGW64）。

## 构建

```bash
./build.sh              # 增量编译
./build.sh --clean      # 全量重编
./build.sh --package    # 编译 + 打包到 dist/LogicAnalyzer/
```

产物：
- `build_cmake/logicanalyzer_build/LogicAnalyzer.exe` — 主程序（23 个 DLL 自动部署）
- `install/` — libsigrok + libsigrokcxx + libsigrokdecode + 130 个解码器

## 运行

```bash
export PYTHONHOME=/mingw64
export SIGROKDECODE_DIR=install/share/libsigrokdecode/decoders
cd build_cmake/logicanalyzer_build
./LogicAnalyzer.exe
```

> 写入 `~/.bashrc` 可永久生效。

## 项目结构

```
.
├── setup_env.sh                       
├── build.sh                           
├── package_installer.sh               
├── README_CH32H417.md
├── libsigrok/                         
│   ├── CMakeLists.txt
│   └── src/hardware/
│       ├── ch32h417/                  
│       └── demo/                      
├── libsigrokdecode/                   
│   └── decoders/                      
├── pulseview/                         
│   ├── CMakeLists.txt
│   ├── main.cpp                       
│   └── pv/                            
└── build_cmake/                       
    └── logicanalyzer_build/           
```

## 打包

```bash
# 便携版
./build.sh --package
# 产物: dist/LogicAnalyzer/（双击 run.bat 运行）

# Windows 安装包
./package_installer.sh
# 产物: dist/LogicAnalyzer/LogicAnalyzer_Setup.exe
```

安装包特性：开始菜单 + 桌面快捷方式，控制面板可卸载。

## 添加自定义驱动

1. 在 `libsigrok/src/hardware/` 下新建 `your-device/`
2. 实现 `api.c`（必须）、`protocol.c`、`protocol.h`
3. 在 `libsigrok/CMakeLists.txt` → `LIBSIGROK_SOURCES` 添加源文件
4. 在 `libsigrok/config.h.cmake` 添加 `#define HAVE_HW_YOUR_DEVICE 1`
5. 在 `libsigrok/CMakeLists.txt` 添加 `set(HAVE_HW_YOUR_DEVICE 1)`
6. `./build.sh` 重新构建

## REV20260912 板卡扩展

当前源码已经接入新板卡固件 FW08 的兼容扩展：

- 保留原有 `0xA0` 至 `0xAE` 实时逻辑/ADC采集和 IAP 升级协议；
- 支持 `0xAF` 设置双通道四档模拟前端量程；
- ADC 波形按新反相 AFE（`Rin=99.8k`，`Rf=6.98k/28k/140k/280k`）换算为输入电压；
- “Help -> On-device Capture” 显示 PSRAM、NAND、格式化状态和当前离线捕获状态；
- 可将 FW08 当前捕获导出为 `.dla`：64 字节 `CaptureHeader` 后紧跟原始数据。

目前 FW08 仅开放当前捕获的只读 `0xC0/0xC1/0xC2` 接口，因此 Flash 历史记录目录、按记录加载/删除、格式化和校准参数写入尚未在 PC 端启用。ADC 零点暂按 512/1023 的标称中点换算，完成实机校准协议后再替换为每通道、每量程的实测参数。

若电脑尚未安装完整 MSYS2 MINGW64 依赖，先运行 `./setup_env.sh`，再运行 `./build.sh --clean`。仅安装 Windows 版 CMake 和 Qt 不足以构建本工程，因为还需要 MinGW 版本的 glib、glibmm、libusb、hidapi、libzip、Boost 和 Python。
