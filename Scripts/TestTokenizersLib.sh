#!/usr/bin/env bash
# shellcheck disable=SC2054  # the commas in -Wl,... linker flags are intended
#
# TestTokenizersLib.sh: engine-free smoke test of the installed Linux or Mac native library.
#
# Builds Tests/Native/TokenizersLibSmoke.cpp into a shared module (.so / .dylib; -fPIC, hidden
# visibility, no undefined symbols allowed), linked against the installed libtokenizers_c.a
# with exactly the system libraries and frameworks TokenizersLibrary.Build.cs lists for the
# platform, then runs it on Content/tokenizer.json. So it checks the lib, the header and the
# Build.cs link list together, the way Unreal links the Tokenizers module.
#
# Run Scripts/BuildTokenizersLib.sh first.
#
# Environment:
#   CXX     C++ compiler/driver (default: c++). May include arguments, e.g. "zig c++ -target ...".
#   LDFLAGS Extra link arguments for the smoke module, e.g. "-L<dir>" for a library the compiler
#           cannot find on its own.
#   RUNNER  Prefix for running the test binary, e.g. "qemu-aarch64-static -L /usr/aarch64-linux-gnu".

set -euo pipefail

usage()
{
    cat <<'EOF'
Usage: Scripts/TestTokenizersLib.sh [options]

  --target <t>     linux-x64, linux-arm64 or mac. Default: the host platform.
  --arch <a>       Mac only: arm64 or x86_64, the slice of the universal lib to test.
                   Default: the host architecture.
  --out-dir <dir>  Where the test binaries go. Default: ${TMPDIR:-/tmp}/tokenizers-lib-smoke
  --link-only      Build and link, but do not run (e.g. when cross-compiling).
  -h, --help       Show this help.
EOF
}

fail()
{
    echo "TestTokenizersLib FAILED: $*" >&2
    exit 1
}

TARGET=''
ARCH=''
OUT_DIR="${TMPDIR:-/tmp}"
OUT_DIR="${OUT_DIR%/}/tokenizers-lib-smoke"
LINK_ONLY=0

while [ $# -gt 0 ]; do
    case "$1" in
        --target)    [ $# -ge 2 ] || fail "$1 needs a value"; TARGET="$2"; shift 2 ;;
        --arch)      [ $# -ge 2 ] || fail "$1 needs a value"; ARCH="$2"; shift 2 ;;
        --out-dir)   [ $# -ge 2 ] || fail "$1 needs a value"; OUT_DIR="$2"; shift 2 ;;
        --link-only) LINK_ONLY=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *)           usage >&2; fail "unknown argument '$1'" ;;
    esac
done

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
if [ -z "$TARGET" ]; then
    case "$HOST_OS/$HOST_ARCH" in
        Linux/x86_64)              TARGET='linux-x64' ;;
        Linux/aarch64|Linux/arm64) TARGET='linux-arm64' ;;
        Darwin/*)                  TARGET='mac' ;;
        *)                         fail "no default --target for host $HOST_OS/$HOST_ARCH" ;;
    esac
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PLUGIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
THIRD_PARTY_DIR="$PLUGIN_ROOT/Source/ThirdParty/tokenizersLibrary"
BUILD_CS="$THIRD_PARTY_DIR/TokenizersLibrary.Build.cs"
SMOKE_SRC="$PLUGIN_ROOT/Tests/Native/TokenizersLibSmoke.cpp"
MAIN_SRC="$PLUGIN_ROOT/Tests/Native/SmokeMain.cpp"
TOKENIZER_JSON="$PLUGIN_ROOT/Content/tokenizer.json"

case "$TARGET" in
    linux-x64)   LIB="$THIRD_PARTY_DIR/Linux/x86_64-unknown-linux-gnu/libtokenizers_c.a"; BLOCK='UnrealTargetPlatform.Linux ||' ;;
    linux-arm64) LIB="$THIRD_PARTY_DIR/Linux/aarch64-unknown-linux-gnu/libtokenizers_c.a"; BLOCK='UnrealTargetPlatform.Linux ||' ;;
    mac)         LIB="$THIRD_PARTY_DIR/Mac/libtokenizers_c.a"; BLOCK='UnrealTargetPlatform.Mac)' ;;
    *)           fail "bad --target '$TARGET': must be linux-x64, linux-arm64 or mac" ;;
esac
[ -f "$LIB" ] || fail "$LIB not found; run Scripts/BuildTokenizersLib.sh --target $TARGET first"

# The platform's link list, read from Build.cs: the lines of its if-block up to the closing brace.
BLOCK_TEXT="$(awk -v Key="$BLOCK" 'index($0, Key) { In = 1; next } In && /^\t\t}/ { exit } In { print }' "$BUILD_CS")"
[ -n "$BLOCK_TEXT" ] || fail "no '$BLOCK' block in $BUILD_CS"
SYSTEM_LIBS="$(printf '%s\n' "$BLOCK_TEXT" | grep 'PublicSystemLibraries' | grep -o '"[^"]*"' | tr -d '"' | tr '\n' ' ' || true)"
FRAMEWORKS="$(printf '%s\n' "$BLOCK_TEXT" | grep 'PublicFrameworks' | grep -o '"[^"]*"' | tr -d '"' | tr '\n' ' ' || true)"
LINK_ARGS=()
for L in $SYSTEM_LIBS; do LINK_ARGS+=("-l$L"); done
for F in $FRAMEWORKS; do LINK_ARGS+=(-framework "$F"); done

read -r -a CXX_CMD <<<"${CXX:-c++}"
read -r -a RUN_CMD <<<"${RUNNER:-}"
read -r -a EXTRA_LDFLAGS <<<"${LDFLAGS:-}"

COMMON=(-std=c++17 -O1 -g0 -I"$THIRD_PARTY_DIR/Public")
if [ "$TARGET" = 'mac' ]; then
    if [ -z "$ARCH" ]; then
        case "$HOST_ARCH" in arm64|aarch64) ARCH='arm64' ;; *) ARCH='x86_64' ;; esac
    fi
    case "$ARCH" in arm64|x86_64) ;; *) fail "bad --arch '$ARCH': must be arm64 or x86_64" ;; esac
    if command -v lipo >/dev/null 2>&1; then
        ARCHS="$(lipo -archs "$LIB")"
        case " $ARCHS " in *" arm64 "*) ;; *) fail "$LIB lacks arm64 (has: $ARCHS)" ;; esac
        case " $ARCHS " in *" x86_64 "*) ;; *) fail "$LIB lacks x86_64 (has: $ARCHS)" ;; esac
        echo "  universal lib archs: $ARCHS"
    fi
    # zig's driver selects the arch with -target, Apple clang with -arch.
    case "${CXX_CMD[*]}" in *zig*) ;; *) COMMON+=(-arch "$ARCH") ;; esac
    MODULE="$OUT_DIR/libTokenizersLibSmoke.dylib"
    MODULE_ARGS=(-dynamiclib -fPIC -fvisibility=hidden -Wl,-undefined,error -Wl,-install_name,@rpath/libTokenizersLibSmoke.dylib)
    RPATH_ARGS=(-Wl,-rpath,@executable_path)
else
    MODULE="$OUT_DIR/libTokenizersLibSmoke.so"
    MODULE_ARGS=(-shared -fPIC -fvisibility=hidden -Wl,--no-undefined)
    RPATH_ARGS=(-Wl,-rpath,'$ORIGIN')
fi
EXE="$OUT_DIR/TokenizersLibSmoke"

echo 'TestTokenizersLib'
echo "  target     : $TARGET${ARCH:+ ($ARCH)}"
echo "  lib        : $LIB"
echo "  compiler   : ${CXX_CMD[*]}"
echo "  link (from Build.cs): ${LINK_ARGS[*]:-(none)}"

rm -rf -- "$OUT_DIR"
mkdir -p -- "$OUT_DIR"

echo
echo '==> Linking the smoke module (shared, no undefined symbols)'
"${CXX_CMD[@]}" "${COMMON[@]}" "${MODULE_ARGS[@]}" "$SMOKE_SRC" "$LIB" ${EXTRA_LDFLAGS[@]+"${EXTRA_LDFLAGS[@]}"} ${LINK_ARGS[@]+"${LINK_ARGS[@]}"} -o "$MODULE" \
    || fail 'linking the smoke module failed (a missing symbol means Build.cs lacks a system library)'
echo '==> Linking the test executable'
"${CXX_CMD[@]}" "${COMMON[@]}" "$MAIN_SRC" -L"$OUT_DIR" -lTokenizersLibSmoke "${RPATH_ARGS[@]}" -o "$EXE" \
    || fail 'linking the test executable failed'
echo "  $EXE"

if [ "$LINK_ONLY" = 1 ]; then
    echo 'TestTokenizersLib OK (link only, not run)'
    exit 0
fi

echo
echo "==> Running ${RUN_CMD[*]:+${RUN_CMD[*]} }$EXE"
if [ "$TARGET" = 'mac' ] && [ "$ARCH" != "$HOST_ARCH" ] && [ ${#RUN_CMD[@]} -eq 0 ]; then
    RUN_CMD=(arch "-$ARCH")
fi
${RUN_CMD[@]+"${RUN_CMD[@]}"} "$EXE" "$TOKENIZER_JSON" || fail 'smoke test failed'
echo 'TestTokenizersLib OK'
