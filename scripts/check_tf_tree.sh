#!/usr/bin/env bash
# TF 树：odom → base_footprint → base_link 两段变换都能查到（mock_bringup 栈）。
# tf2_echo 会一直打印、不会自行退出，所以 timeout 结束时退出码总是 124；判定看输出里有没有
# "Translation"，而不是看退出码（原脚本按退出码判定，从 M0 起就一直记为 TIMEOUT）。
set -euo pipefail

expect_transform() {
  local parent="$1" child="$2" out
  out="$(timeout 10s ros2 run tf2_ros tf2_echo "${parent}" "${child}" 2>&1 || true)"
  if ! rg -q "Translation" <<<"${out}"; then
    echo "no transform ${parent} -> ${child} within 10s:" >&2
    tail -5 <<<"${out}" >&2
    return 1
  fi
  echo "ok: ${parent} -> ${child}"
}

expect_transform odom base_footprint
expect_transform base_footprint base_link
