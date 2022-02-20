#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER sched_sim

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./sched_sim_tracepoint.h"

#if !defined(_SCHED_SIM_TP_H) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _SCHED_SIM_TP_H

#include <lttng/tracepoint.h>

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
        lttng_ust_field_integer(char, cpu, cpu_arg)
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
        lttng_ust_field_integer(char, task, task_arg)
        lttng_ust_field_integer(char, job, job_arg)
        lttng_ust_field_integer(long, deadline, deadline_arg)
    )
)


#endif /* _SCHED_SIM_TP_H */

#include <lttng/tracepoint-event.h>

