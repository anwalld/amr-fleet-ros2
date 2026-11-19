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
