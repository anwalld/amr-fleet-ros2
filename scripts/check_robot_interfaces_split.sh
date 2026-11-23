#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

LEGACY_PACKAGE="robot_interfaces"
# 旧包已冻结：只剩 10 个 msg/action，不得再增减。
LEGACY_REMAINING_INTERFACE_COUNT=10
# 新包的接口总数不写死（原来写死 99，M3 加 TrafficYield.msg 后就失败了）：
# 改为双向核对"目录里的每个接口文件都在 CMakeLists 注册"与"注册的每一项都有文件"。
NEW_PACKAGES=(
  robot_interfaces_core
  robot_interfaces_navigation
  robot_interfaces_mission
  robot_interfaces_facility
  robot_interfaces_fleet
  robot_interfaces_business
  robot_interfaces_site
)

fail() {
  echo "robot interfaces split check failed: $*" >&2
  exit 1
}

interface_files() {
  local package_dir="$1"
  find "${package_dir}" -mindepth 2 -maxdepth 2 -type f \
    \( -name '*.msg' -o -name '*.srv' -o -name '*.action' \) \
    -printf '%P\n' | sort
}

require_file() {
  local path="$1"
  [[ -f "${path}" ]] || fail "missing ${path}"
}

check_package_metadata() {
  local package="$1"
  local package_dir="${ROOT_DIR}/src/${package}"
  require_file "${package_dir}/package.xml"
  require_file "${package_dir}/CMakeLists.txt"
  grep -q "<name>${package}</name>" "${package_dir}/package.xml" ||
    fail "${package}/package.xml has wrong or missing package name"
  grep -q "project(${package})" "${package_dir}/CMakeLists.txt" ||
    fail "${package}/CMakeLists.txt has wrong or missing project name"
  grep -q "rosidl_generate_interfaces" "${package_dir}/CMakeLists.txt" ||
    fail "${package}/CMakeLists.txt does not generate interfaces"
}

legacy_dir="${ROOT_DIR}/src/${LEGACY_PACKAGE}"
require_file "${legacy_dir}/package.xml"
require_file "${legacy_dir}/CMakeLists.txt"

declare -A legacy_interfaces=()
declare -A new_interfaces=()
declare -A new_interface_owner=()

while IFS= read -r rel_path; do
  [[ -n "${rel_path}" ]] || continue
  legacy_interfaces["${rel_path}"]=1
done < <(interface_files "${legacy_dir}")

legacy_count="${#legacy_interfaces[@]}"
[[ "${legacy_count}" -gt 0 ]] || fail "legacy package has no interface files"
[[ "${legacy_count}" -eq "${LEGACY_REMAINING_INTERFACE_COUNT}" ]] ||
  fail "legacy package exposes ${legacy_count} interfaces, expected ${LEGACY_REMAINING_INTERFACE_COUNT} msg/action interfaces"

if find "${legacy_dir}/srv" -type f -name '*.srv' -print -quit 2>/dev/null | grep -q .; then
  fail "legacy package still contains srv definitions"
fi

for package in "${NEW_PACKAGES[@]}"; do
  package_dir="${ROOT_DIR}/src/${package}"
  [[ -d "${package_dir}" ]] || fail "missing package directory ${package_dir}"
  check_package_metadata "${package}"

  while IFS= read -r rel_path; do
    [[ -n "${rel_path}" ]] || continue
    if [[ -n "${new_interfaces[${rel_path}]+x}" ]]; then
      fail "${rel_path} is duplicated in ${new_interface_owner[${rel_path}]} and ${package}"
    fi
    new_interfaces["${rel_path}"]=1
    new_interface_owner["${rel_path}"]="${package}"
    grep -q "\"${rel_path}\"" "${package_dir}/CMakeLists.txt" ||
      fail "${package}/CMakeLists.txt does not register ${rel_path}"
  done < <(interface_files "${package_dir}")
done

for package in "${NEW_PACKAGES[@]}"; do
  package_dir="${ROOT_DIR}/src/${package}"
  while IFS= read -r registered; do
    [[ -f "${package_dir}/${registered}" ]] ||
      fail "${package}/CMakeLists.txt registers ${registered} but the file is missing"
  done < <(grep -oE '"(msg|srv|action)/[A-Za-z0-9_]+\.(msg|srv|action)"' "${package_dir}/CMakeLists.txt" | tr -d '"')
done

new_count="${#new_interfaces[@]}"
[[ "${new_count}" -gt 0 ]] || fail "new packages expose no interfaces"
echo "new packages expose ${new_count} interfaces, all registered"

for rel_path in "${!legacy_interfaces[@]}"; do
  [[ -n "${new_interfaces[${rel_path}]+x}" ]] ||
    fail "legacy interface ${rel_path} is not copied into a target package"
done

if grep -R -n --include='*.msg' --include='*.srv' --include='*.action' \
  'robot_interfaces/' "${ROOT_DIR}"/src/robot_interfaces_* >/tmp/robot_interfaces_split_refs.$$ 2>/dev/null; then
  cat /tmp/robot_interfaces_split_refs.$$ >&2
  rm -f /tmp/robot_interfaces_split_refs.$$
  fail "target interface packages still reference legacy robot_interfaces custom types"
fi
rm -f /tmp/robot_interfaces_split_refs.$$

if grep -R -n -E 'find_package\(robot_interfaces|<depend>robot_interfaces</depend>|<build_depend>robot_interfaces</build_depend>|<exec_depend>robot_interfaces</exec_depend>' \
  "${ROOT_DIR}"/src/robot_interfaces_* >/tmp/robot_interfaces_split_pkg_refs.$$ 2>/dev/null; then
  cat /tmp/robot_interfaces_split_pkg_refs.$$ >&2
  rm -f /tmp/robot_interfaces_split_pkg_refs.$$
  fail "target interface packages depend on legacy robot_interfaces"
fi
rm -f /tmp/robot_interfaces_split_pkg_refs.$$

# CI 必须编译全部新接口包。两种写法都认：
#   1. 对整个工作区 colcon build（不带 --packages-select / --packages-up-to），且没有
#      --packages-skip 掉其中任何一个（2026-11-16 起的容器版 CI 就是这样）；
#   2. 带包过滤时，配置里要逐个写出这些包名（原 action-ros-ci 的 package-name 列表）。
ci_file="${ROOT_DIR}/.github/workflows/ci.yml"
require_file "${ci_file}"
grep -q "colcon build" "${ci_file}" || fail "CI does not run colcon build"
if grep -Eq -- "--packages-(select|up-to)" "${ci_file}"; then
  for package in "${NEW_PACKAGES[@]}"; do
    grep -q "${package}" "${ci_file}" || fail "CI package-name list is missing ${package}"
  done
  ci_coverage="explicit package list"
else
  for package in "${NEW_PACKAGES[@]}"; do
    if grep -E -- "--packages-skip[^#]*\b${package}\b" "${ci_file}" >/dev/null; then
      fail "CI skips ${package}"
    fi
  done
  ci_coverage="whole-workspace colcon build"
fi

echo "robot_interfaces split static check passed"
echo "CI coverage: ${ci_coverage}"
echo "legacy package remaining msg/action interfaces: ${legacy_count}"
echo "target package interfaces: ${new_count}"
for package in "${NEW_PACKAGES[@]}"; do
  count="$(interface_files "${ROOT_DIR}/src/${package}" | wc -l | tr -d ' ')"
  echo "  ${package}: ${count}"
done
