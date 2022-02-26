#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER task_lib

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./task_lib_tracepoint.h"

#if !defined(_TASK_LIB_TP_H) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _TASK_LIB_TP_H

#include <lttng/tracepoint.h>

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    init_task,
    LTTNG_UST_TP_ARGS(
        int, tid_arg,
        int, pid_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, tid, tid_arg)
        lttng_ust_field_integer(int, pid, pid_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    migrated_task,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, cpu_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
        lttng_ust_field_integer(char, cpu, cpu_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    started_real_time_task,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    acquire_sem,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    acquired_sem,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    begin_job,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
        lttng_ust_field_integer(int, job, job_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    end_job,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg,
        int, runtime_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
        lttng_ust_field_integer(int, job, job_arg)
        lttng_ust_field_integer(int, runtime, runtime_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    finished_task,
    LTTNG_UST_TP_ARGS(
        int, task_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    task_lib,
    prediction,
    LTTNG_UST_TP_ARGS(
        int, task_arg,
        int, job_arg,
        int, prediction_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(char, task, task_arg)
        lttng_ust_field_integer(int, job, job_arg)
        lttng_ust_field_integer(int, prediction, prediction_arg)
    )
)


#endif /* _TASK_LIB_TP_H */

#include <lttng/tracepoint-event.h>


