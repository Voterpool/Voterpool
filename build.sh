#!/usr/bin/env bash
#
# Voterpool - universal build script for Linux distributions.
# Detects the system (distro, package manager, compiler) and adapts to
# user input (interactive prompts plus flags for CI environments).
#
#   ./build.sh                     # interactive build, everything auto-detected
#   ./build.sh --yes               # non-interactive (CI); auto-select defaults
#   ./build.sh --vcpkg             # force the vcpkg manifest mode
#   ./build.sh --system-deps       # system packages only
#   ./build.sh --tests --run-tests # build and run tests
#   ./build.sh --debug --jobs 4    # debug build with 4 jobs
#   ./build.sh --clean             # remove the build directory first
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS_PREFIX="$ROOT_DIR/.deps"
BUILD_DIR="$ROOT_DIR/build"

BUILD_TYPE="Release"
JOBS="$(nproc 2>/dev/null || echo 4)"
INSTALL_DEPS="auto"     # auto | yes | no
USE_VCPKG="auto"        # auto | yes | no
VCPKG_DIR="${VCPKG_ROOT:-}"
WITH_TESTS="auto"       # auto | yes | no  (auto = ON)
RUN_TESTS=0
ASSUME_YES=0
NONINTERACTIVE=0
DO_CLEAN=0

DROGON_TAG="v1.9.6"
TRANTOR_COMMIT="79e1ffc59aa1b9c8226c2988fe2e1185e97cd795"

if [ -t 1 ]; then
  C_G="\033[1;32m"; C_Y="\033[1;33m"; C_R="\033[1;31m"; C_B="\033[1;36m"; C_0="\033[0m"
else
  C_G=""; C_Y=""; C_R=""; C_B=""; C_0=""
fi
log()  { printf "%b\n" "${C_B}[voterpool]${C_0} $*"; }
ok()   { printf "%b\n" "${C_G}[  ok  ]${C_0} $*"; }
warn() { printf "%b\n" "${C_Y}[ warn ]${C_0} $*"; }
err()  { printf "%b\n" "${C_R}[ fail ]${C_0} $*" >&2; }

usage() {
  cat <<'EOF'
Voterpool build script

Usage: ./build.sh [options]
  --debug                 Debug build (default: Release)
  --jobs N                Parallel build jobs (default: nproc)
  --tests | --no-tests    Build tests or not (default: yes)
  --run-tests             Run ctest after the build
  --system-deps           Install dependencies via the system package manager
  --vcpkg [PATH]          Build dependencies via vcpkg (manifest mode);
                          PATH is the vcpkg directory (otherwise $VCPKG_ROOT or a fresh clone)
  --install-deps yes|no|auto  Control package installation (default: auto)
  --clean                 Remove the build directory first
  -y, --yes               Non-interactive mode (CI); answer yes to everything
  -h, --help              This help
EOF
}

suggest_option() {
  local cand="${1#-}"; cand="${cand#-}"
  case "$cand" in
    debug|jobs|tests|no-tests|run-tests|system-deps|vcpkg|install-deps|clean|yes|help)
      printf " (did you mean --%s?)" "$cand" ;;
  esac
}

while [ $# -gt 0 ]; do
  case "$1" in
    --debug) BUILD_TYPE="Debug" ;;
    --jobs) JOBS="$2"; shift ;;
    --tests) WITH_TESTS="yes" ;;
    --no-tests) WITH_TESTS="no" ;;
    --run-tests) RUN_TESTS=1 ;;
    --system-deps) USE_VCPKG="no"; INSTALL_DEPS="yes" ;;
    --vcpkg)
      USE_VCPKG="yes"
      if [ "${2:-}" != "" ] && [ "${2:0:1}" != "-" ]; then VCPKG_DIR="$2"; shift; fi
      ;;
    --install-deps) INSTALL_DEPS="${2:?need value}"; shift ;;
    --clean) DO_CLEAN=1 ;;
    -y|--yes) ASSUME_YES=1; NONINTERACTIVE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) err "Unknown option: $1$(suggest_option "$1")"; usage; exit 1 ;;
  esac
  shift
done

ask() {
  local q="$1" def="${2:-Y}" ans
  if [ "$NONINTERACTIVE" = "1" ]; then REPLY="$def"; return 0; fi
  if ! read -r -p "$q [$def]: " ans </dev/tty; then
    err "Interactive input unavailable (no terminal); rerun with --yes."
    exit 1
  fi
  REPLY="${ans:-$def}"
  return 0
}

confirm() { local def="$2"; ask "$1" "$def"; [[ "$REPLY" =~ ^([Yy]) ]]; }

have() { command -v "$1" >/dev/null 2>&1; }
sudo_cmd() { if [ "$(id -u)" -eq 0 ]; then echo ""; elif have sudo; then echo sudo; fi; }

detect_pm() {
  for pm in apt-get dnf yum pacman zypper apk; do
    have "$pm" && { echo "$pm"; return 0; }
  done
  echo ""
}

OS_ID="unknown"; OS_VER=""
if [ -r /etc/os-release ]; then
  . /etc/os-release
  OS_ID="${ID:-unknown}"; OS_VER="${VERSION_ID:-}"
fi

PM="$(detect_pm)"

distro_min_version_met() {
  case "$OS_ID" in
    debian) local min=12 ;;
    ubuntu) local min=22 ;;
    *) return 0 ;;
  esac
  local major="${OS_VER%%.*}"
  case "$major" in ''|*[!0-9]*) return 0 ;; esac
  [ "$major" -ge "$min" ]
}

cmake_major_ok() {
  have cmake || return 1
  local v; v="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)"
  awk -v v="$v" 'BEGIN{ exit !(v >= 3.20) }'
}

cxx20_probe() {
  local comp="$1" src="/tmp/vp_cxx20_probe.cpp" out="/tmp/vp_cxx20_probe.o"
  cat > "$src" <<'CPP'
#include <coroutine>
#include <version>
int main() { return 0; }
CPP
  "$comp" -std=c++20 -fsyntax-only "$src" >/dev/null 2>&1
}

pick_compiler() {
  for c in g++ clang++; do
    if have "$c" && cxx20_probe "$c"; then echo "$c"; return 0; fi
  done
  echo ""
}

DEBIAN_PKGS=(
  build-essential git curl pkg-config ninja-build
  cmake libssl-dev librocksdb-dev libyaml-cpp-dev libspdlog-dev
  libsimdjson-dev libgtest-dev libjemalloc-dev libjsoncpp-dev
  zlib1g-dev libbz2-dev liblz4-dev libzstd-dev libsnappy-dev
  libcurl4-openssl-dev uuid-dev
)
FEDORA_PKGS=(
  gcc-c++ git curl pkgconf-pkg-config ninja-build
  cmake openssl-devel rocksdb-devel yaml-cpp-devel spdlog-devel
  simdjson-devel gtest-devel jemalloc-devel jsoncpp-devel
  zlib-devel bzip2-devel lz4-devel libzstd-devel snappy-devel
  libcurl-devel libuuid-devel fmt-devel
)
ARCH_PKGS=(
  base-devel git curl pkgconf ninja
  cmake openssl rocksdb yaml-cpp spdlog simdjson gtest jemalloc
  jsoncpp zstd lz4 bzip2 snappy curl fmt
)
SUSE_PKGS=(
  gcc-c++ git curl pkgconf-pkg-config ninja
  cmake libopenssl-devel rocksdb-devel yaml-cpp-devel spdlog-devel
  simdjson-devel gtest-devel jemalloc-devel jsoncpp-devel
  zlib-devel libbz2-devel liblz4-devel libzstd-devel snappy-devel
  libcurl-devel
)
ALPINE_PKGS=(
  build-base git curl pkgconf ninja
  cmake linux-headers openssl-dev rocksdb-dev yaml-cpp-dev spdlog-dev
  simdjson-dev gtest-dev jemalloc-dev jsoncpp-dev
  zlib-dev bzip2-dev lz4-dev zstd-dev snappy-dev curl-dev
)

pkgs_for_pm() {
  case "$1" in
    apt-get) echo "${DEBIAN_PKGS[*]}" ;;
    dnf|yum) echo "${FEDORA_PKGS[*]}" ;;
    pacman) echo "${ARCH_PKGS[*]}" ;;
    zypper) echo "${SUSE_PKGS[*]}" ;;
    apk) echo "${ALPINE_PKGS[*]}" ;;
    *) echo "" ;;
  esac
}

install_system_packages() {
  local pkgs; pkgs="$(pkgs_for_pm "$PM")"
  if [ -z "$pkgs" ]; then
    warn "Unknown distribution ($OS_ID): skipping automatic package installation."
    return 1
  fi
  log "Distribution: $OS_ID ${OS_VER} (${PM}). Installing dependencies..."
  local s; s="$(sudo_cmd)"
  case "$PM" in
    apt-get)  $s apt-get update -y && $s env DEBIAN_FRONTEND=noninteractive apt-get install -y $pkgs ;;
    dnf)      $s dnf install -y $pkgs ;;
    yum)      $s yum install -y $pkgs ;;
    pacman)   $s pacman -Sy --needed --noconfirm $pkgs ;;
    zypper)   $s zypper --non-interactive install $pkgs ;;
    apk)      $s apk add $pkgs ;;
  esac
}

drogon_available() {
  have pkg-config && pkg-config --exists drogon && return 0
  for d in /usr/local/lib/cmake/Drogon /usr/lib/cmake/Drogon \
           /usr/lib/x86_64-linux-gnu/cmake/Drogon \
           /usr/lib/aarch64-linux-gnu/cmake/Drogon; do
    [ -d "$d" ] && return 0
  done
  return 1
}

build_drogon_from_source() {
  log "Building Drogon ${DROGON_TAG} from source into .deps ..."
  mkdir -p "$DEPS_PREFIX/src"
  if [ ! -d "$DEPS_PREFIX/src/drogon" ]; then
    git clone --depth 1 --branch "$DROGON_TAG" https://github.com/drogonframework/drogon \
      "$DEPS_PREFIX/src/drogon"
  fi
  if ! [ -f "$DEPS_PREFIX/src/drogon/trantor/CMakeLists.txt" ]; then
    rm -rf "$DEPS_PREFIX/src/drogon/trantor"
    git clone https://github.com/an-tao/trantor "$DEPS_PREFIX/src/drogon/trantor"
    git -C "$DEPS_PREFIX/src/drogon/trantor" fetch --depth 1 origin "$TRANTOR_COMMIT"
    git -C "$DEPS_PREFIX/src/drogon/trantor" checkout "$TRANTOR_COMMIT"
  fi
  cmake -S "$DEPS_PREFIX/src/drogon" -B "$DEPS_PREFIX/src/drogon/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
        -DBUILD_BROTLI=OFF -DBUILD_ORM=OFF -DCMAKE_INSTALL_PREFIX="$DEPS_PREFIX"
  cmake --build "$DEPS_PREFIX/src/drogon/build" -j "$JOBS"
  cmake --install "$DEPS_PREFIX/src/drogon/build"
  ok "Drogon installed into $DEPS_PREFIX"
}

ensure_vcpkg() {
  if [ -z "$VCPKG_DIR" ]; then
    VCPKG_DIR="$ROOT_DIR/vcpkg"
  fi
  if [ ! -d "$VCPKG_DIR" ]; then
    log "Cloning vcpkg into $VCPKG_DIR ..."
    git clone https://github.com/microsoft/vcpkg "$VCPKG_DIR"
  fi
  if [ ! -x "$VCPKG_DIR/vcpkg" ]; then
    log "Bootstrapping vcpkg (this may take a few minutes)..."
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

main() {
  printf "%b\n" "${C_B}"
  cat <<'BANNER'
                                                               
 m    m          m                                       ""#   
 "m  m"  mmm   mm#mm   mmm    m mm  mmmm    mmm    mmm     #   
  #  #  #" "#    #    #"  #   #"  " #" "#  #" "#  #" "#    #   
  "mm"  #   #    #    #""""   #     #   #  #   #  #   #    #   
   ##   "#m#"    "mm  "#mm"   #     ##m#"  "#m#"  "#m#"    "mm 
                                    #                          
                                    "                          
BANNER
  printf "%b\n" "${C_0}"

  log "Project: $ROOT_DIR"
  log "Build type: $BUILD_TYPE, jobs: $JOBS"

  [ "$DO_CLEAN" = "1" ] && rm -rf "$BUILD_DIR" && ok "Build directory removed."

  local compiler; compiler="$(pick_compiler)"
  if [ -z "$compiler" ]; then
    warn "No C++20-capable compiler (coroutines) found."
    if [ "$INSTALL_DEPS" = "no" ]; then
      err "Install g++ (>=11) or clang++ (>=14) manually."; exit 1
    fi
    if ! distro_min_version_met; then
      err "$OS_ID ${OS_VER:-unknown}: distribution repositories cannot provide a C++20 toolchain."
      err "Use Debian >= 12 or Ubuntu >= 22.04, or install g++ (>=11) / clang++ (>=14) manually."
      exit 1
    fi
    confirm "Install the toolchain via $PM?" "Y" || { err "Aborted."; exit 1; }
    install_system_packages || true
    compiler="$(pick_compiler)"
    [ -n "$compiler" ] || { err "No C++20 compiler available even after installation."; exit 1; }
  fi
  ok "Compiler: $compiler (C++20 coroutines available)"

  if ! cmake_major_ok; then
    if [ "$INSTALL_DEPS" = "no" ]; then
      err "cmake >= 3.20 is required."; exit 1
    fi
    if ! distro_min_version_met; then
      err "$OS_ID ${OS_VER:-unknown}: distribution repositories cannot provide cmake >= 3.20."
      err "Use Debian >= 12 or Ubuntu >= 22.04."
      exit 1
    fi
    confirm "Upgrade cmake via $PM?" "Y" || { err "Aborted."; exit 1; }
    install_system_packages || true
    cmake_major_ok || { err "cmake < 3.20 after installation."; exit 1; }
  fi
  ok "cmake: $(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')"

  if [ "$USE_VCPKG" = "auto" ]; then
    if [ -n "$VCPKG_DIR" ]; then
      USE_VCPKG="yes"
    elif [ "$PM" = "unknown" ] || [ "$OS_ID" = "unknown" ]; then
      USE_VCPKG="yes"
    else
      USE_VCPKG="no"
    fi
  fi

  if [ "$USE_VCPKG" = "yes" ]; then
    ensure_vcpkg
    ok "Dependencies: vcpkg manifest ($VCPKG_DIR)"
  else
    if [ "$INSTALL_DEPS" = "auto" ]; then
      if [ -t 0 ]; then
        confirm "Install system dependencies via $PM?" "Y" && INSTALL_DEPS="yes" || INSTALL_DEPS="no"
      else
        INSTALL_DEPS="yes"
      fi
    fi
    if [ "$INSTALL_DEPS" = "yes" ]; then
      install_system_packages || warn "Some packages may have failed to install - check the output above."
    fi

    if ! drogon_available; then
      log "Drogon is not available as a system package."
      case "$OS_ID" in
        debian|ubuntu|linuxmint|raspbian)
          warn "No Drogon package exists for $OS_ID; it will be built from source." ;;
      esac
      build_drogon_from_source
    else
      ok "System Drogon detected."
    fi
  fi

  local tests_flag="OFF"
  [ "$WITH_TESTS" != "no" ] && tests_flag="ON"

  local cmake_args=(-S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja
                    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
                    -DVOTERPOOL_BUILD_TESTS="$tests_flag")
  if [ "$USE_VCPKG" = "yes" ]; then
    cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake")
  elif [ -d "$DEPS_PREFIX" ] && drogon_installed_in_deps; then
    cmake_args+=(-DCMAKE_PREFIX_PATH="$DEPS_PREFIX")
  fi

  log "Configuring CMake..."
  cmake "${cmake_args[@]}"

  log "Building (-j $JOBS)..."
  cmake --build "$BUILD_DIR" -j "$JOBS"

  ok "Done. Binary: $BUILD_DIR/voterpool"

  if [ "$tests_flag" = "ON" ]; then
    ok "Tests built: $BUILD_DIR/tests/"
  fi

  if [ "$RUN_TESTS" = "1" ]; then
    log "Running tests (ctest)..."
    ctest --test-dir "$BUILD_DIR" --output-on-failure -j 2
    ok "All tests passed."
  fi

  cat <<NEXT

──────────────────────────────────────────────────────────────
Next steps:
  server:   $BUILD_DIR/voterpool --config config/default.yaml
  backup:   $BUILD_DIR/voterpool checkpoint --config config/default.yaml \\
            --path /tmp/snapshot
  tests:    ctest --test-dir $BUILD_DIR --output-on-failure
──────────────────────────────────────────────────────────────
NEXT
}

drogon_installed_in_deps() { [ -f "$DEPS_PREFIX/lib/cmake/Drogon/DrogonConfig.cmake" ] || \
                             [ -d "$DEPS_PREFIX/lib/cmake/Drogon" ]; }

main "$@"
