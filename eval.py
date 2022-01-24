#! /usr/bin/env python
import argparse
import re
import sys

from typing import Dict, IO, List, Tuple


class Event:
    time: int
    cpu: int
    event_type: str
    data: Dict[str, str]

    def __init__(self, line: str = ""):
        # placeholder event gets initialised without any arguments
        if line == "":
            self.time = -1
            self.cpu = -1
            self.event_type = "placeholder"
            return

        regex = re.compile(r"\[\d+:\d+:(?P<time>\d+\.\d+)\] \(\+\d+.\d+\) \S+ (?P<type>\S+): "
                           r"\{ cpu_id = (?P<cpu>\d) \}, \{ (?P<data>.*)\s?\}")
        re_match = regex.match(line)
        if not re_match:
            self.event_type = "no_match"
            print(f"line did not match: {line}", file=sys.stderr)
            return

        self.time = int("".join(re_match.group('time').split(".")))
        self.cpu = int(re_match.group('cpu'))
        self.event_type = re_match.group('type')

        data = re_match.group('data')
        # check, if data is provided
        if len(data) == 0:
            return
        self.data = {d.split(" = ")[0]: d.split(" = ")[1]
                     for d in data.split(", ")}


class Job:
    id: int
    submission_time: int
    deadline: int
    begin: int
    end: int
    execution_time: int

    def __init__(self, id: int, submission_time: int, relative_deadline: int):
        self.id = id
        self.submission_time = submission_time
        self.deadline = submission_time + relative_deadline
        self.begin = -1
        self.end = -1
        self.execution_time = -1


class Task:
    id: int
    pid: int
    jobs: Dict[int, Job]
    init_event: Event
    migrated_event: Event
    started_real_time_event: Event
    finished_event: Event
    aquire_sem_events: List[Event]
    aquired_sem_events: List[Event]
    sched_begin_events: List[Event]
    sched_end_events: List[Event]
    job_spawn_events: List[Event]
    begin_job_events: List[Event]
    end_job_events: List[Event]
    runtime_events: List[Event]

    def __init__(self, id: int, pid: int):
        self.id = id
        self.pid = pid
        self.jobs = {}
        self.init_event = Event()
        self.migrated_event = Event()
        self.started_real_time_event = Event()
        self.finished_event = Event()
        self.aquire_sem_events = []
        self.aquired_sem_events = []
        self.sched_begin_events = []
        self.sched_end_events = []
        self.job_spawn_events = []
        self.begin_job_events = []
        self.end_job_events = []
        self.runtime_events = []

    def add_init_event(self, event: Event) -> None:
        self.init_event = event

    def add_migrated_event(self, event: Event) -> None:
        self.migrated_event = event

    def add_started_real_time_event(self, event: Event) -> None:
        self.started_real_time_event = event

    def add_finished_event(self, event: Event) -> None:
        self.finished_event = event

    def add_aquire_sem_event(self, event: Event) -> None:
        self.aquire_sem_events.append(event)

    def add_aquired_sem_event(self, event: Event) -> None:
        self.aquired_sem_events.append(event)

    def add_sched_begin_event(self, event: Event) -> None:
        self.sched_begin_events.append(event)

    def add_sched_end_event(self, event: Event) -> None:
        self.sched_end_events.append(event)

    def add_job_spawn_event(self, event: Event) -> None:
        self.job_spawn_events.append(event)

    def add_begin_job_event(self, event: Event) -> None:
        self.begin_job_events.append(event)

    def add_end_job_event(self, event: Event) -> None:
        self.end_job_events.append(event)

    def add_runtime_event(self, event: Event) -> None:
        self.runtime_events.append(event)

    def finish_initialisation(self) -> None:
        # init jobs
        for e in self.job_spawn_events:
            self.jobs[int(e.data['job'])] = Job(int(e.data['job']), e.time,
                                                int(e.data['deadline']))

        # add begin to jobs
        for e in self.begin_job_events:
            self.jobs[int(e.data['job'])].begin = e.time

        # add end and execution_time to jobs
        for e in self.end_job_events:
            job = self.jobs[int(e.data['job'])]
            job.end = e.time
            job.execution_time = int(e.data['runtime'])

        # return if task has no jobs
        if len(self.jobs) == 0:
            return

        # get first job spawn
        jobs = list(self.jobs.values())
        jobs.sort(key=lambda j: j.submission_time)
        first_job_spawn = jobs[0].submission_time

        # filter out scheduling events that happend before first job spawn
        self.sched_begin_events = [e for e in self.sched_begin_events if e.time > first_job_spawn]
        self.sched_end_events = [e for e in self.sched_end_events if e.time > first_job_spawn]

    def print_events(self) -> None:
        events = []
        events.append((self.init_event.time, "init"))
        events.append((self.migrated_event.time, "migrated"))
        events.append((self.started_real_time_event.time, "start real time"))
        events.append((self.finished_event.time, "finished"))
        events += [(e.time, "aquire_sem") for e in self.aquire_sem_events]
        events += [(e.time, "aquired_sem") for e in self.aquired_sem_events]
        events += [(j.submission_time, f"job {j.id} spawned with deadline "
                                       f"{int(j.deadline / 1000)}")
                   for j in self.jobs.values()]
        events += [(j.deadline, f"deadline for job {j.id}") for j in self.jobs.values()]
        events += [(j.begin, f"job {j.id} started execution") for j in self.jobs.values()]
        events += [(j.end, f"job {j.id} finished execution. "
                           f"Execution time: {int(j.execution_time / 1000)}")
                   for j in self.jobs.values()]
        events += [(e.time, "sched_begin") for e in self.sched_begin_events]
        events += [(e.time, "sched_end") for e in self.sched_end_events]
        events += [(e.time, f"runtime: {e.data['runtime']}")
                   for e in self.runtime_events]

        events.sort(key=lambda e: e[0])

        for e in events:
            print(f"{int(e[0] / 1000)} {self.id} {e[1]}")

    def print_jobs(self, file: IO) -> None:
        for job in self.jobs.values():
            # TODO: calculate n_parts (hardcoded 1 for now)
            print(f"j {job.id} {int((job.end - job.deadline) / 1000)} 1", file=file)


def process_cmd_args():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('trace_report', metavar='TRACE_REPORT', type=str,
                         help='file with the filtered trace report')
    aparser.add_argument('-o', '--output', help='output file')
    return aparser.parse_args()


def parse_trace_file(trace_file: str) -> Dict[int, Task]:
    # read tracepoints into Event objects
    events: Dict[str, List[Event]]
    events = {}
    with open(trace_file) as f:
        for line in f:
            event = Event(line)
            if event.event_type not in events:
                events[event.event_type] = []
            events[event.event_type].append(event)

    # create tasks
    tasks: Dict[int, Task]
    tasks = {}
    pid_mapping: Dict[int, int]
    pid_mapping = {}
    for e in events['sched_sim:init_task']:
        task = Task(int(e.data['tid']), int(e.data['pid']))
        task.add_init_event(e)
        tasks[task.id] = task
        pid_mapping[task.pid] = task.id

    # assign migration events
    for e in events['sched_sim:migrated_task']:
        tasks[int(e.data['task'])].add_migrated_event(e)

    # assign real time start events
    for e in events['sched_sim:started_real_time_task']:
        tasks[int(e.data['task'])].add_started_real_time_event(e)

    # assign real time start events
    for e in events['sched_sim:finished_task']:
        tasks[int(e.data['task'])].add_finished_event(e)

    # assign aquire_sem events
    for e in events['sched_sim:acquire_sem']:
        tasks[int(e.data['task'])].add_aquire_sem_event(e)

    # assign aquired_sem events
    for e in events['sched_sim:acquired_sem']:
        tasks[int(e.data['task'])].add_aquired_sem_event(e)

    # assign scheduling events
    for e in events['sched_switch']:
        prev_id = int(e.data['prev_tid'])
        next_id = int(e.data['next_tid'])
        if prev_id in pid_mapping:
            tasks[pid_mapping[prev_id]].add_sched_end_event(e)
        if next_id in pid_mapping:
            tasks[pid_mapping[next_id]].add_sched_begin_event(e)

    # assign job spawns
    for e in events['sched_sim:job_spawn']:
        tasks[int(e.data['task'])].add_job_spawn_event(e)

    # assign job beginnings
    for e in events['sched_sim:begin_job']:
        tasks[int(e.data['task'])].add_begin_job_event(e)

    # assign job endings
    for e in events['sched_sim:end_job']:
        tasks[int(e.data['task'])].add_end_job_event(e)

    # assign runtime events
    for e in events['sched_stat_runtime']:
        tid = int(e.data['tid'])
        if tid in pid_mapping:
            tasks[pid_mapping[tid]].add_runtime_event(e)

    # let tasks calculate all the rest
    for t in tasks.values():
        t.finish_initialisation()

    return tasks


def main():
    args = process_cmd_args()

    tasks = parse_trace_file(args.trace_report)

    # for task in tasks.values():
    #     task.print_events()

    with open(args.output, "w+") as f:
        for task in tasks.values():
            task.print_jobs(f)
            # task.print_events()


if __name__ == "__main__":
    main()
