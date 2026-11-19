#!/usr/bin/env bash
# 夜间批量验证（二开计划第 9 节）：一天只跑一次全栈验证，次日早上看报告。
#
# 用法（在虚拟机 ~/robot_ws 下）：
#   scripts/nightly.sh [输出目录]          默认 ~/nightly/<日期>
# 从 Mac 发起（setsid nohup 脱离 ssh，断开也继续跑；bash -ic 加载 ~/.bashrc 里的 ROS 与渲染变量）：
#   ssh ubuntu@<IP> "bash -ic 'cd ~/robot_ws && NIGHTLY_REV=<提交号> setsid nohup scripts/nightly.sh >/dev/null 2>&1 </dev/null &'"
# 夜跑前在 Mac 上防睡：caffeinate -i -w <VMware 进程号>
#
# 八个阶段（阶段之间互不依赖，一个失败不中断后面的；每阶段自带超时，阶段间清掉全部 ROS/Gazebo 进程）：
#   1 编译        全量 colcon build                              build.log
#   2 单测        colcon test 全部包                              测试汇总
#   3 全量验收    run_acceptance_baseline.sh 跑用例表全部用例       results.tsv；全部 PASS 才通过，与上一夜对照只作报告
#   4 一键演示    headless amr_demo 跑一遍，按关键日志行判定        demo.log 判定结果
#   5 交通压测    2/4/6 台 × whole_route/rolling，每组 30 分       stress_*/、metrics.tsv
#   6 死锁场景    两车对向、三车成环、环外有车等待，各 3 遍         deadlock_scenarios.tsv
#   7 调度台冒烟  offscreen robot_console 对着压测栈跑 5 分        console_smoke.tsv
#   8 报告        汇总通过/失败、与上一夜 diff、指标表              report.md → docs/baseline/nightly/<日期>.md
# 阶段 5–7 对应的模块还没做出来时自动记 SKIP 并写明原因。
#
# 环境变量：
#   DRY_RUN=1        白天自检：不编译、不起仿真栈、不清进程，只检查各阶段的前置条件并出报告
#   ONLY_STAGES="1 2 8"  只跑列出的阶段（报告阶段 8 建议总是带上）
#   PREV_DIR=<目录>  指定"上一夜"的输出目录（默认取同级目录里最新的一个）
#   NIGHTLY_REV      被测代码的提交号（虚拟机工作区没有 .git，由发起方传入，写进报告）
#   STAGE<n>_TIMEOUT 覆盖第 n 阶段的超时秒数
#   STRESS_MINUTES   阶段 5 每组分钟数（默认 30）；STRESS_NETWORK 阶段 5 路网（默认 workshop_2way）
#   SCENARIO_ROUNDS  阶段 6 每个场景的遍数（默认 3）
#
# 退出码：0 = 没有 FAIL/TIMEOUT 的阶段（SKIP 不算失败），1 = 有。
#
# 注意：所有 ros2 service call 都必须带 timeout。FastDDS 在高负载下会丢服务应答，
# 不带超时的调用会一直挂到用例的 600 秒上限。本脚本自身不直接调用 ros2 service call；
# 被调用的脚本若要调用，必须用 `timeout <秒> ros2 service call ...`（现有脚本统一为
# `timeout "${ROS2_CALL_TIMEOUT:-30}"`，默认 30 秒，可用环境变量调；用例脚本另由 lint:script_timeouts 检查）。报告里会列出
# scripts/ 下仍有不带 timeout 的 ros2 service call 的脚本。

# 阶段函数经 "stage_${N}" 间接调用，shellcheck 看不出来。
# shellcheck disable=SC2329
set -uo pipefail

SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname "${SELF}")"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

DRY_RUN="${DRY_RUN:-0}"
STRESS_MINUTES="${STRESS_MINUTES:-30}"
STRESS_NETWORK="${STRESS_NETWORK:-workshop_2way}"
SCENARIO_ROUNDS="${SCENARIO_ROUNDS:-3}"
DEMO_TIMEOUT="${DEMO_TIMEOUT:-900}"
SKIP_RC=77

STAGE_IDS=(1 2 3 4 5 6 7 8)
declare -A STAGE_NAME=(
  [1]=build [2]=unit_test [3]=acceptance [4]=demo
  [5]=traffic_stress [6]=deadlock_scenarios [7]=console_smoke [8]=report
)
declare -A STAGE_TITLE=(
  [1]="编译" [2]="单测" [3]="全量验收" [4]="一键演示"
  [5]="交通压测矩阵" [6]="死锁场景" [7]="调度台冒烟" [8]="报告"
)
# 默认超时（秒）：比计划预计时长留足余量。
declare -A STAGE_TIMEOUT_DEFAULT=(
  [1]=3600 [2]=3600 [3]=14400 [4]=1500
  [5]=$(( 6 * (STRESS_MINUTES + 5) * 60 )) [6]=5400 [7]=1800 [8]=600
)

stage_dir() { echo "${OUT_DIR}/stage${1}_${STAGE_NAME[$1]}"; }
stage_timeout() {
  local var="STAGE${1}_TIMEOUT"
  echo "${!var:-${STAGE_TIMEOUT_DEFAULT[$1]}}"
}
log() { echo "[$(date '+%F %T')] $*"; }
dry() { [[ "${DRY_RUN}" == 1 ]]; }

source_ros() {
  set +u
  # shellcheck disable=SC1091
  [[ -f /opt/ros/jazzy/setup.bash ]] && source /opt/ros/jazzy/setup.bash
  # shellcheck disable=SC1091
  [[ -f "${ROOT_DIR}/install/setup.bash" ]] && source "${ROOT_DIR}/install/setup.bash"
  set -u
}

# Gazebo 渲染环境。虚拟机的 VMware 虚拟 GPU 没开 3D，必须用 Mesa llvmpipe 软件渲染，
# 这几个变量平时写在 ~/.bashrc 里；但非交互 shell（ssh 直接起、setsid、cron）不读 .bashrc，
# Gazebo 会因 OpenGL 3.3 不可用起不来，所有依赖它的用例都拿不到里程计
# （2026-11-18 白天一轮因此阶段 3 掉到 45/99）。这里给默认值，已设置的不覆盖。
export_render_env() {
  : "${LIBGL_ALWAYS_SOFTWARE:=1}" "${MESA_LOADER_DRIVER_OVERRIDE:=kms_swrast}"
  : "${GALLIUM_DRIVER:=llvmpipe}" "${MESA_GL_VERSION_OVERRIDE:=4.5}"
  export LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER MESA_GL_VERSION_OVERRIDE
}
export_render_env

# 杀掉一个进程组（先 INT/TERM 给 launch 机会收尾，再 KILL）。
kill_group() {
  local pgid="$1"
  kill -INT -- "-${pgid}" 2>/dev/null; sleep 3
  kill -TERM -- "-${pgid}" 2>/dev/null; sleep 2
  kill -KILL -- "-${pgid}" 2>/dev/null
}

# 清掉所有 ROS / Gazebo 进程（与验收运行器一致；方括号写法避免误杀发起 ssh 的 shell）。
ros_sweep() {
  dry && return 0
  pkill -KILL -f "[/]opt/ros/jazzy/lib/" 2>/dev/null
  pkill -KILL -f "[r]obot_ws/install/" 2>/dev/null
  pkill -KILL -f "[g]z sim" 2>/dev/null
  pkill -KILL -f "[r]os2 (launch|run|topic|service|action|node|param)" 2>/dev/null
  pkill -KILL -f "[r]os2cli.daemon" 2>/dev/null
  pkill -KILL -f "[r]obot_ws/scripts/.*\.py" 2>/dev/null
  sleep 2
}

# 阶段内部写结论摘要（几行 markdown，报告直接拼接）。
summary() { echo "$*" >> "${SDIR}/summary.md"; }
# M4：列出调度台回放录像（行数、大小），报告里能看到确实录到了数据。
capture_summary() {
  local dir="$1" pattern="$2" f n=0
  for f in "${dir}"/${pattern}; do
    [[ -f "${f}" ]] || continue
    (( n == 0 )) && summary "调度台回放录像："
    summary "- ${f#"${dir}"/}：$(wc -l < "${f}" | tr -d ' ') 行，$(du -h "${f}" | cut -f1)"
    n=$((n + 1))
  done
  (( n > 0 )) || summary "调度台回放录像：无"
}
skip() { summary "SKIP：$*"; exit "${SKIP_RC}"; }

# ---------------------------------------------------------------------------
# 阶段实现。每个阶段在独立进程组里由本脚本 `__stage <n> <输出目录>` 运行，
# 工作目录为工作区根目录，产出写到 ${SDIR}；退出码 0 = PASS，77 = SKIP，其他 = FAIL（超时由外层判定）。
# ---------------------------------------------------------------------------

stage_1() {
  cd "${ROOT_DIR}" || exit 1
  if dry; then
    local n
    n=$(colcon list --names-only 2>/dev/null | wc -l)
    summary "DRY_RUN：未编译；colcon list 识别到 ${n} 个包"
    (( n > 0 )) || exit 1
    exit "${SKIP_RC}"
  fi
  MAKEFLAGS=-j2 colcon build --symlink-install --parallel-workers 2 \
    --cmake-args -DCMAKE_BUILD_TYPE=Release > "${SDIR}/build.log" 2>&1
  local rc=$?
  local line
  line=$(grep -E '^Summary:' "${SDIR}/build.log" | tail -1)
  summary "colcon build rc=${rc}；${line:-无 Summary 行}"
  (( rc == 0 )) || { summary '```'; grep -E 'Failed|error:' "${SDIR}/build.log" | head -20 >> "${SDIR}/summary.md"; summary '```'; }
  exit "${rc}"
}

stage_2() {
  cd "${ROOT_DIR}" || exit 1
  if dry; then
    [[ -d "${ROOT_DIR}/build" ]] || skip "DRY_RUN：没有 build/ 目录"
    skip "DRY_RUN：全量 colcon test 留给夜跑（白天只跑改动包单测）"
  fi
  colcon test --parallel-workers 2 > "${SDIR}/test.log" 2>&1
  colcon test-result --all > "${SDIR}/test_result.txt" 2>&1
  local rc=$?
  local line
  line=$(grep -E '^Summary:' "${SDIR}/test_result.txt" | tail -1)
  summary "colcon test-result rc=${rc}；${line:-无 Summary 行}"
  if (( rc != 0 )); then
    summary '```'
    grep -vE '^Summary:' "${SDIR}/test_result.txt" | grep -E '\.xml' | head -20 >> "${SDIR}/summary.md"
    summary '```'
  fi
  exit "${rc}"
}

# 找"上一夜"的输出目录：PREV_DIR，或同级目录中比本次早、且有对应文件的最新一个。
prev_night_file() {
  local rel="$1" dir
  if [[ -n "${PREV_DIR:-}" ]]; then
    [[ -f "${PREV_DIR}/${rel}" ]] && echo "${PREV_DIR}/${rel}"
    return 0
  fi
  local parent cur
  parent="$(dirname "${OUT_DIR}")"; cur="$(basename "${OUT_DIR}")"
  while IFS= read -r dir; do
    [[ "$(basename "${dir}")" < "${cur}" && -f "${dir}/${rel}" ]] && { echo "${dir}/${rel}"; return 0; }
  done < <(find "${parent}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort -r)
}

stage_3() {
  local runner="${SCRIPT_DIR}/run_acceptance_baseline.sh"
  [[ -x "${runner}" ]] || { summary "找不到 ${runner}"; exit 1; }
  if dry; then
    local n
    n=$(grep -v '^#' "${SCRIPT_DIR}/acceptance_cases.tsv" | awk -F'\t' 'NF && $1 != "suite"' | wc -l | tr -d ' ')
    skip "DRY_RUN：验收运行器存在，用例表列出 ${n} 个用例"
  fi
  "${runner}" "${SDIR}/acceptance" > "${SDIR}/runner.log" 2>&1
  local results="${SDIR}/acceptance/results.tsv"
  [[ -f "${results}" ]] || { summary "运行器没有产出 results.tsv"; exit 1; }
  cp "${results}" "${SDIR}/results.tsv"

  # 对比基准：上一夜的结果；找不到时用新基线 docs/baseline/acceptance_baseline.tsv。
  local prev
  prev="$(prev_night_file "stage3_acceptance/results.tsv")"
  if [[ -z "${prev}" && -f "${ROOT_DIR}/docs/baseline/acceptance_baseline.tsv" ]]; then
    prev="${ROOT_DIR}/docs/baseline/acceptance_baseline.tsv"
  fi
  # 判定：本夜全部用例 PASS 才算通过；对比（经映射表换算）、新增、已删除、超预算只写进报告
  # （acceptance-suite-rework design D10）。
  python3 "${SCRIPT_DIR}/acceptance_compare.py" "${results}" "${prev}" \
    "${SCRIPT_DIR}/acceptance_cases.tsv" "${ROOT_DIR}/docs/baseline/acceptance_case_map.tsv" \
    "${SDIR}/compare.md" > /dev/null
  local rc=$?
  cat "${SDIR}/compare.md" >> "${SDIR}/summary.md"
  exit "${rc}"
}

# 一键演示判定：编排器的关键日志行（顺序即演示流程）全部出现，且没有动作失败。
DEMO_REQUIRED=(
  "AMR sim demo services are ready; starting default scenario"
  "reached FINISHED"
  "release beta traffic reservation"
  "AMR sim demo action succeeded: clear dynamic speed limit"
)
DEMO_FAIL_PATTERN="AMR sim demo action failed"

stage_4() {
  local launch_file="${ROOT_DIR}/src/robot_bringup/launch/amr_demo.launch.py"
  [[ -f "${launch_file}" ]] || { summary "找不到 ${launch_file}"; exit 1; }
  dry && skip "DRY_RUN：启动文件存在，判定 ${#DEMO_REQUIRED[@]} 条关键日志；全栈演示留给夜跑"
  export ROS_DOMAIN_ID="$(( RANDOM % 150 + 50 ))"
  setsid ros2 launch robot_bringup amr_demo.launch.py gui:=false use_rviz:=false \
    > "${SDIR}/demo.log" 2>&1 < /dev/null &
  local pgid=$! t0=${SECONDS} done_line="${DEMO_REQUIRED[-1]}"
  while (( SECONDS - t0 < DEMO_TIMEOUT )); do
    grep -qF "${done_line}" "${SDIR}/demo.log" && break
    kill -0 "${pgid}" 2>/dev/null || break
    sleep 5
  done
  local secs=$(( SECONDS - t0 ))
  sleep 5
  kill_group "${pgid}"
  local missing=0 line
  : > "${SDIR}/demo_verdict.tsv"
  for line in "${DEMO_REQUIRED[@]}"; do
    if grep -qF "${line}" "${SDIR}/demo.log"; then
      printf "%s\tOK\n" "${line}" >> "${SDIR}/demo_verdict.tsv"
    else
      printf "%s\tMISSING\n" "${line}" >> "${SDIR}/demo_verdict.tsv"; missing=$((missing + 1))
    fi
  done
  local failed
  failed=$(grep -cF "${DEMO_FAIL_PATTERN}" "${SDIR}/demo.log")
  summary "用时 ${secs}s；关键日志缺 ${missing}/${#DEMO_REQUIRED[@]} 条；动作失败 ${failed} 次"
  (( missing > 0 || failed > 0 )) && {
    summary '```'
    grep -F "MISSING" "${SDIR}/demo_verdict.tsv" >> "${SDIR}/summary.md"
    grep -F "${DEMO_FAIL_PATTERN}" "${SDIR}/demo.log" | head -10 >> "${SDIR}/summary.md"
    summary '```'
    exit 1
  }
  exit 0
}

# 把一组压测的 summary.json 摘成 metrics.tsv 的一行（M3 之前没有的死锁字段留空）。
METRICS_HEADER="robots	mode	network	rc	tasks	throughput_per_h	mean_wait_route_s	violations	deadlocks	reroute	backoff	pause	resolve_ms_mean	resolve_ms_max	detect_us_mean	detect_us_max"
metrics_row() {
  local robots="$1" mode="$2" rc="$3" json="$4"
  python3 - "${robots}" "${mode}" "${rc}" "${json}" "${STRESS_NETWORK}" <<'PY'
import json, sys
robots, mode, rc, path, network = sys.argv[1:6]
try:
    s = json.load(open(path))
except Exception:
    s = {}
def g(*keys):
    for k in keys:
        if k in s and s[k] is not None:
            return str(s[k])
    return ""
print("\t".join([robots, mode, g("network") or network, rc, g("tasks_finished"),
                 g("throughput_tasks_per_hour"), g("mean_wait_route_s"), g("violations"),
                 g("deadlocks_detected"), g("deadlock_reroute"), g("deadlock_backoff"),
                 g("deadlock_pause"), g("resolution_ms_mean"), g("resolution_ms_max"),
                 g("detection_us_mean"), g("detection_us_max")]))
PY
}

stage_5() {
  local stress="${SCRIPT_DIR}/run_fleet_traffic_stress.sh"
  [[ -x "${stress}" ]] || skip "找不到 ${stress}"
  # 阶段 5 要同时采 M2 吞吐与 M3 死锁指标；压测脚本加上死锁列（M3 tasks 8.1）之前跳过。
  grep -q "deadlocks.csv" "${SCRIPT_DIR}/fleet_traffic_stress.py" 2>/dev/null || \
    skip "压测脚本还没有死锁指标列（M3 tasks 8.1 未完成）；M2 吞吐数据见 docs/baseline/M2.md"
  dry && skip "DRY_RUN：压测脚本已有死锁列；6 组 × ${STRESS_MINUTES} 分钟留给夜跑"
  printf "%s\n" "${METRICS_HEADER}" > "${SDIR}/metrics.tsv"
  local robots mode rc fails=0 dir capture
  for robots in 2 4 6; do
    for mode in whole_route rolling; do
      dir="${SDIR}/stress_${robots}_${mode}"
      log "stress ${robots} ${mode} -> ${dir}"
      # M4：只在 6 台 rolling 这一组同时录调度台回放（与前几夜同组吞吐对比，检查采集是否有干扰）。
      capture=0
      [[ "${robots}" == 6 && "${mode}" == rolling ]] && capture=1
      CONSOLE_CAPTURE="${capture}" NETWORK="${STRESS_NETWORK}" \
        "${stress}" "${robots}" "${STRESS_MINUTES}" "${mode}" "${dir}" \
        > "${SDIR}/stress_${robots}_${mode}.out" 2>&1 < /dev/null
      rc=$?
      ros_sweep
      metrics_row "${robots}" "${mode}" "${rc}" "${dir}/summary.json" >> "${SDIR}/metrics.tsv"
      (( rc == 0 )) || fails=$((fails + 1))
    done
  done
  summary "6 组中 ${fails} 组退出码非 0（1 = 有安全违规，2 = 车队没就绪）；路网 ${STRESS_NETWORK}，每组 ${STRESS_MINUTES} 分钟"
  capture_summary "${SDIR}" "stress_*/console.jsonl"
  summary ""
  awk -F'\t' 'NR==1 { h="|"; s="|"; for (i=1;i<=NF;i++) { h=h" "$i" |"; s=s" --- |" } print h; print s; next }
              { r="|"; for (i=1;i<=NF;i++) r=r" "$i" |"; print r }' "${SDIR}/metrics.tsv" >> "${SDIR}/summary.md"
  (( fails == 0 ))
}

stage_6() {
  local scen="${SCRIPT_DIR}/run_deadlock_scenarios.sh"
  [[ -x "${scen}" ]] || skip "死锁场景脚本 scripts/run_deadlock_scenarios.sh 还没写（M3 tasks 8.2）"
  dry && skip "DRY_RUN：场景脚本存在；${SCENARIO_ROUNDS} 遍 × 3 个场景留给夜跑"
  CONSOLE_CAPTURE=1 "${scen}" "${SDIR}" "${SCENARIO_ROUNDS}" > "${SDIR}/scenarios.log" 2>&1 < /dev/null
  local rc=$?
  capture_summary "${SDIR}" "scenario_*.console.jsonl"
  if [[ -f "${SDIR}/deadlock_scenarios.tsv" ]]; then
    local pass total
    total=$(tail -n +2 "${SDIR}/deadlock_scenarios.tsv" | wc -l)
    pass=$(awk -F'\t' 'NR==1 { for (i=1;i<=NF;i++) if ($i=="verdict") v=i; next }
                         v && $v=="PASS"' "${SDIR}/deadlock_scenarios.tsv" | wc -l)
    summary "场景脚本 rc=${rc}；PASS ${pass}/${total} 遍"
    summary ""
    awk -F'\t' 'NR==1 { h="|"; s="|"; for (i=1;i<=NF;i++) { h=h" "$i" |"; s=s" --- |" } print h; print s; next }
                { r="|"; for (i=1;i<=NF;i++) r=r" "$i" |"; print r }' "${SDIR}/deadlock_scenarios.tsv" >> "${SDIR}/summary.md"
  else
    summary "场景脚本 rc=${rc}，没有产出 deadlock_scenarios.tsv"
    rc=1
  fi
  exit "${rc}"
}

stage_7() {
  local smoke="${SCRIPT_DIR}/run_console_smoke.sh"
  [[ -d "${ROOT_DIR}/src/robot_console" ]] || skip "调度台包 src/robot_console 还没有（M4）"
  [[ -x "${smoke}" ]] || skip "调度台冒烟脚本 scripts/run_console_smoke.sh 还没写（M4）"
  dry && skip "DRY_RUN：冒烟脚本存在；对着压测栈跑 5 分钟留给夜跑"
  QT_QPA_PLATFORM=offscreen "${smoke}" "${SDIR}" > "${SDIR}/smoke.log" 2>&1 < /dev/null
  local rc=$?
  if [[ -f "${SDIR}/console_smoke.tsv" ]]; then
    summary "冒烟脚本 rc=${rc}"
    summary '```'
    cat "${SDIR}/console_smoke.tsv" >> "${SDIR}/summary.md"
    summary '```'
  else
    summary "冒烟脚本 rc=${rc}，没有产出 console_smoke.tsv"
    rc=1
  fi
  exit "${rc}"
}

stage_8() {
  local report="${OUT_DIR}/report.md" n verdict dir secs
  local night_date
  night_date="$(cat "${OUT_DIR}/.started_date" 2>/dev/null || date +%F)"
  {
    echo "# 夜跑报告 ${night_date}"
    echo
    echo "- 被测代码：\`${NIGHTLY_REV:-未知（发起时未传 NIGHTLY_REV）}\`"
    echo "- 开始：$(cat "${OUT_DIR}/.started_at" 2>/dev/null)；报告生成：$(date '+%F %T')"
    echo "- 主机：$(hostname)，$(nproc) 核；负载 $(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null)"
    echo "- 输出目录（CSV、日志留在虚拟机）：\`${OUT_DIR}\`"
    dry && echo "- **DRY_RUN**：只检查了各阶段前置条件，没有编译、没有起仿真栈"
    echo
    echo "| 阶段 | 结论 | 用时 | 摘要 |"
    echo "| --- | --- | --- | --- |"
    for n in "${STAGE_IDS[@]}"; do
      (( n == 8 )) && continue
      dir="$(stage_dir "${n}")"
      verdict="$(cat "${dir}/verdict" 2>/dev/null || echo 未运行)"
      secs="$(cat "${dir}/seconds" 2>/dev/null || echo -)"
      echo "| ${n} ${STAGE_TITLE[$n]} | ${verdict} | ${secs}s | $(head -1 "${dir}/summary.md" 2>/dev/null | tr '|' '/') |"
    done
    for n in "${STAGE_IDS[@]}"; do
      (( n == 8 )) && continue
      dir="$(stage_dir "${n}")"
      [[ -f "${dir}/summary.md" ]] || continue
      echo
      echo "## 阶段 ${n} ${STAGE_TITLE[$n]}（$(cat "${dir}/verdict" 2>/dev/null)）"
      echo
      cat "${dir}/summary.md"
    done
    echo
    echo "## 提示：不带 timeout 的 ros2 service call"
    echo
    local f cnt files=0 total=0 shared=0 untimed="${OUT_DIR}/untimed_service_calls.txt"
    : > "${untimed}"
    for f in "${SCRIPT_DIR}"/*.sh; do
      cnt=$(grep -E 'ros2 service call' "${f}" | grep -vcE 'timeout|^[[:space:]]*#')
      (( cnt > 0 )) || continue
      echo "scripts/$(basename "${f}")	${cnt}" >> "${untimed}"
      files=$((files + 1)); total=$((total + cnt))
      [[ "$(basename "${f}")" == robot_wait.sh ]] && shared=${cnt}
    done
    echo "FastDDS 高负载下会丢服务应答，不带超时的调用会挂到 600 秒上限。"
    echo "按行粗查（同一行没有 \`timeout\`）：${files} 个脚本共 ${total} 处，其中共用助手 \`scripts/robot_wait.sh\` ${shared} 处；明细 \`${untimed}\`。"
  } > "${report}"

  if dry; then
    summary "DRY_RUN：报告写在 ${report}，不复制进 docs/baseline/nightly/"
  else
    mkdir -p "${ROOT_DIR}/docs/baseline/nightly"
    cp "${report}" "${ROOT_DIR}/docs/baseline/nightly/${night_date}.md"
    summary "报告：${report}，已复制到 docs/baseline/nightly/${night_date}.md（虚拟机工作区，需取回 Mac 提交）"
  fi
  exit 0
}

# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

if [[ "${1:-}" == "__stage" ]]; then
  N="$2"; OUT_DIR="$3"
  SDIR="$(stage_dir "${N}")"
  # 在工作区根目录运行：不少验收脚本用相对路径 install/setup.bash。产出一律写到 ${SDIR}。
  mkdir -p "${SDIR}"; cd "${ROOT_DIR}" || exit 1
  source_ros
  "stage_${N}"
  exit $?
fi

OUT_DIR="${1:-${HOME}/nightly/$(date +%F)}"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"
date +%F > "${OUT_DIR}/.started_date"
date '+%F %T' > "${OUT_DIR}/.started_at"
MAIN_LOG="${OUT_DIR}/nightly.log"
exec > >(tee -a "${MAIN_LOG}") 2>&1

ONLY_STAGES="${ONLY_STAGES:-}"
log "nightly start: out=${OUT_DIR} dry_run=${DRY_RUN} only=[${ONLY_STAGES:-全部}] rev=${NIGHTLY_REV:-未知}"

trap 'ros_sweep' EXIT
ros_sweep
any_fail=0
for n in "${STAGE_IDS[@]}"; do
  [[ -n "${ONLY_STAGES}" && " ${ONLY_STAGES} " != *" ${n} "* ]] && continue
  sdir="$(stage_dir "${n}")"
  rm -rf "${sdir}"; mkdir -p "${sdir}"
  t="$(stage_timeout "${n}")"
  log "stage ${n} ${STAGE_NAME[$n]} start (timeout ${t}s)"
  start=${SECONDS}
  setsid timeout --kill-after=30 "${t}" bash "${SELF}" __stage "${n}" "${OUT_DIR}" \
    > "${sdir}/stage.log" 2>&1 < /dev/null &
  pgid=$!
  wait "${pgid}"
  rc=$?
  secs=$(( SECONDS - start ))
  dry || kill_group "${pgid}" 2>/dev/null
  ros_sweep
  case ${rc} in
    0) verdict=PASS ;;
    "${SKIP_RC}") verdict=SKIP ;;
    124|137) verdict=TIMEOUT; echo "超过 ${t}s 被终止" >> "${sdir}/summary.md" ;;
    *) verdict="FAIL(${rc})" ;;
  esac
  [[ "${verdict}" == PASS || "${verdict}" == SKIP ]] || any_fail=1
  echo "${verdict}" > "${sdir}/verdict"
  echo "${secs}" > "${sdir}/seconds"
  log "stage ${n} ${STAGE_NAME[$n]} ${verdict} (${secs}s): $(head -1 "${sdir}/summary.md" 2>/dev/null)"
done
log "nightly done: report=${OUT_DIR}/report.md any_fail=${any_fail}"
exit "${any_fail}"
