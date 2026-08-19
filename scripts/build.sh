#!/bin/sh

# Build AACGain and its bundled faad2/mp4v2 dependencies on macOS or FreeBSD.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}

die()
{
    echo "build.sh: $*" >&2
    exit 1
}

OS_NAME=$(uname -s)
case "$OS_NAME" in
    Darwin)
        MAKE_PROGRAM=$(command -v make) || die "make が見つかりません。"
        INSTALL_HINT="brew install cmake"
        ;;
    FreeBSD)
        MAKE_PROGRAM=$(command -v gmake) || die "gmake が見つかりません。pkg install gmake を実行してください。"
        INSTALL_HINT="pkg install cmake gmake git"
        ;;
    *)
        die "対応OSはmacOSとFreeBSDです: $OS_NAME"
        ;;
esac

for command_name in cmake git; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        die "'$command_name' が見つかりません。推奨コマンド: $INSTALL_HINT"
    fi
done

cd "$PROJECT_DIR"

if [ ! -f 3rdparty/faad2/CMakeLists.txt ]; then
    echo "faad2サブモジュールを取得します..."
    git submodule update --init --recursive 3rdparty/faad2
fi

[ -f 3rdparty/faad2/CMakeLists.txt ] || die "3rdparty/faad2を取得できませんでした。"

case "$OS_NAME" in
    Darwin)
        CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
        ;;
    FreeBSD)
        CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
        ;;
esac

case "$CPU_COUNT" in
    ''|*[!0-9]*) CPU_COUNT=1 ;;
esac

echo "構成: $PROJECT_DIR"
echo "ビルド: $BUILD_DIR ($BUILD_TYPE)"
echo "make: $MAKE_PROGRAM (${CPU_COUNT}並列)"

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_FLAGS_RELEASE=-std=gnu89 \
    -DCMAKE_MAKE_PROGRAM="$MAKE_PROGRAM"

cmake --build "$BUILD_DIR" -- -j"$CPU_COUNT"

echo "ビルド完了: $BUILD_DIR/aacgain/aacgain"
