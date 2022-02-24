#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

#define __USE_GNU
#include <sched.h>

#include "rt.h"
#include "ctask.h"
#include "play_video_tracepoint.h"

enum {
    MAX_DECODE_LOADS = 8,
    MAX_PREPARE_LOADS = 8,
    MAX_RENDER_LOADS = 8,
    N_PICS_TO_SHOW = 3600,
};

AVFormatContext *format_context = NULL;
AVCodecContext *codec_context = NULL;
struct SwsContext *sws_context = NULL;

AVPacket *read_packet = NULL;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;


int video_stream = -1;

const AVCodec *codec = NULL;

struct decode_next_workload {
    int frame_id;
    AVFrame *frame;
    int finished;
};

struct prepare_workload {
    int frame_id;
    AVFrame *frame;
    AVFrame *scaled_frame;
    uint8_t *buffer;
    SDL_Texture *texture;
    int finished;
};

struct render_workload {
    int frame_id;
    SDL_Texture *texture;
    double t_to_show;
    int finished;
};

static double now() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    double t_now = t.tv_nsec;
    return t_now + t.tv_sec * 1000 * 1000 * 1000;
}

static double thread_now() {
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    double t_now = t.tv_nsec;
    return t_now + t.tv_sec * 1000 * 1000 * 1000;
}


static int init_player(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Please provide a sourcefile.\n");
        return -1;
    }
    char *input_file = argv[1];

    /* init SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Error initialising SDL: %s\n", SDL_GetError());
        return -1;
    }

    /* open input file */
    if (avformat_open_input(&format_context, input_file, NULL, NULL) < 0) {
        fprintf(stderr, "Error opening input file %s\n", input_file);
        return -1;
    }

    /* get header info */
    if (avformat_find_stream_info(format_context, NULL) < 0) {
        fprintf(stderr, "Error finding stream information %s\n", input_file);
        return -1;
    }

    av_dump_format(format_context, 0, input_file, 0);

    /* find video stream */
    for (unsigned i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = i;
            break;
        }
    }
    if (video_stream == -1) {
        fprintf(stderr, "Error finding video stream.");
        return -1;
    }

    /* init codec */
    codec = avcodec_find_decoder(format_context->streams[video_stream]->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Error: Codec not supported.\n");
        return -1;
    }

    codec_context = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_context,
                                      format_context->streams[video_stream]->codecpar)) {
        fprintf(stderr, "Error copying codec context.\n");
        return -1;
    }

    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        fprintf(stderr, "Error opening codec.\n");
        return -1;
    }

    /* init SDL window */
    window = SDL_CreateWindow("Play Video", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            codec_context->width, codec_context->height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "Error setting video mode.\n");
        exit(-1);
    }

    SDL_GL_SetSwapInterval(1);

    /* init SDL renderer */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
                                            | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        fprintf(stderr, "Error creating renderer.\n");
        exit(-1);
    }

    sws_context = sws_getContext(codec_context->width, codec_context->height,
                                 codec_context->pix_fmt,
                                 codec_context->width, codec_context->height,
                                 AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 NULL, NULL, NULL);


    /* allocate read input packet */
    read_packet = av_packet_alloc();
    if (read_packet == NULL) {
        fprintf(stderr, "Error allocating packet\n");
        exit(-1);
    }

    return 0;
}

static void decode_next(void *workload) {
    static int packet_read = 0;
    double t_begin = thread_now();
    struct decode_next_workload *load = workload;


    /* read and send packets until there is a frame to receive */
    while (1) {
        /* there might be a packet from a previous job. If not, read new one */
        if (!packet_read) {
            av_packet_unref(read_packet);
            int ret = av_read_frame(format_context, read_packet);

            if (ret < 0) {
                fprintf(stderr, "Error reading packet.\n");
                return;
            }
            if (read_packet->stream_index != video_stream) {
                continue;
            }
            packet_read = 1;
        }

        /* send_packet returns EAGAIN if the provided packet can not be decoded before receive_frame
         * gets called */
        int ret = avcodec_send_packet(codec_context, read_packet);
        if (ret == AVERROR(EAGAIN)) {
            break;
        } else {
            packet_read = 0;
        }
        if (ret < 0) {
            fprintf(stderr, "Error sending packet for decoding.\n");
            exit(-1);
        }
    }

    int ret = avcodec_receive_frame(codec_context, load->frame);
    if (ret == AVERROR(EAGAIN)) {
        fprintf(stderr, "No image to process. This should not happen.\n");
        exit(-1);
    }
    if (ret < 0) {
        fprintf(stderr, "Error while decoding.\n");
        exit(-1);
    }


    lttng_ust_tracepoint(play_video, decode_next, thread_now() - t_begin);
    //printf("%10.0f: %4d - decoding (%c) took %.0fns\n", now(), load->frame_id,
    //       av_get_picture_type_char(load->frame->pict_type), thread_now() - t_begin);

    load->finished = 1;
}

static void prepare(void *prepare_workload) {
    double t_begin = thread_now();
    struct prepare_workload *load = prepare_workload;


    /* scale */
    sws_scale(sws_context, (uint8_t const * const *)load->frame->data, load->frame->linesize, 0,
              codec_context->height, load->scaled_frame->data, load->scaled_frame->linesize);

    /* prepare texture to render */
    SDL_Rect rect = {0, 0, codec_context->width, codec_context->height};
    SDL_UpdateYUVTexture(load->texture, &rect, load->scaled_frame->data[0],
                         load->scaled_frame->linesize[0], load->scaled_frame->data[1],
                         load->scaled_frame->linesize[1], load->scaled_frame->data[2],
                         load->scaled_frame->linesize[2]);
    lttng_ust_tracepoint(play_video, prepare, thread_now() - t_begin);
    //printf("%10.0f: %4d - preparing took %.0fns\n", now(), load->frame_id, thread_now() - t_begin);

    av_frame_free(&load->frame);
    av_free(load->frame);
    load->finished = 1;
}

static void render(void *render_workload) {
    static int n_pics_shown = 0;
    double t_begin = thread_now();
    double t_begin_global = now();
    struct render_workload *load = render_workload;

    double t_until_next_pic = load->t_to_show - now();

    /* sleep until there is less then 2ms time left */
    if (t_until_next_pic / 1000 / 1000 >= 2) {
        //printf("%10.0f: %4d - sleep for %.0fms\n", now(), load->frame_id, t_until_next_pic / 1000 / 1000 - 1);
        SDL_Delay(t_until_next_pic / 1000 / 1000 - 2);
        t_until_next_pic = load->t_to_show - now();
    } else {
        //printf("%10.0f: %4d - do not sleep because frame has to be shown in %.0fns\n", now(), load->frame_id, t_until_next_pic);
    }

    /* busily wait until there is only 1us remaining */
    while (t_until_next_pic > 1000) {
        t_until_next_pic = load->t_to_show - now();
    }

    if (t_until_next_pic < 0) {
        //printf("%10.0f: %4d - Just a little late: %.0fus\n", now(), load->frame_id, -t_until_next_pic / 1000);
    }


    if (n_pics_shown < 25 || n_pics_shown % 25 == 0) {
        printf("Rendered pic %d\n", n_pics_shown);
    }
    SDL_RenderClear(renderer);
    //printf("%10.0f: %4d - RenderClear finished\n", now(), load->frame_id);
    SDL_RenderCopy(renderer, load->texture, NULL, NULL);
    //printf("%10.0f: %4d - RenderCopy finished\n", now(), load->frame_id);
    SDL_RenderPresent(renderer);
    //printf("%10.0f: %4d - RenderPresent finished\n", now(), load->frame_id);


    lttng_ust_tracepoint(play_video, render, thread_now() - t_begin, t_until_next_pic);
    //printf("%10.0f: %4d - rendering took %.0fns (%.0fns global)\n", now(), load->frame_id, thread_now() - t_begin,
    //     now() - t_begin_global);

    ++n_pics_shown;
    load->finished = 1;
}


int main(int argc, char **argv) {
    lttng_ust_tracepoint(play_video, start_main);

    /* initialise before pinning to a CPU to allow SDL threads to go everywhere */
    if (init_player(argc, argv)) {
        fprintf(stderr, "Initialisation error\n");
        exit(-1);
    }

    /* put the job spawning onto CPU 7 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(7,&set);

    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

    if (argc > 2 && strcmp(argv[2],"cfs") != 0) {
        /* configure real-time scheduling */
        struct sched_attr attr;
        unsigned int flags = 0;

        attr.size = sizeof(attr);
        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = SCHED_FIFO;

        attr.sched_policy = SCHED_FIFO;

        if (sched_setattr(0, &attr, flags) < 0) {
            perror("sched_setattr");
            exit(-1);
        }
    }

    double fps = av_q2d(format_context->streams[video_stream]->r_frame_rate);
    double frame_period = 1.0/fps * 1000 * 1000 * 1000;

    int decode_task;
    int prepare_task;
    int render_task;

    if (argc < 3 || strcmp(argv[2],"cfs") == 0) {
        /* non-rt tasks */
        decode_task = create_non_rt_task(127, 0, decode_next);
        prepare_task = create_non_rt_task(127, 1, prepare);
        render_task = create_non_rt_task(127, 3, render);
    } else if (strcmp(argv[2],"rt") == 0) {
        /* rt tasks without prediction */
        decode_task = create_task(127, 0, frame_period, decode_next, 9291982);
        prepare_task = create_task(127, 1, frame_period, prepare, 998915);
        render_task = create_task(127, 3, frame_period, render, 2521469);
    } else {
        /* rt tasks with prediction */
        decode_task = create_task_with_prediction(127, 0, frame_period, decode_next, NULL);
        prepare_task = create_task_with_prediction(127, 1, frame_period, prepare, NULL);
        render_task = create_task_with_prediction(127, 3, frame_period, render, NULL);
    }
    /* wait for tasks to init */
    SDL_Delay(10);

    struct decode_next_workload decode_loads[MAX_DECODE_LOADS];
    int first_decode_load = 0;

    /* initialise all read jobs and add them to the read task */
    for (int i = 0; i < MAX_DECODE_LOADS; ++i) {
        struct decode_next_workload *load = &decode_loads[i];
        load->frame_id = i;
        /* init frame */
        load->frame = av_frame_alloc();
        if (load->frame == NULL) {
            fprintf(stderr, "Error allocating frame.\n");
            exit(-1);
        }
        load->finished = 0;
        /* start job */
        add_job_to_task(decode_task, load);
        //printf("%10.0f: %4d - submit decode job\n", now(), i);
    }

    struct prepare_workload prepare_loads[MAX_DECODE_LOADS];
    int first_prepare_load = 0;
    int n_prepare_loads = 0;

    /* initialise all prepare loads */
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_context->width,
                                            codec_context->height, 32);
    for (int i = 0; i < MAX_PREPARE_LOADS; ++i) {
        struct prepare_workload *load = &prepare_loads[i];
        /* The input frame does not have to be initialised because it comes as input.
         * Just init scaled_frame. */
        load->frame = NULL;
        load->buffer = av_malloc(numBytes * sizeof(uint8_t));
        load->scaled_frame = av_frame_alloc();
        av_image_fill_arrays(load->scaled_frame->data, load->scaled_frame->linesize, load->buffer,
                             AV_PIX_FMT_YUV420P, codec_context->width, codec_context->height, 32);
        /* init texture */
        load->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          codec_context->width, codec_context->height);
        load->finished = 0;
    }

    struct render_workload render_loads[MAX_RENDER_LOADS];
    int first_render_load = 0;
    int n_render_loads = 0;

    /* initialise all render loads */
    for (int i = 0; i < MAX_RENDER_LOADS; ++i) {
        render_loads[i].texture = NULL;
        render_loads[i].t_to_show = 0;
        render_loads[i].finished = 0;
    }

    double t_next_pic = now() + 10 * frame_period;
    int n_pics_started = 32;
    int running = 1;
    while (running && n_pics_started < N_PICS_TO_SHOW) {
        /* check for quit */
        SDL_Event event;
        while (0 != SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT
                || (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_Q)) {
                running = 0;
            }
        }

        /* check if decode job finished */
        struct decode_next_workload *decode_load = &decode_loads[first_decode_load];
        if (decode_load->finished && n_prepare_loads < MAX_PREPARE_LOADS) {
            /* first decode load gets used, so increment index */
            ++first_decode_load;
            first_decode_load %= MAX_DECODE_LOADS;

            /* prepare prepare job */
            int next_prepare_load = (first_prepare_load + n_prepare_loads) % MAX_PREPARE_LOADS;
            struct prepare_workload *prepare_load = &prepare_loads[next_prepare_load];
            prepare_load->frame_id = decode_load->frame_id;
            prepare_load->frame = decode_load->frame;
            prepare_load->finished = 0;
            ++n_prepare_loads;
            /* start prepare job */
            add_job_to_task(prepare_task, prepare_load);
            //printf("%10.0f: %4d - submit prepare job\n", now(), prepare_load->frame_id);

            /* prepare read job */
            decode_load->frame_id = n_pics_started;
            decode_load->frame = av_frame_alloc();
            if (decode_load->frame == NULL) {
                fprintf(stderr, "Error allocating frame.\n");
                exit(-1);
            }
            decode_load->finished = 0;

            /* start decode job */
            add_job_to_task(decode_task, decode_load);
            //printf("%10.0f: %4d - submit decode job\n", now(), decode_load->frame_id);
            //printf("==== start prepare job ====\n"
            //       "%d of %d prepare loads\n"
            //       "%d of %d render loads\n",
            //       n_prepare_loads, MAX_PREPARE_LOADS, n_render_loads, MAX_RENDER_LOADS);
            ++n_pics_started;
        }

        /* check if prepare job finished */
        struct prepare_workload *prepare_load = &prepare_loads[first_prepare_load];
        if (n_prepare_loads && prepare_load->finished && n_render_loads < MAX_RENDER_LOADS) {
            /* first prepare load gets used, so increment index and decrement count */
            ++first_prepare_load;
            first_prepare_load %= MAX_PREPARE_LOADS;
            --n_prepare_loads;

            /* prepare render load */
            int next_render_load = (first_render_load + n_render_loads) % MAX_RENDER_LOADS;
            struct render_workload *render_load = &render_loads[next_render_load];
            render_load->frame_id = prepare_load->frame_id;
            render_load->texture = prepare_load->texture;
            render_load->t_to_show = t_next_pic;
            render_load->finished = 0;
            ++n_render_loads;
            t_next_pic += frame_period;

            /* start render job */
            add_job_to_task(render_task, render_load);
            //printf("%10.0f: %4d - submit render job\n", now(), render_load->frame_id);
            //printf("==== start render job ====\n"
            //       "%d of %d prepare loads\n"
            //       "%d of %d render loads\n",
            //       n_prepare_loads, MAX_PREPARE_LOADS, n_render_loads, MAX_RENDER_LOADS);
        }


        struct render_workload *render_load = &render_loads[first_render_load];
        if (n_render_loads && render_load->finished) {
            ++first_render_load;
            first_render_load %= MAX_RENDER_LOADS;
            --n_render_loads;

            //printf("==== render job finished ====\n"
            //       "%d of %d prepare loads\n"
            //       "%d of %d render loads\n",
            //       n_prepare_loads, MAX_PREPARE_LOADS, n_render_loads, MAX_RENDER_LOADS);
        }
        SDL_Delay(1);
    }

    /* join tasks */
    release_sem(decode_task);
    join_task(decode_task);

    release_sem(prepare_task);
    join_task(prepare_task);

    release_sem(render_task);
    join_task(render_task);

    /* Cleanup */
    for (int i = 0; i < MAX_DECODE_LOADS; ++i) {
        struct decode_next_workload *load = &decode_loads[i];
        av_frame_free(&load->frame);
        av_free(load->frame);
    }

    for (int i = 0; i < MAX_PREPARE_LOADS; ++i) {
        struct prepare_workload *load = &prepare_loads[i];
        av_frame_free(&load->scaled_frame);
        av_free(load->scaled_frame);
        av_free(load->buffer);
        SDL_DestroyTexture(load->texture);
    }

    avcodec_close(codec_context);
    avformat_close_input(&format_context);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}
