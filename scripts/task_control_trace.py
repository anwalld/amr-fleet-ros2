"""Write time-correlated ROS evidence for a single isolated control scenario."""

import atexit
import json
import os
import threading
import time

from rosidl_runtime_py.convert import message_to_ordereddict


def json_value(value):
    to_list = getattr(value, "tolist", None)
    if to_list is not None:
        return to_list()
    item = getattr(value, "item", None)
    if item is not None:
        return item()
    if isinstance(value, (bytes, bytearray)):
        return list(value)
    raise TypeError(f"unsupported ROS trace value {type(value).__name__}")


class TaskControlTrace:
    def __init__(self):
        path = os.environ.get("TASK_CONTROL_TRACE")
        self.stream = open(path, "x", encoding="utf-8", buffering=1) if path else None
        self.lock = threading.Lock()
        if self.stream:
            atexit.register(self.stream.close)

    def record(self, event, **fields):
        if self.stream:
            with self.lock:
                row = {"wall_ns": time.time_ns(), "monotonic_ns": time.monotonic_ns(),
                       "event": event, **fields}
                self.stream.write(json.dumps(row, ensure_ascii=False, default=json_value) + "\n")

    def message(self, event, message, **fields):
        if self.stream:
            self.record(event, data=message_to_ordereddict(message), **fields)
