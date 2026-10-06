#!/bin/bash
# ============================================================================
# setup_env.sh - CH32H417 LogicAnalyzer 编译+运行环境一键部署
# 在 MSYS2 MINGW64 shell 中运行
# ============================================================================
set -e

if [[ "$MSYSTEM" != "MINGW64" ]]; then
    echo "[错误] 请在 MSYS2 MINGW64 shell 中运行（开始菜单 → MSYS2 MINGW64）"
    exit 1
fi

echo ">>> 安装编译环境..."

pacman -S --needed --noconfirm \
    mingw-w64-x86_64-{gcc,cmake,ninja,pkgconf,gdb} \
    mingw-w64-x86_64-{glib2,glibmm,libusb,hidapi,libzip} \
    mingw-w64-x86_64-{boost,qt5-static,python}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cat <<EOF

============================================
 环境部署完成！
============================================

运行时环境变量（每次打开新终端需设置）：

  export PYTHONHOME=/mingw64
  export SIGROKDECODE_DIR=${SCRIPT_DIR}/install/share/libsigrokdecode/decoders

永久生效（写入 ~/.bashrc）：

  echo 'export PYTHONHOME=/mingw64' >> ~/.bashrc
  echo 'export SIGROKDECODE_DIR=${SCRIPT_DIR}/install/share/libsigrokdecode/decoders' >> ~/.bashrc

现在可以运行:

  ./build.sh         # 编译
  cd build_cmake/pulseview_build && ./LogicAnalyzer.exe   # 运行
EOF
