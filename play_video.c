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

AVFormatContext *format_context = NULL;
AVCodecContext *codec_context = NULL;
struct SwsContext *sws_context = NULL;

int video_stream = -1;

uint8_t *buf = NULL;

const AVCodec *codec = NULL;

SDL_Window *screen = NULL;
SDL_Renderer *renderer = NULL;

struct read_workload {
    AVFormatContext *format_context;
    AVPacket *packet;
    int finished;
};

struct pic {
    AVFrame *pic;
    int pic_number;
};

struct decode_workload {
    AVCodecContext *codec_context;
    AVPacket *packet;
    struct pic pics[32];
    int n_pics;
    int finished;
};

struct texture_workload {
    struct pic pic;
    SDL_Texture *texture;
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
    screen = SDL_CreateWindow("Play Video", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              codec_context->width, codec_context->height,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!screen) {
        fprintf(stderr, "Error setting video mode.\n");
        return -1;
    }

    SDL_GL_SetSwapInterval(1);

    /* init SDL renderer */
    renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
                                              | SDL_RENDERER_TARGETTEXTURE);

    sws_context = sws_getContext(codec_context->width, codec_context->height,
                                 codec_context->pix_fmt,
                                 codec_context->width, codec_context->height,
                                 AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 NULL, NULL, NULL);

    int n_bytes;
    n_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_context->width,
                                       codec_context->height, 32);
    buf = av_malloc(n_bytes * sizeof(uint8_t));

    return 0;
}

static struct read_workload init_read_load() {
    struct read_workload ret = {.format_context = format_context,
                                .packet = NULL,
                                .finished = 0};
    return ret;
}

static void read_packet(void *workload) {
    double t_begin = thread_now();
    struct read_workload *load = workload;

    /* allocate packet */
    load->packet = NULL;
    load->packet = av_packet_alloc();
    if (!load->packet) {
        return;
    }

    while (!load->finished) {
        /* read frame */
        av_read_frame(load->format_context, load->packet);

        /* discard packet if it is not from the vide stream */
        if (load->packet->stream_index != video_stream) {
            continue;
        }

        load->finished = 1;
    }
    lttng_ust_tracepoint(play_video, read_packet, thread_now() - t_begin);
}

static struct decode_workload init_decode_load(AVPacket *packet) {
    struct decode_workload ret = {.codec_context = codec_context,
                                  .packet = packet,
                                  .n_pics = 0,
                                  .finished = 0};
    return ret;
}

struct metrics decode_metrics(void *workload) {
    struct decode_workload *load = workload;
    struct metrics metrics;
    metrics.size = 1;
    metrics.data = malloc(metrics.size * sizeof(*metrics.data));

    metrics.data[0] = load->packet->size;
    return metrics;
}

static void decode_frame(void *workload) {
    double t_begin = thread_now();
    static int n = 0;
    struct decode_workload *load = workload;

    int ret = avcodec_send_packet(load->codec_context, load->packet);
    load->n_pics = 0;
    AVFrame *frame = av_frame_alloc();

    while(ret >= 0) {

        /* get frame */
        ret = avcodec_receive_frame(codec_context, frame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error while decoding.\n");
            return;
        }

        /* init new pic */
        AVFrame *pic = av_frame_alloc();
        av_image_fill_arrays(pic->data, pic->linesize, buf, AV_PIX_FMT_YUV420P,
                             load->codec_context->width, load->codec_context->height, 32);

        /* convert image to YUV420 */
        sws_scale(sws_context, (uint8_t const * const *)frame->data, frame->linesize,
                  0, codec_context->height, pic->data, pic->linesize);


        //printf("Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d,"
        //       " display_picture_number %d]\n",
        //       av_get_picture_type_char(frame->pict_type), codec_context->frame_number,
        //       frame->pts, frame->pkt_dts, frame->key_frame,
        //       frame->coded_picture_number, frame->display_picture_number);

        load->pics[load->n_pics].pic = pic;
        load->pics[load->n_pics].pic_number = codec_context->frame_number;

        ++load->n_pics;

        lttng_ust_tracepoint(play_video, decode_frame, thread_now() - t_begin);
    }

    /* clean up */
    av_frame_free(&frame);
    av_free(frame);
    av_packet_unref(load->packet);

    n++;
    load->finished = 1;
}

static struct texture_workload init_texture_load() {
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                             SDL_TEXTUREACCESS_STREAMING, codec_context->width,
                                             codec_context->height);

    struct texture_workload ret = {.texture = texture,
                                   .finished = 0};
    return ret;
}

struct metrics texture_metrics(void *workload) {
    struct texture_workload *load = workload;
    struct metrics metrics;
    metrics.size = 3;
    metrics.data = malloc(metrics.size * sizeof(*metrics.data));

    metrics.data[0] = load->pic.pic->linesize[0];
    metrics.data[1] = load->pic.pic->linesize[1];
    metrics.data[2] = load->pic.pic->linesize[2];
    return metrics;
}

static void update_texture(void *texture_workload) {
    double t_begin = thread_now();
    struct texture_workload *load = texture_workload;


    SDL_Rect rect = {0, 0, codec_context->width, codec_context->height};

    SDL_UpdateYUVTexture(load->texture, &rect, load->pic.pic->data[0], load->pic.pic->linesize[0],
                         load->pic.pic->data[1], load->pic.pic->linesize[1], load->pic.pic->data[2],
                         load->pic.pic->linesize[2]);

    /* clean up pic */
    av_frame_free(&load->pic.pic);
    av_free(load->pic.pic);
    load->pic.pic = NULL;
    load->finished = 1;
    lttng_ust_tracepoint(play_video, update_texture, thread_now() - t_begin);
}


int main(int argc, char **argv) {
    lttng_ust_tracepoint(play_video, start_main);

    /* put the job spawning onto CPU 7 */
    cpu_set_t set;
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

    if (init_player(argc, argv)) {
        fprintf(stderr, "Initialisation error\n");
        exit(-1);
    }

    double fps = av_q2d(format_context->streams[video_stream]->r_frame_rate);
    double frame_period = 1.0/fps * 1000 * 1000 * 1000;

    int read_task;
    int decode_task;
    int texture_task;

    if (argc < 3 || strcmp(argv[2],"cfs") == 0) {
        /* non-rt tasks */
        int read_task = create_non_rt_task(127, 0, read_packet);
        int decode_task = create_non_rt_task(127, 1, decode_frame);
        int texture_task = create_non_rt_task(127, 2, update_texture);
    } else if (strcmp(argv[2],"rt") == 0) {
        /* rt tasks without prediction */
        read_task = create_task(127, 0, frame_period, read_packet, 34703);
        decode_task = create_task(127, 1, frame_period, decode_frame, 9613976);
        texture_task = create_task(127, 2, frame_period, update_texture, 489318);
    } else if (strcmp(argv[2],"ret") == 0) {
        /* rt tasks with prediction */
        read_task = create_task_with_prediction(127, 0, frame_period, read_packet, NULL);
        decode_task = create_task_with_prediction(127, 1, frame_period, decode_frame, NULL);
        texture_task = create_task_with_prediction(127, 2, frame_period, update_texture, NULL);
    } else {
        /* rt tasks with prediction and metrics */
        read_task = create_task_with_prediction(127, 0, frame_period, read_packet, NULL);
        decode_task = create_task_with_prediction(127, 1, frame_period, decode_frame, decode_metrics);
        texture_task = create_task_with_prediction(127, 2, frame_period, update_texture, texture_metrics);
    }

    struct read_workload read_loads[32];
    int first_read_load = 0;

    /* initialise all 32 read jobs */
    for (int i = 0; i < 32; ++i) {
        read_loads[i] = init_read_load();
    }

    /* add 32 jobs to read task */
    for (int i = 0; i < 32; ++i) {
        add_job_to_task(read_task, &read_loads[i]);
        release_sem(read_task);
    }

    struct decode_workload decode_loads[32];
    int first_decode_load = 0;
    int n_decode_loads = 0;

    struct pic pics[32];
    int first_pic = 0;
    int n_pics = 0;

    struct texture_workload texture_load;
    int n_texture_loads = 0;
    texture_load = init_texture_load();

    double t_next_pic = now();
    int n_pics_shown = 0;
    int did_sleep = 0;
    while (n_pics_shown < 3600) {
        /* check for quit */
        SDL_Event event;
        SDL_PollEvent(&event);
        if (event.type == SDL_QUIT
            || (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_Q)) {
            break;
        }

        /* check if read job finished */
        struct read_workload *read_load = &read_loads[first_read_load];
        if (read_load->finished && n_decode_loads < 32) {
            /* init new decode job */
            int next_decode_load = (first_decode_load + n_decode_loads) % 32;
            decode_loads[next_decode_load] = init_decode_load(read_load->packet);
            add_job_to_task(decode_task, &decode_loads[next_decode_load]);
            release_sem(decode_task);
            ++n_decode_loads;

            /* init new read job */
            *read_load = init_read_load();
            add_job_to_task(read_task, read_load);
            release_sem(read_task);

            ++first_read_load;
            first_read_load %= 32;
        }

        /* check if decode job finished */
        struct decode_workload *decode_load = &decode_loads[first_decode_load];
        if (n_decode_loads && decode_load->finished
            && n_pics + decode_load->n_pics <= 32) {
            for (int i = 0; i < decode_load->n_pics; ++i) {
                /* save decoded picture */
                int next_pic = (first_pic + n_pics) % 32;
                pics[next_pic] = decode_load->pics[i];

                ++n_pics;
            }

            ++first_decode_load;
            first_decode_load %= 32;
            --n_decode_loads;
        }

        /* check if texture task is ready and there is a pic to texture */
        if (!n_texture_loads && n_pics) {
            /* start texture job */
            texture_load.finished = 0;
            texture_load.pic = pics[first_pic];
            add_job_to_task(texture_task, &texture_load);
            release_sem(texture_task);

            ++first_pic;
            first_pic %= 32;
            --n_pics;
            ++n_texture_loads;
        }

        double t_until_next_pic = t_next_pic - now();
        /* sleep 1ms until there is less then 2ms time left */
        if (t_until_next_pic >= 2000 * 1000) {
            SDL_Delay(1);
            did_sleep = 1;
            continue;
        }

        /* check if texture job finished */
        if (n_texture_loads && texture_load.finished) {
            int did_wait = 0;
            /* reset timing if at start */
            if (!n_pics_shown) {
                t_next_pic = now();
                t_until_next_pic = 0;
            } else {
                /* busily wait until there is only 1us remaining */
                while (t_until_next_pic > 1 * 1000) {
                    t_until_next_pic = t_next_pic - now();
                    did_wait = 1;
                }
            }

            t_next_pic += frame_period;


            ///* reset if more than 5 frames late */
            //if (-t_until_next_pic > 5 * frame_period) {
            //    printf("Resetting timing because of frame %d being %.0fus too late.\n",
            //           texture_load.pic.pic_number, -t_until_next_pic / 1000);
            //    t_next_pic = now() + frame_period;
            //    t_until_next_pic = 0;
            //}

            /* reset timing if too late */
            //if (-t_until_next_pic > frame_period) {
            //    printf("Resetting timing because of frame %d being %.0fus too late.\n",
            //           texture_load.pic.pic_number, -t_until_next_pic / 1000);
            //    t_next_pic = now() + frame_period;
            //    t_until_next_pic = 0;
            //}

            //if (t_until_next_pic < 0) {
            //    printf("did_sleep? %d\t", did_sleep);
            //    printf("did_wait? %d\t", did_wait);
            //    printf("Just a little late: %.0fus\n", -t_until_next_pic / 1000);
            //}



            did_sleep = 0;

            lttng_ust_tracepoint(play_video, render, t_until_next_pic);
            if (n_pics_shown < 25 || n_pics_shown % 25 == 0) {
                printf("Rendered pic %d\n", n_pics_shown);
            }
            //SDL_RenderClear(renderer);
            //SDL_RenderCopy(renderer, texture_load.texture, NULL, NULL);
            //SDL_RenderPresent(renderer);
            //printf("Rendering took %.0fus\n", (thread_now() - t_begin) / 1000);

            --n_texture_loads;
            ++n_pics_shown;
            continue;
        }
    }

    /* destroy texture */
    SDL_DestroyTexture(texture_load.texture);

    /* join tasks */
    release_sem(read_task);
    join_task(read_task);

    release_sem(decode_task);
    join_task(decode_task);

    /* Cleanup */
    av_free(buf);
    avcodec_close(codec_context);
    avformat_close_input(&format_context);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}
