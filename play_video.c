#include <unistd.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>

#define __USE_GNU
#include <sched.h>

#include "rt.h"
#include "ctask.h"

AVFormatContext *format_context = NULL;
AVCodecContext *codec_context = NULL;
struct SwsContext *sws_context = NULL;

int video_stream = -1;

AVCodec *codec = NULL;

SDL_Window *screen = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;


int init_player(int argc, char **argv) {
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
    for (int i = 0; i < format_context->nb_streams; i++) {
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

    /* init SDL texture */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                                codec_context->width, codec_context->height);

    sws_context = sws_getContext(codec_context->width, codec_context->height,
                                 codec_context->pix_fmt,
                                 codec_context->width, codec_context->height,
                                 AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 NULL, NULL, NULL);
    return 0;
}

struct read_workload {
    AVFormatContext *format_context;
    AVPacket *packet;
    int finished;
};

struct read_workload init_read_load() {
    struct read_workload ret = {.format_context = format_context,
                                .packet = NULL,
                                .finished = 0};
    return ret;
}

void read_frame(void *workload) {
    struct read_workload *load = workload;

    /* allocate packet */
    load->packet = NULL;
    load->packet = av_packet_alloc();
    if (!load->packet) {
        return;
    }

    av_read_frame(load->format_context, load->packet);
    load->finished = 1;
}

struct decode_workload {
    AVCodecContext *codec_context;
    AVPacket *packet;
    AVFrame *pics[32];
    int n_pics;
    int finished;
};

struct decode_workload init_decode_load(AVPacket *packet) {
    struct decode_workload ret = {.codec_context = codec_context,
                                  .packet = packet,
                                  .pics = {NULL},
                                  .n_pics = 0,
                                  .finished = 0};
    return ret;
}

void decode_frame(void *workload) {
    static int n = 0;
    struct decode_workload *load = workload;
    if (load->packet->stream_index != video_stream) {
        load->finished = 1;
        return;
    }

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
        int n_bytes;
        uint8_t *buf = NULL;
        n_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, load->codec_context->width,
                                           load->codec_context->height, 32);
        buf = av_malloc(n_bytes * sizeof(uint8_t));

        AVFrame *pic = av_frame_alloc();
        av_image_fill_arrays(pic->data, pic->linesize, buf, AV_PIX_FMT_YUV420P,
                             load->codec_context->width, load->codec_context->height, 32);

        /* convert image to YUV420 */
        sws_scale(sws_context, (uint8_t const * const *)frame->data, frame->linesize,
                  0, codec_context->height, pic->data, pic->linesize);


        //printf("Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d,"
        //       " display_picture_number %d, %dx%d]\n",
        //       av_get_picture_type_char(frame->pict_type), codec_context->frame_number,
        //       frame->pts, frame->pkt_dts, frame->key_frame,
        //       frame->coded_picture_number, frame->display_picture_number,
        //       codec_context->width, codec_context->height);

        load->pics[load->n_pics] = pic;

        ++load->n_pics;
    }

    av_frame_free(&frame);
    av_free(frame);
    n++;
    load->finished = 1;
}



int main(int argc, char **argv) {
    /* put the job spawning onto CPU 7 */
    cpu_set_t set;
    CPU_SET(7,&set);

    if (sched_setaffinity(0, sizeof(set), &set) < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

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

    if (init_player(argc, argv)) {
        fprintf(stderr, "Initialisation error\n");
        exit(-1);
    }

    double fps = av_q2d(format_context->streams[video_stream]->r_frame_rate);
    double sleep_time = 1.0/(double)fps;

    int read_task = create_task_with_prediction(1, 0, sleep_time * 1000 * 1000 * 1000, read_frame, NULL);
    int decode_task = create_task_with_prediction(2, 1, sleep_time * 1000 * 1000 * 1000, decode_frame, NULL);

    struct read_workload read_loads[32];
    struct decode_workload decode_loads[32];
    AVFrame *pics[100] = {NULL};

    int first_read_load = 0;

    int first_decode_load = 0;
    int n_decode_loads = 0;

    int first_pic = 0;
    int n_pics = 0;

    /* initialise all 32 read jobs */
    for (int i = 0; i < 32; ++i) {
        read_loads[i] = init_read_load();
    }

    /* add 32 jobs to read task */
    for (int i = 0; i < 32; ++i) {
        add_job_to_task(read_task, &read_loads[i]);
        release_sem(read_task);
    }



    int running = 1;
    while (running) {
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
        if (n_decode_loads && decode_load->finished && n_pics + decode_load->n_pics <= 100) {
            for (int i = 0; i < decode_load->n_pics; ++i) {
                int next_pic = (first_pic + n_pics) % 100;
                pics[next_pic] = decode_load->pics[i];
                ++n_pics;
            }
            /* clean up decode load */
            av_packet_unref(decode_load->packet);

            ++first_decode_load;
            first_decode_load %= 32;
            --n_decode_loads;
        }

        if (n_pics > 0) {
            AVFrame *pic = pics[first_pic];

            SDL_Rect rect = {0, 0, codec_context->width, codec_context->height};

            SDL_UpdateYUVTexture(texture, &rect, pic->data[0], pic->linesize[0],
                                    pic->data[1], pic->linesize[1], pic->data[2],
                                    pic->linesize[2]);

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            /* clean up pic */
            av_frame_free(&pic);
            av_free(pic);

            ++first_pic;
            first_pic %= 100;
            --n_pics;

            SDL_Delay((1000 * sleep_time) - 10);
        }

        SDL_Event event;
        SDL_PollEvent(&event);
        switch(event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            default: break;
        }
    }

    /* Cleanup */
    avcodec_close(codec_context);
    avformat_close_input(&format_context);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}
