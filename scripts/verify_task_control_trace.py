"""Check the evidence streams required by each task-control scenario."""

import collections
import json
import sys
from pathlib import Path


def verify_trace(case, path):
    required = {
        "d16-delayed-reply": ("operator_request", "operator_reply", "navigation_pause_sent",
                              "navigation_pause_ack", "forwarded_pause_ack", "mission_state"),
        "d16-route-hazard": ("localized", "state", "goals", "service_request", "service_reply"),
        "d16-motion-cutover": ("odometry", "velocity_command", "navigation_goals", "mission_state",
                               "stationary_window_start", "stationary_window_end", "service_reply"),
    }
    minimum = required.get(case)
    if minimum is None:
        if case not in {"fleet-rolling", "fleet-whole-route", "fleet-cancel-paused",
                        "fleet-restart-held", "fleet-restart-missing-route",
                        "fleet-restart-paused-rolling", "fleet-restart-paused-whole-route"}:
            raise ValueError(f"unknown task-control case: {case}")
        minimum = ("odometry", "traffic", "stationary_window_start", "stationary_window_end",
                   "service_request", "service_reply")
    counts = collections.Counter()
    last_stamp = 0
    with Path(path).open(encoding="utf-8") as stream:
        for line in stream:
            row = json.loads(line)
            stamp = row["monotonic_ns"]
            if stamp < last_stamp or stamp <= 0 or row["wall_ns"] <= 0:
                raise ValueError(f"{case}: invalid trace timeline")
            counts[row["event"]] += 1
            last_stamp = stamp
    missing = [event for event in minimum if counts[event] == 0]
    if missing:
        raise ValueError(f"{case}: missing trace events {', '.join(missing)}")
    if counts["stationary_window_start"] != counts["stationary_window_end"]:
        raise ValueError(f"{case}: incomplete stationary window")
    if case.startswith("fleet-restart-") and (
            counts["agent_started"] != 2 or counts["agent_stopped"] != 2):
        raise ValueError(f"{case}: missing agent process transition evidence")
    return dict(counts)


if __name__ == "__main__":
    print(json.dumps(verify_trace(sys.argv[1], sys.argv[2]), sort_keys=True))
