#!/usr/bin/env bash
#
# BuildTokenizersLib.sh: builds libtokenizers_c.a (the Rust staticlib from tokenizers-cpp) at a
# pinned commit for Linux or Mac and installs it, plus the matching C header, into the plugin.
#
# The Linux/Mac counterpart of BuildTokenizersLib.ps1 (Win64). Same pin, same checks:
#   - clones tokenizers-cpp at a pinned tag and verifies the exact commit hash,
#   - builds the Rust crate in rust/ with cargo --locked (the tag's Cargo.lock),
#   - checks that every C API function is defined in the lib,
#   - checks that the lib needs no system library the plugin's Build.cs does not link,
#   - copies the lib to Source/ThirdParty/tokenizersLibrary/<Platform>/... and the header
#     byte-for-byte to Source/ThirdParty/tokenizersLibrary/Public/TokenizersLibrary/,
#   - writes tokenizers_c.buildinfo.json next to the lib recording what was used.
#
# Targets:
#   linux-x64    Linux/x86_64-unknown-linux-gnu/libtokenizers_c.a   (UE platform Linux)
#   linux-arm64  Linux/aarch64-unknown-linux-gnu/libtokenizers_c.a  (UE platform LinuxArm64)
#   mac          Mac/libtokenizers_c.a, universal (arm64 + x86_64)   (UE platform Mac)
#
# Requires bash 3.2+, git, cargo/rustc with the Rust target(s) installed, a C compiler for the
# target (oniguruma is C) and, on Mac, Xcode command line tools (lipo). Nothing is installed;
# a missing tool is reported and the script exits non-zero.
#
# Linux C compiler: if LINUX_MULTIARCH_ROOT is set (the Unreal Linux cross toolchain), its
# clang and sysroot are used, so the C objects match the glibc the engine links against.
# Otherwise CC (or cc) from PATH is used. --ue-toolchain overrides LINUX_MULTIARCH_ROOT.

set -euo pipefail

# ---- Pin (keep in sync with BuildTokenizersLib.ps1) -------------------------------------
REPO_URL='https://github.com/P1ayer-1/tokenizers-cpp'
REPO_TAG='v0.1.5'
REPO_COMMIT='b1dab5c2e04dc7462f5a38e9ef2c93c2f8218495'

# Every function declared in include/tokenizers_c.h at the pin.
EXPECTED_SYMBOLS='tokenizers_new_from_str
byte_level_bpe_tokenizers_new_from_str
tokenizers_encode
tokenizers_encode_batch
tokenizers_encode_batch_truncated
tokenizers_free_encode_results
tokenizers_decode
tokenizers_get_decode_str
tokenizers_get_vocab_size
tokenizers_id_to_token
tokenizers_token_to_id
tokenizers_free
tokenizers_get_last_error'

# native-static-libs entries Source/ThirdParty/tokenizersLibrary/TokenizersLibrary.Build.cs
# links (or the engine always links). Anything else fails the build: update Build.cs first.
ALLOWED_NATIVE_LINUX='-lgcc_s -lutil -lrt -lpthread -lm -ldl -lc'
ALLOWED_NATIVE_MAC='-lSystem -lc -lm -liconv -lresolv -framework CoreFoundation -framework Security'

DEFAULT_MACOS_MIN='11.0'

# -----------------------------------------------------------------------------------------
usage()
{
    cat <<'EOF'
Usage: Scripts/BuildTokenizersLib.sh [options]

  --target <t>          linux-x64, linux-arm64 or mac. Default: the host platform.
  --work-dir <dir>      Clone and build folder; must be outside the plugin folder.
                        Default: ${TMPDIR:-/tmp}/tokenizers-cpp-build
  --clean               Delete the clone (and with it the cargo target folder) first.
  --ue-toolchain <dir>  Linux only: Unreal Linux cross toolchain root (the folder holding
                        x86_64-unknown-linux-gnu/ and aarch64-unknown-linux-gnueabi/).
                        Default: $LINUX_MULTIARCH_ROOT if set, else the C compiler on PATH.
  --macos-min <ver>     Mac only: MACOSX_DEPLOYMENT_TARGET. Default: 11.0.
  -h, --help            Show this help.
EOF
}

fail()
{
    echo "BuildTokenizersLib FAILED: $*" >&2
    exit 1
}

step()
{
    echo
    echo "==> $*"
}

need_tool()
{
    command -v "$1" >/dev/null 2>&1 || fail "'$1' is not on PATH"
}

sha256_of()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

abs_path()
{
    # Absolute path of a possibly non-existing path (parent need not exist either).
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *)  printf '%s\n' "$PWD/$1" ;;
    esac
}

path_under()
{
    # 0 if $1 is $2 or inside it (both absolute).
    local Child="${1%/}/" Parent="${2%/}/"
    case "$Child" in
        "$Parent"*) return 0 ;;
        *)          return 1 ;;
    esac
}

json_escape()
{
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# ---- Arguments --------------------------------------------------------------------------
TARGET=''
WORK_DIR="${TMPDIR:-/tmp}"
WORK_DIR="${WORK_DIR%/}/tokenizers-cpp-build"
CLEAN=0
UE_TOOLCHAIN="${LINUX_MULTIARCH_ROOT:-}"
MACOS_MIN="$DEFAULT_MACOS_MIN"

while [ $# -gt 0 ]; do
    case "$1" in
        --target)       [ $# -ge 2 ] || fail "$1 needs a value"; TARGET="$2"; shift 2 ;;
        --work-dir)     [ $# -ge 2 ] || fail "$1 needs a value"; WORK_DIR="$2"; shift 2 ;;
        --clean)        CLEAN=1; shift ;;
        --ue-toolchain) [ $# -ge 2 ] || fail "$1 needs a value"; UE_TOOLCHAIN="$2"; shift 2 ;;
        --macos-min)    [ $# -ge 2 ] || fail "$1 needs a value"; MACOS_MIN="$2"; shift 2 ;;
        -h|--help)      usage; exit 0 ;;
        *)              usage >&2; fail "unknown argument '$1'" ;;
    esac
done

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"
if [ -z "$TARGET" ]; then
    case "$HOST_OS/$HOST_ARCH" in
        Linux/x86_64)                 TARGET='linux-x64' ;;
        Linux/aarch64|Linux/arm64)    TARGET='linux-arm64' ;;
        Darwin/*)                     TARGET='mac' ;;
        *)                            fail "no default --target for host $HOST_OS/$HOST_ARCH" ;;
    esac
fi

case "$TARGET" in
    linux-x64)
        RUST_TARGETS='x86_64-unknown-linux-gnu'
        PLATFORM_SUBDIR='Linux/x86_64-unknown-linux-gnu'
        UE_TOOLCHAIN_ARCH='x86_64-unknown-linux-gnu'
        ALLOWED_NATIVE="$ALLOWED_NATIVE_LINUX"
        ;;
    linux-arm64)
        RUST_TARGETS='aarch64-unknown-linux-gnu'
        PLATFORM_SUBDIR='Linux/aarch64-unknown-linux-gnu'
        UE_TOOLCHAIN_ARCH='aarch64-unknown-linux-gnueabi'
        ALLOWED_NATIVE="$ALLOWED_NATIVE_LINUX"
        ;;
    mac)
        [ "$HOST_OS" = 'Darwin' ] || fail "--target mac must be built on macOS (needs the Apple SDK and lipo)"
        RUST_TARGETS='aarch64-apple-darwin x86_64-apple-darwin'
        PLATFORM_SUBDIR='Mac'
        UE_TOOLCHAIN_ARCH=''
        ALLOWED_NATIVE="$ALLOWED_NATIVE_MAC"
        ;;
    *)
        fail "bad --target '$TARGET': must be linux-x64, linux-arm64 or mac"
        ;;
esac
case "$TARGET" in linux-*) [ "$HOST_OS" = 'Linux' ] || [ -n "$UE_TOOLCHAIN" ] || fail "--target $TARGET off Linux needs --ue-toolchain (or LINUX_MULTIARCH_ROOT)" ;; esac

# ---- Paths ------------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
PLUGIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
THIRD_PARTY_DIR="$PLUGIN_ROOT/Source/ThirdParty/tokenizersLibrary"
LIB_OUT_DIR="$THIRD_PARTY_DIR/$PLATFORM_SUBDIR"
HEADER_OUT="$THIRD_PARTY_DIR/Public/TokenizersLibrary/tokenizers_c.h"
LIB_OUT="$LIB_OUT_DIR/libtokenizers_c.a"
BUILD_INFO_OUT="$LIB_OUT_DIR/tokenizers_c.buildinfo.json"

[ -n "$WORK_DIR" ] || fail 'work dir is empty'
WORK_DIR="$(abs_path "$WORK_DIR")"
if path_under "$WORK_DIR" "$PLUGIN_ROOT"; then
    fail "work dir '$WORK_DIR' is inside the plugin folder '$PLUGIN_ROOT'; choose a folder outside the repo"
fi
CLONE_DIR="$WORK_DIR/tokenizers-cpp"
RUST_DIR="$CLONE_DIR/rust"

echo 'BuildTokenizersLib'
echo "  repo        : $REPO_URL"
echo "  tag         : $REPO_TAG"
echo "  commit      : $REPO_COMMIT"
echo "  plugin      : $PLUGIN_ROOT"
echo "  work dir    : $WORK_DIR"
echo "  target      : $TARGET ($RUST_TARGETS)"
echo "  clean       : $CLEAN"

# ---- 1. Tools ---------------------------------------------------------------------------
step 'Checking tools'
need_tool git
need_tool cargo
need_tool rustc
GIT_VERSION="$(git --version)"
CARGO_VERSION="$(cargo -V)"
RUSTC_VERSION="$(rustc -V)"
RUSTC_HOST="$(rustc -vV | sed -n 's/^host: //p')"
echo "  $GIT_VERSION"
echo "  $CARGO_VERSION"
echo "  $RUSTC_VERSION"

INSTALLED_TARGETS=''
if command -v rustup >/dev/null 2>&1; then
    INSTALLED_TARGETS="$(rustup target list --installed)"
fi
for T in $RUST_TARGETS; do
    if ! grep -qxF -- "$T" <<<"$INSTALLED_TARGETS" && [ "$RUSTC_HOST" != "$T" ]; then
        fail "Rust target $T is not installed (rustup target add $T)"
    fi
    echo "  rust target $T installed"
done

if [ "$TARGET" = 'mac' ]; then
    need_tool lipo
    need_tool nm
    need_tool xcrun
    NM='nm'
else
    if command -v llvm-nm >/dev/null 2>&1; then NM='llvm-nm'; else need_tool nm; NM='nm'; fi
fi

# ---- 2. Clone / fetch, check out the pinned tag ----------------------------------------
if [ "$CLEAN" = 1 ] && [ -e "$CLONE_DIR" ]; then
    step "Clean: deleting $CLONE_DIR"
    rm -rf -- "$CLONE_DIR"
    [ ! -e "$CLONE_DIR" ] || fail "could not delete $CLONE_DIR"
fi
mkdir -p -- "$WORK_DIR"

if [ -d "$CLONE_DIR/.git" ]; then
    step "Reusing clone $CLONE_DIR; fetching tags"
    git -C "$CLONE_DIR" config core.autocrlf false
    git -C "$CLONE_DIR" -c core.autocrlf=false fetch --tags --force "$REPO_URL"
else
    [ ! -e "$CLONE_DIR" ] || fail "$CLONE_DIR exists but is not a git clone; delete it or use --clean"
    step "Cloning $REPO_URL into $CLONE_DIR"
    git -c core.autocrlf=false clone --no-checkout "$REPO_URL" "$CLONE_DIR"
    git -C "$CLONE_DIR" config core.autocrlf false
fi

step "Checking out tag $REPO_TAG (detached, no submodules)"
git -C "$CLONE_DIR" -c core.autocrlf=false -c advice.detachedHead=false checkout --force --detach "refs/tags/$REPO_TAG"
HEAD_COMMIT="$(git -C "$CLONE_DIR" rev-parse HEAD)"
echo "  HEAD = $HEAD_COMMIT"
[ "$HEAD_COMMIT" = "$REPO_COMMIT" ] || fail "commit mismatch: tag $REPO_TAG is at $HEAD_COMMIT, expected $REPO_COMMIT"
echo '  HEAD matches pinned commit'

# ---- 3. Lockfile ------------------------------------------------------------------------
step 'Checking rust/Cargo.lock'
LOCK_FILE="$RUST_DIR/Cargo.lock"
[ -f "$LOCK_FILE" ] || fail "rust/Cargo.lock is missing at $REPO_TAG; cannot build with --locked"
TOKENIZERS_CRATE="$(awk '/^name = "tokenizers"$/ { getline; if ($0 ~ /^version = /) { gsub(/^version = "|"$/, ""); print; exit } }' "$LOCK_FILE")"
[ -n "$TOKENIZERS_CRATE" ] || fail 'tokenizers crate not found in rust/Cargo.lock'
echo "  tokenizers crate (Cargo.lock) = $TOKENIZERS_CRATE"

# ---- 4. C toolchain ---------------------------------------------------------------------
step 'Setting up the C toolchain'
C_TOOLCHAIN=''
if [ "$TARGET" = 'mac' ]; then
    export MACOSX_DEPLOYMENT_TARGET="$MACOS_MIN"
    SDK_PATH="$(xcrun --sdk macosx --show-sdk-path)" || fail 'xcrun could not find the macOS SDK (install the Xcode command line tools)'
    SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"
    C_TOOLCHAIN="$(xcrun clang --version | head -n 1); macOS SDK $SDK_VERSION; MACOSX_DEPLOYMENT_TARGET=$MACOS_MIN"
    echo "  MACOSX_DEPLOYMENT_TARGET = $MACOS_MIN"
    echo "  SDK = $SDK_PATH ($SDK_VERSION)"
elif [ -n "$UE_TOOLCHAIN" ]; then
    UE_ARCH_ROOT="${UE_TOOLCHAIN%/}/$UE_TOOLCHAIN_ARCH"
    UE_CLANG="$UE_ARCH_ROOT/bin/clang"
    [ -x "$UE_CLANG" ] || fail "Unreal toolchain clang not found at $UE_CLANG"
    # cc-rs reads CC_<target> and CFLAGS_<target> (target triple with '-' as '_').
    RT="$(printf '%s' "$RUST_TARGETS" | tr '-' '_')"
    export "CC_$RT=$UE_CLANG"
    export "AR_$RT=$UE_ARCH_ROOT/bin/llvm-ar"
    export "CFLAGS_$RT=--target=$UE_TOOLCHAIN_ARCH --sysroot=$UE_ARCH_ROOT -fPIC"
    [ -x "$UE_ARCH_ROOT/bin/llvm-ar" ] || fail "llvm-ar not found at $UE_ARCH_ROOT/bin/llvm-ar"
    C_TOOLCHAIN="$("$UE_CLANG" --version | head -n 1); sysroot $UE_ARCH_ROOT"
    echo "  Unreal toolchain: $UE_ARCH_ROOT"
else
    CC_BIN="${CC:-cc}"
    need_tool "$CC_BIN"
    if [ "$TARGET" = 'linux-arm64' ] && [ "$HOST_ARCH" != 'aarch64' ] && [ "$HOST_ARCH" != 'arm64' ] && [ -z "${CC_aarch64_unknown_linux_gnu:-}" ]; then
        fail 'cross-building linux-arm64 needs --ue-toolchain (or CC_aarch64_unknown_linux_gnu)'
    fi
    C_TOOLCHAIN="$("$CC_BIN" --version | head -n 1); host sysroot"
    echo '  No Unreal toolchain (LINUX_MULTIARCH_ROOT / --ue-toolchain); using the host C compiler.'
    echo '  The C objects are compiled against the host glibc headers; the engine links glibc 2.17.'
fi
echo "  $C_TOOLCHAIN"

# ---- 5. cargo build ---------------------------------------------------------------------
NATIVE_STATIC_LIBS=''
BUILT_LIBS=''
for T in $RUST_TARGETS; do
    step "Building rust staticlib for $T (cargo rustc --release --locked)"
    # One cargo target dir per Rust target and C toolchain: cc-rs (onig_sys) does not rebuild
    # when the C compiler changes, so a shared dir could keep objects from another toolchain.
    TC_KEY="$(printf '%s' "$C_TOOLCHAIN" | cksum | awk '{print $1}')"
    TARGET_DIR_REL="target/$T-$TC_KEY"
    LOG="$WORK_DIR/cargo-$T.log"
    echo "  (in $RUST_DIR)"
    echo "  cargo rustc --release --locked --lib --target $T --target-dir $TARGET_DIR_REL -- --print native-static-libs"
    if ! (cd "$RUST_DIR" && cargo rustc --release --locked --lib --target "$T" --target-dir "$TARGET_DIR_REL" -- --print native-static-libs) >"$LOG" 2>&1; then
        sed 's/^/    /' "$LOG"
        fail "cargo rustc for $T (log: $LOG)"
    fi
    sed 's/^/    /' "$LOG"
    LINE="$(grep 'native-static-libs:' "$LOG" | tail -n 1 || true)"
    [ -n "$LINE" ] || fail "cargo output for $T has no native-static-libs line"
    NATIVE="$(printf '%s' "$LINE" | sed -e 's/^.*native-static-libs:[[:space:]]*//' -e 's/[[:space:]]*$//')"
    echo "  native-static-libs: $NATIVE"

    # Every entry must be one Build.cs links. "-framework X" is one entry.
    set -- $NATIVE
    while [ $# -gt 0 ]; do
        ENTRY="$1"
        if [ "$1" = '-framework' ] && [ $# -ge 2 ]; then ENTRY="$1 $2"; shift; fi
        shift
        case " $ALLOWED_NATIVE " in
            *" $ENTRY "*) ;;
            *) fail "native-static-libs for $T has '$ENTRY', which TokenizersLibrary.Build.cs does not link; add it there and to this script's allow list" ;;
        esac
    done
    if [ -z "$NATIVE_STATIC_LIBS" ]; then NATIVE_STATIC_LIBS="$NATIVE"; fi

    BUILT="$RUST_DIR/$TARGET_DIR_REL/$T/release/libtokenizers_c.a"
    [ -f "$BUILT" ] || fail "cargo did not produce $BUILT"

    # ---- 6. Symbols (per architecture) --------------------------------------------------
    step "Checking defined C API symbols for $T ($NM)"
    if [ "$TARGET" = 'mac' ]; then
        # Mach-O prefixes C symbols with '_'.
        DEFINED="$("$NM" -gU "$BUILT" 2>/dev/null | awk 'NF >= 3 { s = $NF; sub(/^_/, "", s); print s }')"
    else
        DEFINED="$("$NM" -g --defined-only "$BUILT" 2>/dev/null | awk 'NF >= 3 { print $NF }')"
    fi
    MISSING=''
    for S in $EXPECTED_SYMBOLS; do
        grep -qxF -- "$S" <<<"$DEFINED" || MISSING="$MISSING $S"
    done
    [ -z "$MISSING" ] || fail "missing symbols in $BUILT:$MISSING"
    echo "  all $(printf '%s\n' "$EXPECTED_SYMBOLS" | wc -l | tr -d ' ') C API symbols found"

    if [ "$TARGET" != 'mac' ]; then
        # glibc 2.38+ headers redirect strtol & co. to __isoc23_* in the C objects; those symbols
        # do not exist in the older glibc the engine links against (UE: 2.17).
        ISOC23="$("$NM" -u "$BUILT" 2>/dev/null | awk '$NF ~ /^__isoc23_/ { print $NF }' | sort -u | tr '\n' ' ')"
        [ -z "$ISOC23" ] || fail "the C objects need glibc 2.38+ (${ISOC23% }); build with --ue-toolchain (or LINUX_MULTIARCH_ROOT)"
        echo '  no glibc 2.38+ only symbols (__isoc23_*)'
    fi

    BUILT_LIBS="$BUILT_LIBS $BUILT"
done

# ---- 7. Install -------------------------------------------------------------------------
step 'Installing lib, header and build info into the plugin'
# Remove the old build info first, so a failure below never leaves a stale record beside a
# different lib. The lib is copied last, after the header check, then the build info written.
rm -f -- "$BUILD_INFO_OUT"
[ ! -e "$BUILD_INFO_OUT" ] || fail "could not delete $BUILD_INFO_OUT"
HEADER_SRC="$CLONE_DIR/include/tokenizers_c.h"
[ -f "$HEADER_SRC" ] || fail "header not found at $HEADER_SRC"
mkdir -p -- "$LIB_OUT_DIR"
cp -f -- "$HEADER_SRC" "$HEADER_OUT"
[ "$(sha256_of "$HEADER_SRC")" = "$(sha256_of "$HEADER_OUT")" ] || fail 'installed header differs from the pinned header'

TMP_LIB="$LIB_OUT.tmp.$$"
if [ "$TARGET" = 'mac' ]; then
    # shellcheck disable=SC2086
    lipo -create $BUILT_LIBS -output "$TMP_LIB" || fail 'lipo -create failed'
    ARCHS="$(lipo -archs "$TMP_LIB")"
    case " $ARCHS " in *" arm64 "*) ;; *) rm -f "$TMP_LIB"; fail "universal lib lacks arm64 (has: $ARCHS)" ;; esac
    case " $ARCHS " in *" x86_64 "*) ;; *) rm -f "$TMP_LIB"; fail "universal lib lacks x86_64 (has: $ARCHS)" ;; esac
    echo "  universal lib archs: $ARCHS"
else
    cp -f -- $BUILT_LIBS "$TMP_LIB"
fi
mv -f -- "$TMP_LIB" "$LIB_OUT"

LIB_SHA="$(sha256_of "$LIB_OUT")"
LIB_SIZE="$(wc -c <"$LIB_OUT" | tr -d ' ')"
BUILT_UTC="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

{
    echo '{'
    echo "    \"repo\":  \"$(json_escape "$REPO_URL")\","
    echo "    \"tag\":  \"$(json_escape "$REPO_TAG")\","
    echo "    \"commit\":  \"$(json_escape "$REPO_COMMIT")\","
    echo "    \"rustc\":  \"$(json_escape "$RUSTC_VERSION")\","
    echo "    \"cargo_target\":  \"$(json_escape "$RUST_TARGETS")\","
    echo "    \"c_toolchain\":  \"$(json_escape "$C_TOOLCHAIN")\","
    echo "    \"tokenizers_crate\":  \"$(json_escape "$TOKENIZERS_CRATE")\","
    echo "    \"native_static_libs\":  \"$(json_escape "$NATIVE_STATIC_LIBS")\","
    echo "    \"lib_sha256\":  \"$LIB_SHA\","
    echo "    \"built_utc\":  \"$BUILT_UTC\""
    echo '}'
} >"$BUILD_INFO_OUT"

step 'Summary'
echo "  commit           : $HEAD_COMMIT ($REPO_TAG)"
echo "  tokenizers crate : $TOKENIZERS_CRATE"
echo "  rustc            : $RUSTC_VERSION"
echo "  rust target(s)   : $RUST_TARGETS"
echo "  c toolchain      : $C_TOOLCHAIN"
echo "  lib              : $LIB_OUT"
echo "  lib size         : $LIB_SIZE bytes"
echo "  lib sha256       : $LIB_SHA"
echo "  header           : $HEADER_OUT"
echo "  build info       : $BUILD_INFO_OUT"
echo 'BuildTokenizersLib OK'
