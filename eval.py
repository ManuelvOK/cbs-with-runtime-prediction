#! /usr/bin/env python
import argparse
import re
import sys

from typing import Dict, IO, List, Tuple


class Task:
    id: int
    pid: int
    sched_events: List[Tuple[int, str]]
    job_events: List[Tuple[int, str]]

    def __init__(self, id: int):
        self.id = id
        self.pid = -1
        self.sched_events = []
        self.job_events = []

    def add_sched_event(self, time: int, event: str) -> None:
        self.sched_events.append((time, event))

    def add_job_event(self, time: int, event: str) -> None:
        self.job_events.append((time, event))

    def sync_events(self) -> None:
        diff = self.job_events[0][0] - self.sched_events[0][0] - 10
        self.job_events = [(time - diff, event) for time, event in self.job_events]

    def print_events(self) -> None:
        for (t, e) in sorted(self.job_events + self.sched_events, key=lambda x: x[0]):
            print(f"{t}: {e}")


def process_cmd_args():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('trace_report', metavar='TRACE_REPORT', type=str,
                         help='file with the filtered trace report')
    aparser.add_argument('log_file', metavar='LOG_FILE', type=str,
                         help='file with the main applications output')
    aparser.add_argument('-o', '--output', help='output file')
    return aparser.parse_args()


def parse_log_file(log_file: str) -> Dict[int, Task]:
    tasks = {}
    with open(log_file) as f:
        for line in f:
            if not len(line):
                continue

            split = line.split(":")
            task_id = int(split[0])
            key = split[1]
            value = split[2]

            # log does not belong to task (id is -1)
            if task_id < 0:
                continue

            # create new task if this is the first log for this task
            if task_id not in tasks:
                tasks[task_id] = Task(task_id)

            # add pid or event to task
            task = tasks[task_id]
            if key == 'pid':
                task.pid = int(value)
            else:
                task.add_job_event(int(key), value[:-1])

    return {t.pid: t for t in tasks.values()}


def parse_trace_file(trace_file: str, tasks: Dict[int, Task]) -> None:
    regex = re.compile(r".+-\d+\s+\[(\d+)\]\s+(\d+.\d+):\s+sched_switch:.+\sprev_pid=(\d+).+"
                       r"next_pid=(\d+).*")
    first_filtered = {}
    for task in tasks:
        first_filtered[task] = {"begin": False, "end": False}

    with open(trace_file) as f:
        for line in f:
            re_match = regex.match(line)
            if not re_match:
                print(f"line did not match: {line}", file=sys.stderr)
                continue

            time = re_match.group(2)
            time_int = int("".join(time.split(".")))

            time_int = time_int

            # filter out work on wrong CPU
            core = re_match.group(1)
            if core == "001":
                continue

            prev_id = int(re_match.group(3))
            next_id = int(re_match.group(4))

            if prev_id in tasks:
                # do not save first event since it is initialisation work
                if not first_filtered[prev_id]["begin"]:
                    first_filtered[prev_id]["begin"] = True
                else:
                    tasks[prev_id].add_sched_event(time_int, "exec_end")

            if next_id in tasks:
                # do not save first event since it is initialisation work
                if not first_filtered[next_id]["end"]:
                    first_filtered[next_id]["end"] = True
                else:
                    tasks[next_id].add_sched_event(time_int, "exec_begin")


def main():
    args = process_cmd_args()

    tasks = parse_log_file(args.log_file)

    parse_trace_file(args.trace_report, tasks)

    for task in tasks.values():
        task.sync_events()

    for task in tasks.values():
        print(f"=== Task {task.id} ===")
        task.print_events()


if __name__ == "__main__":
    main()
