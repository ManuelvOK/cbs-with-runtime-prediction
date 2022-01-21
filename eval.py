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
                                        f" Execution_time: {job.execution_time}"
                                        f" Deadline: {job.deadline % 10000000}"))
            job_events.append((job.submission_time, f"Job {job.id} submitted."
                                                    f" Deadline: {job.deadline % 10000000}"))

        for (t, e) in sorted(self.sched_events + job_events, key=lambda x: x[0]):
            print(f"{t%10000000}: {e}")

    def print_jobs(self, file: IO) -> None:
        for job in self.jobs.values():
            # TODO: calculate n_parts (hardcoded 1 for now)
            print(f"j {job.id} {job.end - job.deadline} 1", file=file)


def process_cmd_args():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('trace_report', metavar='TRACE_REPORT', type=str,
                         help='file with the filtered trace report')
    aparser.add_argument('-o', '--output', help='output file')
    return aparser.parse_args()


def parse_trace_file(trace_file: str, tasks: Dict[int, Task]) -> None:
    regex = re.compile(r"\[\d+:\d+:(?P<time>\d+\.\d+)\] \(\+\d+.\d+\) \S+ (?P<type>\S+): "
                       r"\{ cpu_id = (?P<cpu>\d) \}, \{ (?P<data>.*) \}")
    first_filtered = {}
    for task in tasks:
        first_filtered[task] = {"begin": False, "end": False}

    with open(trace_file) as f:
        for line in f:
            re_match = regex.match(line)
            if not re_match:
                print(f"line did not match: {line}", file=sys.stderr)
                continue

            time = re_match.group('time')
            time = int("".join(time.split(".")))

            # filter out work on wrong CPU
            core = re_match.group('cpu')
            if core == "1":
                continue

            event_type = re_match.group('type')

            data = re_match.group('data')
            data = {d.split(" = ")[0]: d.split(" = ")[1]
                    for d in re_match.group('data').split(", ")}

            match event_type:
                case 'sched_switch':
                    print('switch')
                    # process_switch(data, tasks)
                case 'sched_sim:custom':
                    print(line)
                    # process_custom(data)
                case _:
                    pass

            # prev_id = data['prev_tid']
            # next_id = data['next_tid']

            # if prev_id in tasks:
            #     # do not save first event since it is initialisation work
            #     if not first_filtered[prev_id]["begin"]:
            #         first_filtered[prev_id]["begin"] = True
            #     else:
            #         tasks[prev_id].add_sched_event(time, "exec_end")

            # if next_id in tasks:
            #     # do not save first event since it is initialisation work
            #     if not first_filtered[next_id]["end"]:
            #         first_filtered[next_id]["end"] = True
            #     else:
            #         tasks[next_id].add_sched_event(time, "exec_begin")


def main():
    args = process_cmd_args()

    tasks = {}

    if args.trace_report != "_":
        parse_trace_file(args.trace_report, tasks)
        for task in tasks.values():
            task.sync_events()

    with open(args.output, "w+") as f:
        for task in tasks.values():
            task.print_jobs(f)
            # task.print_events()


if __name__ == "__main__":
    main()
