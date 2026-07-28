#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build-wsl-armhf"
stage_dir="${project_dir}/dist/armhf-root"
toolchain="${project_dir}/cmake/toolchains/debian-armhf.cmake"
jobs="${CEDARVIEW_BUILD_JOBS:-$(nproc)}"

die()
{
  echo "ERROR: $*" >&2
  exit 1
}

if ! grep -qi wsl2 /proc/sys/kernel/osrelease; then
  die "Run this script inside WSL2 Debian, not on the X6 Pro."
fi

source /etc/os-release
[[ "${ID:-}" == "debian" ]] ||
  die "This cross-build release supports Debian under WSL2."

[[ "$(dpkg --print-architecture)" == "amd64" ]] ||
  die "WSL2 host architecture must be amd64."
dpkg --print-foreign-architectures | grep -qx armhf ||
  die "armhf multiarch is missing. Run ./scripts/setup-wsl-armhf.sh."

for command_name in \
  arm-linux-gnueabihf-g++ \
  arm-linux-gnueabihf-readelf \
  cmake \
  file \
  ninja \
  pkg-config
do
  command -v "${command_name}" >/dev/null ||
    die "Missing ${command_name}. Run ./scripts/setup-wsl-armhf.sh."
done

required_paths=(
  /usr/lib/arm-linux-gnueabihf/cmake/Qt6/Qt6Config.cmake
  /usr/lib/arm-linux-gnueabihf/pkgconfig/gstreamer-1.0.pc
  /usr/lib/arm-linux-gnueabihf/pkgconfig/gstreamer-video-1.0.pc
  /usr/lib/x86_64-linux-gnu/cmake/Qt6
)
for required_path in "${required_paths[@]}"; do
  [[ -e "${required_path}" ]] ||
    die "Missing ${required_path}. Run ./scripts/setup-wsl-armhf.sh."
done

host_qt_version="$(
  dpkg-query -W -f='${Version}' qt6-base-dev:amd64 2>/dev/null
)" || die "Missing qt6-base-dev:amd64."
target_qt_version="$(
  dpkg-query -W -f='${Version}' qt6-base-dev:armhf 2>/dev/null
)" || die "Missing qt6-base-dev:armhf."
[[ "${host_qt_version}" == "${target_qt_version}" ]] ||
  die "Host Qt ${host_qt_version} and target Qt ${target_qt_version} differ."

if [[ -f "${build_dir}/CMakeCache.txt" ]]; then
  cached_compiler="$(
    sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' \
      "${build_dir}/CMakeCache.txt" | head -n 1
  )"
  if [[ -n "${cached_compiler}" &&
        "${cached_compiler}" != *arm-linux-gnueabihf-g++ ]]; then
    die "${build_dir} contains a non-ARM CMake cache; remove that directory."
  fi
fi

cmake -S "${project_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${toolchain}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "${build_dir}" --parallel "${jobs}"

binary="${build_dir}/cedarview"
[[ -x "${binary}" ]] || die "Build completed without creating ${binary}."

file_output="$(file -b "${binary}")"
machine_output="$(
  arm-linux-gnueabihf-readelf -h "${binary}" |
    sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p'
)"

[[ "${file_output}" == *"ELF 32-bit"* && "${file_output}" == *"ARM"* ]] ||
  die "Wrong output architecture: ${file_output}"
[[ "${machine_output}" == "ARM" ]] ||
  die "readelf reported the wrong machine: ${machine_output}"

if arm-linux-gnueabihf-readelf -d "${binary}" |
    grep -Eq 'x86_64|lib64/ld-linux-x86-64'; then
  die "The ARM binary contains an amd64 runtime dependency."
fi

rm -rf "${stage_dir}"
DESTDIR="${stage_dir}" cmake --install "${build_dir}"

mkdir -p "${project_dir}/dist"
cp -f "${stage_dir}/usr/bin/cedarview" \
  "${project_dir}/dist/cedarview-armhf"

echo
echo "Cross-build verified."
echo "Host:   amd64 WSL2"
echo "Target: ${file_output}"
echo "Binary: ${project_dir}/dist/cedarview-armhf"
