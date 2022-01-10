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
        diff = self.job_events[0][0] - self.sched_events[0][0]
        self.job_events = [(time - diff, event) for time, event in self.job_events]

    def print_events(self) -> None:
        for (t, e) in sorted(self.job_events + self.sched_events, key=lambda x: x[0]):
            print(f"{t}: {e}")


def process_cmd_args():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('trace_report', metavar='TRACE_REPORT', type=str,
                         help='file with the filtered trace report')
    aparser.add_argument('n_tasks', metavar='N_TASKS', type=int,
                         help='number of tasks')
    aparser.add_argument('-o', '--output', help='output file')
    return aparser.parse_args()


def parse_task_file(task_id: int) -> Task:
    input_file = f"{task_id}.log"
    task = Task(task_id)
    with open(input_file) as f:
        for line in f:
            if not len(line):
                continue
            parts = line.split(": ")
            if parts[0] == 'pid':
                task.pid = int(parts[1])
            else:
                task.add_job_event(int(parts[0]), parts[1][:-1])
    return task


def parse_trace_file(trace_file: str, tasks: Dict[int, Task]) -> int:
    regex = re.compile(r"\s*(\S+)-(\d+)\s+\[(\d+)\]\s+(\d+.\d+):\s+sched_switch:\s+prev_comm=(\S+)\s+prev_pid=(\d+).*next_comm=(\S+)\s+next_pid=(\d+).*")
    start_time = -1
    first_filtered = {}
    for task in tasks:
        first_filtered[task] = {"begin": False, "end": False}

    with open(trace_file) as f:
        for line in f:
            re_match = regex.match(line)
            if not re_match:
                print(f"line did not match: {line}")
                continue

            time = re_match.group(4)
            time_int = int("".join(time.split(".")))

            # save start time of scenario
            if start_time < 0:
                start_time = time_int

            time_int = time_int - start_time

            # filter out work on wrong CPU
            core = re_match.group(3)
            if core == "001":
                continue

            prev_id = int(re_match.group(6))
            next_id = int(re_match.group(8))

            if prev_id in tasks:
                # do not save first event since it is initialisation work
                if not first_filtered[prev_id]["begin"]:
                    first_filtered[prev_id]["begin"] = True
                else:
                    tasks[prev_id].add_sched_event(time_int, "exec_end")
                    print(f"{time_int}: end {prev_id} on core {re_match.group(3)}")

            if next_id in tasks:
                # do not save first event since it is initialisation work
                if not first_filtered[next_id]["end"]:
                    first_filtered[next_id]["end"] = True
                else:
                    tasks[next_id].add_sched_event(time_int, "exec_begin")
                    print(f"{time_int}: begin {next_id} on core {re_match.group(3)}")
    return start_time


def main():
    args = process_cmd_args()

    tasks = {}
    for i in range(args.n_tasks):
        task = parse_task_file(i)
        tasks[task.pid] = task

    print(tasks.keys())
    start_time = parse_trace_file(args.trace_report, tasks)

    for task in tasks.values():
        task.sync_events()

    for task in tasks.values():
        print(f"=== Task {task.id} ===")
        task.print_events()


if __name__ == "__main__":
    main()
