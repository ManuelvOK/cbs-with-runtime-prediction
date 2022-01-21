#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER sched_sim

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./sched_sim_tracepoint.h"

#if !defined(_HELLO_TP_H) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _HELLO_TP_H

#include <lttng/tracepoint.h>

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    custom,
    LTTNG_UST_TP_ARGS(
        char *, my_string_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_string(my_string_field, my_string_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    start_main,
    LTTNG_UST_TP_ARGS(
    ),
    LTTNG_UST_TP_FIELDS(
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    input_parsed,
    LTTNG_UST_TP_ARGS(
    ),
    LTTNG_UST_TP_FIELDS(
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    waited_for_task_init,
    LTTNG_UST_TP_ARGS(
    ),
    LTTNG_UST_TP_FIELDS(
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    migrated,
    LTTNG_UST_TP_ARGS(
        int, cpu_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, cpu_field, cpu_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    job_spawn,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg,
        int, deadline_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
        lttng_ust_field_integer(char, job_field, job_arg)
        lttng_ust_field_integer(long, deadline_field, deadline_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    init_task,
    LTTNG_UST_TP_ARGS(
        int, tid_arg,
        int, pid_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, tid_field, tid_arg)
        lttng_ust_field_integer(char, pid_field, pid_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    migrated_task,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, cpu_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
        lttng_ust_field_integer(char, cpu_field, cpu_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    started_real_time_task,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    acquire_sem,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    acquired_sem,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    begin_job,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
        lttng_ust_field_integer(char, job_field, job_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    sched_sim,
    end_job,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg,
        int, runtime_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task_field, task_arg)
        lttng_ust_field_integer(char, job_field, job_arg)
        lttng_ust_field_integer(int, runtime_field, runtime_arg)
    )
)


#endif /* _HELLO_TP_H */

#include <lttng/tracepoint-event.h>

