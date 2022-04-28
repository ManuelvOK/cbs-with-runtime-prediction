#! /usr/bin/env python3
import argparse
import re
import sys

from statistics import mean

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


def process_cmd_args():
    aparser = argparse.ArgumentParser()
    aparser.add_argument('trace_cfs_0', metavar='TRACE_CFS_0', type=str,
                         help='file with the filtered trace report for cfs execution')
    aparser.add_argument('trace_cfs_25', metavar='TRACE_CFS_25', type=str,
                         help='file with the filtered trace report for cfs execution')
    aparser.add_argument('trace_cfs_50', metavar='TRACE_CFS_50', type=str,
                         help='file with the filtered trace report for cfs execution')
    aparser.add_argument('trace_cfs_75', metavar='TRACE_CFS_75', type=str,
                         help='file with the filtered trace report for cfs execution')
    aparser.add_argument('trace_cfs_100', metavar='TRACE_CFS_100', type=str,
                         help='file with the filtered trace report for cfs execution')
    aparser.add_argument('trace_rt', metavar='TRACE_RT', type=str,
                         help='file with the filtered trace report for rt execution without \
                               prediction')
    aparser.add_argument('trace_pred', metavar='TRACE_PRED', type=str,
                         help='file with the filtered trace report for rt execution with \
                               prediction but without metrics')
    aparser.add_argument('trace_metr', metavar='TRACE_METR', type=str,
                         help='file with the filtered trace report for rt execution with \
                               prediction and metrics')
    aparser.add_argument('-o', '--output', help='output file')
    return aparser.parse_args()


def parse_trace_file(trace_file: str) -> Dict[str, List[Event]]:
    # read tracepoints into Event objects
    events: Dict[str, List[Event]]
    events = {}
    with open(trace_file) as f:
        for line in f:
            event = Event(line)
            if (event.event_type == "no_match"):
                continue
            if event.event_type not in events:
                events[event.event_type] = []
            events[event.event_type].append(event)

    return events


def print_tardiness(cfs_0_events: Dict[str, List[Event]],
                    cfs_25_events: Dict[str, List[Event]],
                    cfs_50_events: Dict[str, List[Event]],
                    cfs_75_events: Dict[str, List[Event]],
                    cfs_100_events: Dict[str, List[Event]],
                    rt_events: Dict[str, List[Event]],
                    pred_events: Dict[str, List[Event]],
                    metr_events: Dict[str, List[Event]],
                    file: IO) -> None:
    print("id,tard_cfs_0,tard_cfs_25,tard_cfs_50,tard_cfs_75,tard_cfs_100,tard_rt,tard_pred,"
          "tard_metr,pred_pred,pred_metr,rsr_exec,scale_exec,render_exec",
          file=file)

    cfs_0_tardiness = [-float(e.data['tardiness']) for e in cfs_0_events['play_video:render']]
    cfs_25_tardiness = [-float(e.data['tardiness']) for e in cfs_25_events['play_video:render']]
    cfs_50_tardiness = [-float(e.data['tardiness']) for e in cfs_50_events['play_video:render']]
    cfs_75_tardiness = [-float(e.data['tardiness']) for e in cfs_75_events['play_video:render']]
    cfs_100_tardiness = [-float(e.data['tardiness']) for e in cfs_100_events['play_video:render']]
    rt_tardiness = [-float(e.data['tardiness']) for e in rt_events['play_video:render']]
    pred_tardiness = [-float(e.data['tardiness']) for e in pred_events['play_video:render']]
    metr_tardiness = [-float(e.data['tardiness']) for e in metr_events['play_video:render']]

    pred_prediction = [float(0)] + [float(e.data['prediction'])
                                    for e in pred_events['task_lib:prediction']
                                    if int(e.data['task']) == 0]

    metr_prediction = [float(0)] + [float(e.data['prediction'])
                                    for e in metr_events['task_lib:prediction']
                                    if int(e.data['task']) == 0]

    rsr_exec = [float(e.data['runtime'])
                for e in cfs_0_events['task_lib:end_job']
                if int(e.data['task']) == 0]

    scale_exec = [float(e.data['runtime'])
                  for e in cfs_0_events['task_lib:end_job']
                  if int(e.data['task']) == 1]

    render_exec = [float(e.data['runtime'])
                   for e in cfs_0_events['task_lib:end_job']
                   if int(e.data['task']) == 2]

    for id, values in enumerate(zip(cfs_0_tardiness, cfs_25_tardiness, cfs_50_tardiness,
                                    cfs_75_tardiness, cfs_100_tardiness, rt_tardiness,
                                    pred_tardiness, metr_tardiness, pred_prediction,
                                    metr_prediction, rsr_exec, scale_exec, render_exec)):
        data_str = ",".join([str(v) for v in values])
        print(f"{id}, {data_str}", file=file)


def main():
    args = process_cmd_args()

    cfs_0_events = parse_trace_file(args.trace_cfs_0)
    cfs_25_events = parse_trace_file(args.trace_cfs_25)
    cfs_50_events = parse_trace_file(args.trace_cfs_50)
    cfs_75_events = parse_trace_file(args.trace_cfs_75)
    cfs_100_events = parse_trace_file(args.trace_cfs_100)
    rt_events = parse_trace_file(args.trace_rt)
    pred_events = parse_trace_file(args.trace_pred)
    metr_events = parse_trace_file(args.trace_metr)

    print_tardiness(cfs_0_events, cfs_25_events, cfs_50_events, cfs_75_events, cfs_100_events,
                    rt_events, pred_events, metr_events, sys.stdout)

    # decode_times = [float(e.data['duration']) for e in cfs_events['play_video:decode_next']]
    # print(f"decode times - min: {min(decode_times)} max: {max(decode_times)} mean: \
    #         {mean(decode_times)}")

    # prepare_times = [float(e.data['duration']) for e in cfs_events['play_video:prepare']]
    # print(f"prepare times - min: {min(prepare_times)} max: {max(prepare_times)} mean: \
    #         {mean(prepare_times)}")

    # render_times = [float(e.data['duration']) for e in cfs_events['play_video:render']]
    # print(f"render times - min: {min(render_times)} max: {max(render_times)} mean: \
    #         {mean(render_times)}")

    # with open(args.output, "w+") as f:
    #     for task in tasks.values():
    #         task.print_jobs(f)


if __name__ == "__main__":
    main()
