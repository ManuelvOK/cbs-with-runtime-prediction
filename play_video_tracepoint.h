#undef LTTNG_UST_TRACEPOINT_PROVIDER
#define LTTNG_UST_TRACEPOINT_PROVIDER play_video

#undef LTTNG_UST_TRACEPOINT_INCLUDE
#define LTTNG_UST_TRACEPOINT_INCLUDE "./play_video_tracepoint.h"

#if !defined(_PLAY_VIDEO_TP_H) || defined(LTTNG_UST_TRACEPOINT_HEADER_MULTI_READ)
#define _PLAY_VIDEO_TP_H

#include <lttng/tracepoint.h>

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
    start_main,
    LTTNG_UST_TP_ARGS(
    ),
    LTTNG_UST_TP_FIELDS(
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
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

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
    read_packet,
    LTTNG_UST_TP_ARGS(
        float, duration_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_float(double, duration, duration_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
    decode_frame,
    LTTNG_UST_TP_ARGS(
        float, duration_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_float(double, duration, duration_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
    update_texture,
    LTTNG_UST_TP_ARGS(
        float, duration_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_float(double, duration, duration_arg)
    )
)

LTTNG_UST_TRACEPOINT_EVENT(
    play_video,
    render,
    LTTNG_UST_TP_ARGS(
        float, tardiness_arg
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_float(double, tardiness, tardiness_arg)
    )
)

#endif /* _PLAY_VIDEO_TP_H */

#include <lttng/tracepoint-event.h>


