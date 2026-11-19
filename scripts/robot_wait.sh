#!/usr/bin/env bash

wait_service_type() {
  local service_name="$1"
  local expected_type="$2"
  local timeout_sec="${3:-10}"
  local err_file="${4:-/tmp/robot_wait_service.err}"
  local deadline=$((SECONDS + timeout_sec))
  while ! ros2 service type "${service_name}" 2>"${err_file}" | rg "${expected_type}"; do
    if (( SECONDS >= deadline )); then
      echo "${service_name} service did not become available as ${expected_type}" >&2
      return 1
    fi
    sleep 1
  done
}

wait_submit_order() {
  local timeout_sec="${1:-10}"
  local err_file="${2:-/tmp/robot_submit_order.err}"
  wait_service_type /v2/submit_order "robot_interfaces_business/srv/SubmitOrder" "${timeout_sec}" "${err_file}"
}

yaml_single_quote() {
  local value="$1"
  printf "'%s'" "${value//\'/\'\'}"
}

yaml_flow_list() {
  if (($# == 0)); then
    printf "[]"
    return
  fi
  local first=true
  printf "["
  local item
  for item in "$@"; do
    if ${first}; then
      first=false
    else
      printf ", "
    fi
    yaml_single_quote "${item}"
  done
  printf "]"
}

call_submit_order() {
  local order_id="$1"
  local order_type="$2"
  local priority="$3"
  local payload_json="$4"
  shift 4
  local tags
  tags="$(yaml_flow_list "$@")"
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/submit_order robot_interfaces_business/srv/SubmitOrder \
    "{order_id: $(yaml_single_quote "${order_id}"), order_type: $(yaml_single_quote "${order_type}"), priority: ${priority}, payload_json: $(yaml_single_quote "${payload_json}"), tags: ${tags}}"
}

# 断言一个设施资源的状态（acceptance-suite-rework design D7，specs/facility-reservation）。
# ListFacilityResources 按资源类型返回并列数组，这里按资源号取出同一下标的 status / holder / available 逐项比对。
# 用法：expect_facility_resource <resource_type> <resource_id> <status> <holder_id> <available: True|False>
expect_facility_resource() {
  local resource_type="$1" resource_id="$2" want_status="$3" want_holder="$4" want_available="$5" out
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_facility_resources \
    robot_interfaces_facility/srv/ListFacilityResources \
    "{resource_type: ${resource_type}, include_disabled: false}")" || {
    echo "list_facility_resources call failed for ${resource_type}" >&2
    return 1
  }
  FACILITY_OUT="${out}" python3 - "${resource_id}" "${want_status}" "${want_holder}" "${want_available}" <<'PY'
import ast, os, re, sys
rid, want_status, want_holder, want_available = sys.argv[1:5]
out = os.environ["FACILITY_OUT"]
def arr(name):
    m = re.search(r"\b" + name + r"=(\[[^\]]*\])", out)
    return ast.literal_eval(m.group(1)) if m else None
ids = arr("resource_ids")
if not ids or rid not in ids:
    sys.exit(f"facility resource {rid} not listed: {out}")
i = ids.index(rid)
got = {"status": arr("status")[i], "holder": arr("holder_ids")[i], "available": str(arr("available")[i])}
want = {"status": want_status, "holder": want_holder, "available": want_available}
bad = [f"{k}={got[k]!r} (want {want[k]!r})" for k in want if got[k] != want[k]]
if bad:
    sys.exit(f"facility resource {rid}: " + ", ".join(bad))
print(f"facility resource {rid}: status={got['status']} holder={got['holder']!r} available={got['available']}")
PY
}

# 等某个任务成为当前任务并进入指定状态（默认 FINISHED）；看到 FAILED / ERROR 立即失败。
# 第 4 个参数为 seen 时视为已见过该任务（取消之后当前任务号可能已清空）。
# 用法：wait_active_mission <mission_id> <timeout_sec> [state_regex] [seen]
wait_active_mission() {
  local mission_id="$1" timeout_sec="$2" want="${3:-FINISHED}"
  local deadline=$((SECONDS + timeout_sec)) seen=false active state
  [[ "${4:-}" == seen ]] && seen=true
  while true; do
    active="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state \
      robot_interfaces/msg/RobotState --field active_goal_id 2>/dev/null || true)"
    state="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state \
      robot_interfaces/msg/RobotState --field state 2>/dev/null || true)"
    grep -q "${mission_id}" <<<"${active}" && seen=true
    if ${seen} && grep -Eq "${want}" <<<"${state}"; then
      echo "mission ${mission_id} reached ${want}"
      return 0
    fi
    if grep -Eq "FAILED|ERROR" <<<"${state}"; then
      echo "mission ${mission_id} failed: state=${state} active=${active}" >&2
      return 1
    fi
    if (( SECONDS >= deadline )); then
      echo "mission ${mission_id} did not reach ${want}; seen=${seen} state=${state} active=${active}" >&2
      return 1
    fi
    sleep 1
  done
}

# 写一份长巡逻任务文件：沿 x 轴每 0.1 米一个目标。无 Nav2 的模拟导航下每个目标约 1 秒，
# 40 个目标约 40 秒，足够在任务结束前完成暂停、取消等操作（默认的 demo_patrol 只有 2 个点、约 1 秒）。
# 用法：write_long_mission <文件> <mission_id> [目标数，默认 40]
write_long_mission() {
  local file="$1" mission_id="$2" goals="${3:-40}" i
  {
    echo "mission_id: ${mission_id}"
    echo "frame_id: map"
    echo "loop: false"
    echo "goals:"
    for i in $(seq 1 "${goals}"); do
      printf '  - x: %s\n    y: 0.0\n    yaw: 0.0\n' "$(awk -v i="${i}" 'BEGIN{printf "%.2f", i*0.1}')"
    done
  } > "${file}"
}

# ---- 解析 ros2 service call 的响应（repr 形式），供逐项断言（acceptance-suite-rework design D7）----
# resp_scalar <字段>          从标准输入的响应里取标量字段，去掉引号；取不到时输出空并返回 1。
resp_scalar() {
  python3 -c '
import re, sys
text, name = sys.stdin.read(), sys.argv[1]
m = re.search(r"\b" + re.escape(name) + r"=(\x27(?:[^\x27\\]|\\.)*\x27|\"(?:[^\"\\]|\\.)*\"|[^,()\s]+)", text)
if not m:
    sys.exit(1)
v = m.group(1)
print(v[1:-1] if v[:1] in "\x27\"" else v)
' "$1"
}

# resp_row <键数组> <键> <字段数组>   在并列数组里找到键所在下标，输出字段数组同一下标的值。
resp_row() {
  python3 -c '
import ast, re, sys
text, key_field, key, field = sys.stdin.read(), *sys.argv[1:4]
def arr(n):
    m = re.search(r"\b" + re.escape(n) + r"=(\[[^\]]*\])", text)
    return ast.literal_eval(m.group(1)) if m else None
keys, vals = arr(key_field), arr(field)
if not keys or key not in keys or vals is None:
    sys.exit(1)
print(vals[keys.index(key)])
' "$1" "$2" "$3"
}

# expect_eq <说明> <实际> <期望>
expect_eq() {
  if [[ "$2" != "$3" ]]; then
    echo "expect failed: $1: got '$2', want '$3'" >&2
    return 1
  fi
  echo "ok: $1 = $3"
}

# expect_close <说明> <实际> <期望> [容差，默认 0.05]
expect_close() {
  if ! awk -v a="$2" -v b="$3" -v t="${4:-0.05}" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(a != "" && b != "" && d <= t) }'; then
    echo "expect failed: $1: got '$2', want '$3' (±${4:-0.05})" >&2
    return 1
  fi
  echo "ok: $1 ≈ $3"
}

wait_topic_type() {
  local topic_name="$1"
  local expected_type="$2"
  local timeout_sec="${3:-10}"
  local err_file="${4:-/tmp/robot_wait_topic.err}"
  local deadline=$((SECONDS + timeout_sec))
  while ! ros2 topic type "${topic_name}" 2>"${err_file}" | rg "${expected_type}"; do
    if (( SECONDS >= deadline )); then
      echo "${topic_name} topic did not become available as ${expected_type}" >&2
      return 1
    fi
    sleep 1
  done
}

wait_topic_sample() {
  local topic_name="$1"
  local expected_type="$2"
  local timeout_sec="${3:-10}"
  local err_file="${4:-/tmp/robot_wait_topic_sample.err}"
  if ! timeout "${timeout_sec}s" ros2 topic echo --once "${topic_name}" "${expected_type}" >"${err_file}" 2>&1; then
    echo "${topic_name} topic did not publish a ${expected_type} sample" >&2
    cat "${err_file}" >&2 || true
    return 1
  fi
}

wait_action_type() {
  local action_name="$1"
  local expected_type="$2"
  local timeout_sec="${3:-10}"
  local err_file="${4:-/tmp/robot_wait_action.err}"
  local deadline=$((SECONDS + timeout_sec))
  while ! ros2 action list -t 2>"${err_file}" | rg "${action_name}.*${expected_type}"; do
    if (( SECONDS >= deadline )); then
      echo "${action_name} action did not become available as ${expected_type}" >&2
      return 1
    fi
    sleep 1
  done
}

wait_lifecycle_active() {
  local service_name="$1"
  local timeout_sec="${2:-30}"
  local err_file="${3:-/tmp/robot_wait_lifecycle.err}"
  local deadline=$((SECONDS + timeout_sec))
  while true; do
    if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "${service_name}" lifecycle_msgs/srv/GetState "{}" >"${err_file}" 2>&1 &&
      rg "label(: |=)'?active'?" "${err_file}" >/dev/null; then
      return 0
    fi
    if (( SECONDS >= deadline )); then
      echo "${service_name} lifecycle state did not reach active" >&2
      cat "${err_file}" >&2 || true
      return 1
    fi
    sleep 1
  done
}

# ---------------------------------------------------------------------------
# 站点运输的装卸确认
#
# 上游提交 fb9e36e 起，站点运输任务到取货站后停在 WAIT_LOAD 等上货确认，到卸货站后
# 停在 WAIT_UNLOAD 等卸货确认（演示编排器 amr_sim_demo_director_node 会自己发确认）。
# 验收脚本里没人发确认，任务会一直停在 WAIT_LOAD 直到超时。下面的后台确认器代替人工：
# 轮询 /mission_runner/state，看到 WAIT_LOAD / WAIT_UNLOAD 就按消息里的站点名调用
# /v2/confirm_load / /v2/confirm_unload。确认被拒（例如刚确认过、状态还没刷新）只记日志。
# 确认调用带 10 秒超时：负载高时 DDS 偶尔丢服务应答，不加超时确认器会永远卡在那一次调用上，
# 之后的装卸都没人确认（M2 全量验收里 workflow_pause_resume 一次失败的原因）。
#
# 用法：
#   start_station_auto_confirm            # 启动；若脚本没有 EXIT trap，会自动注册一个停止它
#   stop_station_auto_confirm             # 手动停止
#   station_auto_confirm_loop <payload_id> <log_file>
#                                         # 自带 cleanup/PIDS 的脚本可以直接 start_bg 这个函数
# 环境变量：STATION_AUTO_CONFIRM_PAYLOAD（默认 acceptance_tote）、STATION_AUTO_CONFIRM_LOG。
# ---------------------------------------------------------------------------

STATION_AUTO_CONFIRM_PID=""

station_auto_confirm_loop() {
  local payload_id="${1:-acceptance_tote}"
  local log_file="${2:-/tmp/robot_station_auto_confirm.log}"
  local sample state message station
  while true; do
    sample="$(timeout 5s ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState 2>/dev/null || true)"
    state="$(sed -n 's/^state: *//p' <<<"${sample}" | tr -d "'\"" | head -n 1)"
    message="$(sed -n 's/^message: *//p' <<<"${sample}" | head -n 1)"
    case "${state}" in
      WAIT_LOAD)
        station="$(sed -n 's/.*pickup station \([A-Za-z0-9_]*\).*/\1/p' <<<"${message}")"
        if [[ -n "${station}" ]]; then
          echo "[auto_confirm $(date +%T)] WAIT_LOAD at ${station} -> confirm_load" >>"${log_file}"
          timeout 10s ros2 service call /v2/confirm_load robot_interfaces_facility/srv/ConfirmLoad \
            "{payload_id: ${payload_id}, weight_kg: 8.5, station_id: ${station}}" >>"${log_file}" 2>&1 || true
        fi
        ;;
      WAIT_UNLOAD)
        station="$(sed -n 's/.*dropoff station \([A-Za-z0-9_]*\).*/\1/p' <<<"${message}")"
        if [[ -n "${station}" ]]; then
          echo "[auto_confirm $(date +%T)] WAIT_UNLOAD at ${station} -> confirm_unload" >>"${log_file}"
          timeout 10s ros2 service call /v2/confirm_unload robot_interfaces_facility/srv/ConfirmUnload \
            "{payload_id: ${payload_id}, station_id: ${station}}" >>"${log_file}" 2>&1 || true
        fi
        ;;
    esac
    sleep 0.5
  done
}

stop_station_auto_confirm() {
  if [[ -n "${STATION_AUTO_CONFIRM_PID}" ]] && kill -0 "${STATION_AUTO_CONFIRM_PID}" 2>/dev/null; then
    kill "${STATION_AUTO_CONFIRM_PID}" 2>/dev/null || true
    wait "${STATION_AUTO_CONFIRM_PID}" 2>/dev/null || true
  fi
  STATION_AUTO_CONFIRM_PID=""
}

start_station_auto_confirm() {
  local payload_id="${STATION_AUTO_CONFIRM_PAYLOAD:-acceptance_tote}"
  local log_file="${STATION_AUTO_CONFIRM_LOG:-/tmp/robot_station_auto_confirm.log}"
  : >"${log_file}"
  station_auto_confirm_loop "${payload_id}" "${log_file}" &
  STATION_AUTO_CONFIRM_PID=$!
  if [[ -z "$(trap -p EXIT)" ]]; then
    trap stop_station_auto_confirm EXIT
  fi
}

# ---------------------------------------------------------------------------
# M2 滚动申请：按车辆号申请 / 释放一段路的路权（/v2/request_route_segment、
# /v2/release_route_segment）。release 的资源列表为空表示释放该车持有的全部资源。
#   call_request_route_segment <robot_id> <from> <to> [priority]
#   call_release_route_segment <robot_id> [resource_id...]
# ---------------------------------------------------------------------------

call_request_route_segment() {
  local robot_id="$1"
  local from_station="$2"
  local to_station="$3"
  local priority="${4:-0}"
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: $(yaml_single_quote "${robot_id}"), from_station: $(yaml_single_quote "${from_station}"), to_station: $(yaml_single_quote "${to_station}"), priority: ${priority}, station_path: []}"
}

call_release_route_segment() {
  local robot_id="$1"
  shift
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/release_route_segment robot_interfaces_mission/srv/ReleaseRouteSegment \
    "{robot_id: $(yaml_single_quote "${robot_id}"), resource_ids: $(yaml_flow_list "$@")}"
}
