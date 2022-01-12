#! /usr/bin/env python
import argparse
import re
import sys

from typing import Dict, IO, List, Tuple


class Job:
    id: int
    submission_time: int
    deadline: int
    begin: int
    end: int
    execution_time: int

    def __init__(self, id: int):
        self.id = id
        self.submission_time = -1
        self.deadline = -1
        self.begin = -1
        self.end = -1
        self.execution_time = -1


class Task:
    id: int
    pid: int
    jobs: Dict[int, Job]
    sched_events: List[Tuple[int, str]]

    def __init__(self, id: int):
        self.id = id
        self.pid = -1
        self.jobs = {}
        self.sched_events = []

    def add_sched_event(self, time: int, event: str) -> None:
        self.sched_events.append((time, event))

    def sync_events(self) -> None:
        first_job_begin = min([job.begin for job in self.jobs.values()])
        diff = first_job_begin - self.sched_events[0][0] - 5
        for job in self.jobs.values():
            job.submission_time -= diff
            job.deadline -= diff
            job.begin -= diff
            job.end -= diff

    def print_events(self) -> None:
        job_events = []
        for job in self.jobs.values():
            job_events.append((job.begin, f"Job {job.id} started execution."))
            job_events.append((job.end, f"Job {job.id} finished execution."
                                        f" Execution_time: {job.execution_time}"))
            job_events.append((job.submission_time, f"Job {job.id} submitted."
                                                    f" Deadline: {job.deadline % 10000000}"))

        for (t, e) in sorted(self.sched_events + job_events, key=lambda x: x[0]):
            print(f"{t%10000000}: {e}")


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
            line = line[:-1]
            split = line.split(" ")
            task_id = int(split[0])
            key = split[1]

            # log does not belong to task (id is -1)
            if task_id < 0:
                continue

            # create new task if this is the first log for it
            if task_id not in tasks:
                tasks[task_id] = Task(task_id)

            # add pid or event to task
            task = tasks[task_id]
            if key == 'p':
                task.pid = int(split[2])
                continue

            time = int(split[2])
            job_id = int(split[3])

            # create job if this is the first log for it
            if job_id not in task.jobs:
                task.jobs[job_id] = Job(job_id)

            job = task.jobs[job_id]

            if key == 's':
                deadline = int(split[4])
                job.submission_time = time
                job.deadline = deadline
            elif key == 'b':
                job.begin = time
            elif key == 'e':
                execution_time = int(split[4])
                job.end = time
                job.execution_time = execution_time

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
