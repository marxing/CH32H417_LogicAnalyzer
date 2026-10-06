#!/bin/bash
# ============================================================================
# build.sh - CH32H417 LogicAnalyzer Build + Package
# Usage:
#   ./build.sh              # Build only
#   ./build.sh --package    # Build + create dist/LogicAnalyzer/ package
#   ./build.sh --clean      # Clean build
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_cmake"
PREFIX="${SCRIPT_DIR}/install"
# Allow packaging to a new directory when an existing distribution folder is
# open in Explorer/a terminal and Windows refuses to remove it.
DIST_DIR="${LOGICANALYZER_DIST_DIR:-${SCRIPT_DIR}/dist/LogicAnalyzer}"
CLEAN=0
PACKAGE=0

# Ensure MinGW tools in PATH on MSYS2
if [[ "$OSTYPE" == "msys" ]] || [[ -n "$MSYSTEM" ]]; then
    export PATH="/mingw64/bin:/usr/bin:$PATH"
fi

for arg in "$@"; do
    case $arg in
        --clean) CLEAN=1 ;;
        --package) PACKAGE=1 ;;
        --prefix) PREFIX="$2"; shift ;;
    esac
    shift 2>/dev/null || true
done

echo "============================================"
echo " CH32H417 LogicAnalyzer Build"
echo "============================================"
echo "Source:  $SCRIPT_DIR"
echo "Build:   $BUILD_DIR"
echo "Install: $PREFIX"
[ $PACKAGE -eq 1 ] && echo "Package: $DIST_DIR"
echo ""

if [ $CLEAN -eq 1 ]; then
    echo "[CLEAN] Removing build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# MinGW detection
MINGW_EXTRA=""
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ -n "$MSYSTEM" ]]; then
    echo "Detected Windows/MinGW build environment"
    MINGW_EXTRA="-DWIN32=ON"
fi

# ============================================================================
# Step 1: libsigrok (CH32H417 + Demo)
# ============================================================================
echo ""
echo "===== Step 1/4: libsigrok (CH32H417 + Demo) ====="

mkdir -p libsigrok_build && cd libsigrok_build
cmake "${SCRIPT_DIR}/libsigrok" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDISABLE_WERROR=ON $MINGW_EXTRA
cmake --build . -j$(nproc 2>/dev/null || echo 4)
cmake --install .
echo "[OK] libsigrok"

# ============================================================================
# Step 2: libsigrokcxx (C++ bindings)
# ============================================================================
echo ""
echo "===== Step 2/4: libsigrokcxx ====="

cd "$BUILD_DIR" && mkdir -p libsigrokcxx_build && cd libsigrokcxx_build
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH}"
cmake "${SCRIPT_DIR}/libsigrok/bindings/cxx" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX" $MINGW_EXTRA
cmake --build . -j$(nproc 2>/dev/null || echo 4)
cmake --install .
echo "[OK] libsigrokcxx"

# ============================================================================
# Step 3: libsigrokdecode
# ============================================================================
echo ""
echo "===== Step 3/4: libsigrokdecode ====="

cd "$BUILD_DIR" && mkdir -p libsigrokdecode_build && cd libsigrokdecode_build
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH}"
cmake "${SCRIPT_DIR}/libsigrokdecode" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDISABLE_WERROR=ON $MINGW_EXTRA
cmake --build . -j$(nproc 2>/dev/null || echo 4)
cmake --install .
echo "[OK] libsigrokdecode"

# ============================================================================
# Step 4: LogicAnalyzer
# ============================================================================
echo ""
echo "===== Step 4/4: LogicAnalyzer ====="

# Generate .qm translation files from .ts sources
if ls "${SCRIPT_DIR}/pulseview/l10n/"*.ts >/dev/null 2>&1; then
    echo "  Generating translations..."
    export PATH="/mingw64/qt5-static/bin:$PATH"
    for ts in "${SCRIPT_DIR}/pulseview/l10n/"*.ts; do
        [ -f "$ts" ] && lrelease "$ts" -qm "${ts%.ts}.qm" 2>/dev/null || true
    done
    echo "  [OK] Translations generated"
fi

cd "$BUILD_DIR" && mkdir -p logicanalyzer_build && cd logicanalyzer_build
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH}"
cmake "${SCRIPT_DIR}/pulseview" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSTATIC_PKGDEPS_LIBS=OFF \
    -DENABLE_DECODE=ON \
    -DENABLE_FLOW=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_SIGNALS=OFF \
    -DDISABLE_WERROR=ON $MINGW_EXTRA
cmake --build . -j$(nproc 2>/dev/null || echo 4)

# Deploy DLLs and create run.bat in build dir
echo ""
echo "  Deploying DLLs..."
PVDIR="$BUILD_DIR/logicanalyzer_build"
for dll in libsigrok.dll libsigrokcxx.dll libsigrokdecode.dll; do
    cp "$PREFIX/bin/$dll" "$PVDIR/" 2>/dev/null || true
    cp "$BUILD_DIR/libsigrok_build/$dll" "$PVDIR/" 2>/dev/null || true
    cp "$BUILD_DIR/libsigrokdecode_build/$dll" "$PVDIR/" 2>/dev/null || true
done
for dll in libglib-2.0-0.dll libglibmm-2.4-1.dll libgobject-2.0-0.dll \
    libsigc-2.0-0.dll libstdc++-6.dll libwinpthread-1.dll \
    libgcc_s_seh-1.dll libgmodule-2.0-0.dll libusb-1.0.dll libhidapi-0.dll \
    libintl-8.dll libiconv-2.dll libpcre2-8-0.dll libffi-8.dll \
    libzip.dll libbz2-1.dll liblzma-5.dll zlib1.dll libzstd.dll libpython3.14.dll; do
    cp "/mingw64/bin/$dll" "$PVDIR/" 2>/dev/null || true
done

# Create run.bat for build dir
cat > "$PVDIR/run.bat" << 'RBEOF'
@echo off
cd /d "%~dp0"
set "PATH=%~dp0;%PATH%"
set "PYTHONHOME=%~dp0python-portable"
set "SIGROKDECODE_DIR=%~dp0decoders"
if exist "..\..\install\share\libsigrokdecode\decoders" (
    set "SIGROKDECODE_DIR=..\..\install\share\libsigrokdecode\decoders"
    set "PYTHONHOME="
)
start "" "LogicAnalyzer.exe"
RBEOF

echo "  $(ls "$PVDIR"/*.dll 2>/dev/null | wc -l) DLLs, run.bat created"

# Strip debug symbols (cuts exe size ~60%, no runtime impact)
echo "  Stripping debug symbols..."
strip "$PVDIR/LogicAnalyzer.exe" 2>/dev/null || echo "  (strip not available, skipping)"
echo "  $(du -sh "$PVDIR/LogicAnalyzer.exe" | cut -f1) after strip"

echo ""
echo "===== BUILD COMPLETE ====="
echo "  LogicAnalyzer.exe: $BUILD_DIR/logicanalyzer_build/LogicAnalyzer.exe"
echo ""

# ============================================================================
# Package: create self-contained distribution
# ============================================================================
if [ $PACKAGE -eq 1 ]; then
    echo "===== Packaging to $DIST_DIR ====="
    rm -rf "$DIST_DIR"
    mkdir -p "$DIST_DIR"

    # 1. Copy exe
    cp "$BUILD_DIR/logicanalyzer_build/LogicAnalyzer.exe" "$DIST_DIR/"
    echo "  LogicAnalyzer.exe"

    # 2. Copy our DLLs
    for dll in libsigrok.dll libsigrokcxx.dll libsigrokdecode.dll; do
        cp "$PREFIX/bin/$dll" "$DIST_DIR/" 2>/dev/null || \
        cp "$BUILD_DIR/libsigrok_build/$dll" "$DIST_DIR/" 2>/dev/null || \
        cp "$BUILD_DIR/libsigrokdecode_build/$dll" "$DIST_DIR/" 2>/dev/null || true
        [ -f "$DIST_DIR/$dll" ] && echo "  $dll"
    done

    # 3. Copy system DLLs
    echo "  System DLLs..."
    for dll in \
        libglib-2.0-0.dll libglibmm-2.4-1.dll libgobject-2.0-0.dll \
        libsigc-2.0-0.dll libstdc++-6.dll libwinpthread-1.dll \
        libgcc_s_seh-1.dll libgmodule-2.0-0.dll \
        libusb-1.0.dll libhidapi-0.dll \
        libintl-8.dll libiconv-2.dll libpcre2-8-0.dll libffi-8.dll \
        libzip.dll libbz2-1.dll liblzma-5.dll \
        zlib1.dll libzstd.dll libpython3.14.dll; do
        cp "/mingw64/bin/$dll" "$DIST_DIR/" 2>/dev/null || true
    done

    # 4. Copy Python standard library (needed for protocol decoders)
    echo "  Python standard library..."
    PYTHON_SRC="/mingw64/lib/python3.14"
    if [ -d "$PYTHON_SRC" ]; then
        mkdir -p "$DIST_DIR/python-portable/lib/python3.14"
        if [ -d "$PYTHON_SRC/lib-dynload" ]; then
            cp -r "$PYTHON_SRC/lib-dynload" "$DIST_DIR/python-portable/lib/python3.14/"
        fi
        for dir in asyncio collections concurrent ctypes distutils email encodings \
                   html http importlib json lib logging multiprocessing \
                   pydoc_data re sqlite3 unittest urllib wsgi xml xmlrpc \
                   _collections_abc.py _compat_pickle.py _py_abc.py \
                   _sitebuiltins.py site.py string.py \
                   abc.py ast.py base64.py bisect.py calendar.py cgi.py \
                   codecs.py copy.py copyreg.py csv.py dataclasses.py \
                   datetime.py decimal.py dis.py enum.py fnmatch.py \
                   functools.py glob.py hashlib.py heapq.py inspect.py io.py \
                   keyword.py linecache.py locale.py math.py operator.py \
                   os.py pathlib.py pickle.py platform.py plistlib.py \
                   pprint.py queue.py random.py reprlib.py selectors.py \
                   shutil.py signal.py socket.py sre_compile.py \
                   sre_constants.py sre_parse.py stat.py string string.py \
                   struct.py subprocess.py sysconfig.py tempfile.py \
                   textwrap.py threading.py token.py tokenize.py \
                   types.py typing.py uuid.py warnings.py weakref.py; do
            cp -r "$PYTHON_SRC/$dir" "$DIST_DIR/python-portable/lib/python3.14/" 2>/dev/null || true
        done
        echo "    $(find "$DIST_DIR/python-portable" -type f | wc -l) files"
    fi

    # 5. Copy protocol decoders
    echo "  Protocol decoders..."
    DECODERS_SRC="$PREFIX/share/libsigrokdecode/decoders"
    if [ -d "$DECODERS_SRC" ]; then
        cp -r "$DECODERS_SRC" "$DIST_DIR/decoders"
        echo "    $(ls "$DIST_DIR/decoders" | wc -l) decoders"
    fi

    # 6. Create run.bat
    cat > "$DIST_DIR/run.bat" << 'EOF'
@echo off
cd /d "%~dp0"
set "PATH=%~dp0;%PATH%"
set "PYTHONHOME=%~dp0python-portable"
set "SIGROKDECODE_DIR=%~dp0decoders"
echo Starting LogicAnalyzer...
LogicAnalyzer.exe
if %ERRORLEVEL% neq 0 (
    echo.
    echo LogicAnalyzer exited with error code %ERRORLEVEL%
    pause
)
EOF
    echo "  run.bat"

    echo ""
    echo "===== PACKAGE COMPLETE ====="
    echo "  $DIST_DIR/"
    echo ""
    echo "To distribute: copy the entire dist/LogicAnalyzer/ folder."
    echo "To run: double-click run.bat or LogicAnalyzer.exe"
fi

