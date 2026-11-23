#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 用例清单来自用例表（acceptance-suite-rework design D3），运行器读同一张表。
CASES_FILE="${ACCEPTANCE_CASES_FILE:-${SCRIPT_DIR}/acceptance_cases.tsv}"

# 用例表的数据行（去掉 # 注释和表头）。
case_rows() {
  grep -v '^#' "${CASES_FILE}" | awk -F'\t' 'NF && $1 != "suite"'
}

suite_names() {
  case_rows | awk -F'\t' '!seen[$1]++ { print $1 }'
}

usage() {
  cat <<'EOF'
Usage:
  scripts/check_robot.sh --list
  scripts/check_robot.sh <suite> <case> [case args...]
  scripts/check_robot.sh <suite> all

Examples:
  scripts/check_robot.sh mission queue
  scripts/check_robot.sh business all

Suites and cases (from scripts/acceptance_cases.tsv):
EOF
  local suite
  for suite in $(suite_names); do
    echo "  ${suite}:"
    echo "    $(suite_cases "${suite}") all" | fold -s -w 96 | sed '2,$s/^/    /'
  done
}

run_script() {
  local script_name="$1"
  shift || true
  local script_path="${SCRIPT_DIR}/${script_name}"
  if [[ ! -x "${script_path}" ]]; then
    echo "check script is missing or not executable: ${script_path}" >&2
    return 1
  fi
  echo "==> ${script_name} $*"
  "${script_path}" "$@"
}

case_script() {
  local suite="$1"
  local check_case="$2"
  local script
  script="$(case_rows | awk -F'\t' -v s="${suite}" -v c="${check_case}" '$1 == s && $2 == c { print $3; exit }')"
  [[ -n "${script}" ]] || return 1
  echo "${script}"
}

suite_cases() {
  local cases
  cases="$(case_rows | awk -F'\t' -v s="$1" '$1 == s { printf "%s%s", sep, $2; sep = " " }')"
  [[ -n "${cases}" ]] || return 1
  echo "${cases}"
}

run_case() {
  local suite="$1"
  local check_case="$2"
  shift 2
  local script_name
  if ! script_name="$(case_script "${suite}" "${check_case}")"; then
    echo "unknown check case: ${suite} ${check_case}" >&2
    usage >&2
    return 1
  fi
  run_script "${script_name}" "$@"
}

main() {
  if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    return 0
  fi
  if [[ "${1:-}" == "--list" ]]; then
    usage
    return 0
  fi
  if [[ $# -lt 2 ]]; then
    usage >&2
    return 1
  fi

  local suite="$1"
  local check_case="$2"
  shift 2

  if [[ "${check_case}" == "all" ]]; then
    local cases
    if ! cases="$(suite_cases "${suite}")"; then
      echo "unknown check suite: ${suite}" >&2
      usage >&2
      return 1
    fi
    local current_case
    for current_case in ${cases}; do
      run_case "${suite}" "${current_case}" "$@"
    done
    return 0
  fi

  run_case "${suite}" "${check_case}" "$@"
}

main "$@"
