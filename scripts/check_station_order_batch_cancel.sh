#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 10 /tmp/robot_station_batch_cancel_submit.err
wait_service_type /v2/cancel_station_order_batch "robot_interfaces_mission/srv/CancelStationOrderBatch" 10 /tmp/robot_station_batch_cancel.err

_rg_out="$(call_submit_order wms_cancel_alpha station_order_batch 50 \
  "batch_id=wms_cancel_alpha;order_ids=one,two,three;pickup_station_ids=receiving,storage_a,packing;dropoff_station_ids=storage_a,packing,dock;start_if_idle=false;preempt_current=false;continue_on_error=false" \
  station_order_batch)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_order_batch_wms_cancel_alpha_order_one" <<<"${_rg_out}"
rg "station_order_batch_wms_cancel_alpha_order_three" <<<"${_rg_out}"
rg "queue_size=3" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/cancel_station_order_batch robot_interfaces_mission/srv/CancelStationOrderBatch \
  "{batch_id: wms_cancel_alpha, cancel_active: false}")"
rg "success=True" <<<"${_rg_out}"
rg "station_order_batch_wms_cancel_alpha_order_one" <<<"${_rg_out}"
rg "station_order_batch_wms_cancel_alpha_order_three" <<<"${_rg_out}"
rg "queue_size=0" <<<"${_rg_out}"

echo "station order batch cancel passed"
