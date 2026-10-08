#include "video_encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>


struct VideoEncoder
{
    AVFormatContext *format_ctx;

    AVCodecContext *codec_ctx;

    AVStream *stream;

    AVFrame *frame;

    AVPacket *packet;

    struct SwsContext *rgb_to_yuv;

    int width;
    int height;
    int fps;

    int64_t frame_number;
};


/*
 * Вывести FFmpeg ошибку.
 */
static void print_encoder_error(
    const char *message,
    int error)
{
    char buffer[
        AV_ERROR_MAX_STRING_SIZE
    ];

    av_strerror(
        error,
        buffer,
        sizeof(buffer)
    );

    fprintf(
        stderr,
        "%s: %s\n",
        message,
        buffer
    );
}


/*
 * ------------------------------------------------------------
 * CREATE
 * ------------------------------------------------------------
 */

VideoEncoder *video_encoder_create(
    const char *filename,
    int width,
    int height,
    int fps)
{
    if (!filename ||
        width <= 0 ||
        height <= 0 ||
        fps <= 0)
    {
        return NULL;
    }

    VideoEncoder *encoder =
        calloc(
            1,
            sizeof(VideoEncoder)
        );

    if (!encoder)
        return NULL;

    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;

    /*
     * --------------------------------------------------------
     * Кодек.
     *
     * Используем MPEG-4, потому что он доступен
     * в стандартных FFmpeg сборках.
     * --------------------------------------------------------
     */

    const AVCodec *codec =
        avcodec_find_encoder(
            AV_CODEC_ID_MPEG4
        );

    if (!codec)
    {
        fprintf(
            stderr,
            "ERROR: MPEG-4 encoder not found.\n"
        );

        free(encoder);

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * MP4 container.
     * --------------------------------------------------------
     */

    int ret =
        avformat_alloc_output_context2(
            &encoder->format_ctx,
            NULL,
            "mp4",
            filename
        );

    if (ret < 0 ||
        !encoder->format_ctx)
    {
        print_encoder_error(
            "Could not create MP4 context",
            ret
        );

        free(encoder);

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * Stream.
     * --------------------------------------------------------
     */

    encoder->stream =
        avformat_new_stream(
            encoder->format_ctx,
            NULL
        );

    if (!encoder->stream)
    {
        fprintf(
            stderr,
            "ERROR: Could not create video stream.\n"
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * Codec context.
     * --------------------------------------------------------
     */

    encoder->codec_ctx =
        avcodec_alloc_context3(codec);

    if (!encoder->codec_ctx)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate codec context.\n"
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    encoder->codec_ctx->codec_id =
        AV_CODEC_ID_MPEG4;

    encoder->codec_ctx->codec_type =
        AVMEDIA_TYPE_VIDEO;

    encoder->codec_ctx->width =
        width;

    encoder->codec_ctx->height =
        height;

    encoder->codec_ctx->pix_fmt =
        AV_PIX_FMT_YUV420P;

    encoder->codec_ctx->time_base =
        (AVRational){1, fps};

    encoder->codec_ctx->framerate =
        (AVRational){fps, 1};

    /*
     * 2.5 Mbit/s.
     */
    encoder->codec_ctx->bit_rate =
        2500000;

    /*
     * Keyframe каждые 30 кадров.
     */
    encoder->codec_ctx->gop_size =
        fps;

    encoder->codec_ctx->max_b_frames =
        0;

    if (encoder->format_ctx->oformat->flags &
        AVFMT_GLOBALHEADER)
    {
        encoder->codec_ctx->flags |=
            AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    /*
     * --------------------------------------------------------
     * Открытие кодека.
     * --------------------------------------------------------
     */

    ret =
        avcodec_open2(
            encoder->codec_ctx,
            codec,
            NULL
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Could not open MPEG-4 encoder",
            ret
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * Codec parameters -> stream.
     * --------------------------------------------------------
     */

    ret =
        avcodec_parameters_from_context(
            encoder->stream->codecpar,
            encoder->codec_ctx
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Could not copy codec parameters",
            ret
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    encoder->stream->time_base =
        encoder->codec_ctx->time_base;

    /*
     * --------------------------------------------------------
     * Открываем файл.
     * --------------------------------------------------------
     */

    if (!(encoder->format_ctx->oformat->flags &
          AVFMT_NOFILE))
    {
        ret =
            avio_open(
                &encoder->format_ctx->pb,
                filename,
                AVIO_FLAG_WRITE
            );

        if (ret < 0)
        {
            print_encoder_error(
                "Could not open output file",
                ret
            );

            video_encoder_close(
                encoder
            );

            return NULL;
        }
    }

    /*
     * --------------------------------------------------------
     * Заголовок MP4.
     * --------------------------------------------------------
     */

    ret =
        avformat_write_header(
            encoder->format_ctx,
            NULL
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Could not write MP4 header",
            ret
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * Frame.
     * --------------------------------------------------------
     */

    encoder->frame =
        av_frame_alloc();

    encoder->packet =
        av_packet_alloc();

    if (!encoder->frame ||
        !encoder->packet)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate encoder buffers.\n"
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    encoder->frame->format =
        AV_PIX_FMT_YUV420P;

    encoder->frame->width =
        width;

    encoder->frame->height =
        height;

    ret =
        av_frame_get_buffer(
            encoder->frame,
            32
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Could not allocate YUV frame",
            ret
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    /*
     * --------------------------------------------------------
     * RGB24 -> YUV420P
     * --------------------------------------------------------
     */

    encoder->rgb_to_yuv =
        sws_getContext(
            width,
            height,
            AV_PIX_FMT_RGB24,

            width,
            height,
            AV_PIX_FMT_YUV420P,

            SWS_FAST_BILINEAR,

            NULL,
            NULL,
            NULL
        );

    if (!encoder->rgb_to_yuv)
    {
        fprintf(
            stderr,
            "ERROR: Could not create RGB->YUV converter.\n"
        );

        video_encoder_close(
            encoder
        );

        return NULL;
    }

    printf(
        "Video output: %s\n",
        filename
    );

    printf(
        "Codec: MPEG-4\n"
    );

    printf(
        "Resolution: %dx%d\n",
        width,
        height
    );

    printf(
        "FPS: %d\n",
        fps
    );

    return encoder;
}


/*
 * ------------------------------------------------------------
 * WRITE RGB FRAME
 * ------------------------------------------------------------
 */

int video_encoder_write_rgb24(
    VideoEncoder *encoder,
    const uint8_t *rgb,
    int rgb_stride)
{
    if (!encoder ||
        !rgb)
    {
        return -1;
    }

    int ret =
        av_frame_make_writable(
            encoder->frame
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Encoder frame is not writable",
            ret
        );

        return ret;
    }

    /*
     * RGB source.
     */
    const uint8_t *src_data[4] =
    {
        rgb,
        NULL,
        NULL,
        NULL
    };

    int src_linesize[4] =
    {
        rgb_stride,
        0,
        0,
        0
    };

    /*
     * RGB -> YUV420P.
     */
    sws_scale(
        encoder->rgb_to_yuv,

        src_data,
        src_linesize,

        0,
        encoder->height,

        encoder->frame->data,
        encoder->frame->linesize
    );

    encoder->frame->pts =
        encoder->frame_number++;

    /*
     * --------------------------------------------------------
     * Отправляем кадр кодеку.
     * --------------------------------------------------------
     */

    ret =
        avcodec_send_frame(
            encoder->codec_ctx,
            encoder->frame
        );

    if (ret < 0)
    {
        print_encoder_error(
            "Could not send frame to encoder",
            ret
        );

        return ret;
    }

    /*
     * --------------------------------------------------------
     * Получаем packets.
     * --------------------------------------------------------
     */

    while (1)
    {
        ret =
            avcodec_receive_packet(
                encoder->codec_ctx,
                encoder->packet
            );

        if (ret == AVERROR(EAGAIN) ||
            ret == AVERROR_EOF)
        {
            break;
        }

        if (ret < 0)
        {
            print_encoder_error(
                "Could not receive encoded packet",
                ret
            );

            return ret;
        }

        av_packet_rescale_ts(
            encoder->packet,

            encoder->codec_ctx->time_base,

            encoder->stream->time_base
        );

        encoder->packet->stream_index =
            encoder->stream->index;

        ret =
            av_interleaved_write_frame(
                encoder->format_ctx,
                encoder->packet
            );

        av_packet_unref(
            encoder->packet
        );

        if (ret < 0)
        {
            print_encoder_error(
                "Could not write packet",
                ret
            );

            return ret;
        }
    }

    return 0;
}


/*
 * ------------------------------------------------------------
 * CLOSE
 * ------------------------------------------------------------
 */

int video_encoder_close(
    VideoEncoder *encoder)
{
    if (!encoder)
        return 0;

    /*
     * --------------------------------------------------------
     * Flush encoder.
     * --------------------------------------------------------
     */

    if (encoder->codec_ctx &&
        encoder->packet)
    {
        int ret =
            avcodec_send_frame(
                encoder->codec_ctx,
                NULL
            );

        if (ret >= 0)
        {
            while (1)
            {
                ret =
                    avcodec_receive_packet(
                        encoder->codec_ctx,
                        encoder->packet
                    );

                if (ret == AVERROR(EAGAIN) ||
                    ret == AVERROR_EOF)
                {
                    break;
                }

                if (ret < 0)
                    break;

                av_packet_rescale_ts(
                    encoder->packet,

                    encoder->codec_ctx->time_base,

                    encoder->stream->time_base
                );

                encoder->packet->stream_index =
                    encoder->stream->index;

                av_interleaved_write_frame(
                    encoder->format_ctx,
                    encoder->packet
                );

                av_packet_unref(
                    encoder->packet
                );
            }
        }
    }

    /*
     * --------------------------------------------------------
     * MP4 trailer.
     * --------------------------------------------------------
     */

    if (encoder->format_ctx)
    {
        av_write_trailer(
            encoder->format_ctx
        );
    }

    /*
     * --------------------------------------------------------
     * Close file.
     * --------------------------------------------------------
     */

    if (encoder->format_ctx &&
        !(encoder->format_ctx->oformat->flags &
          AVFMT_NOFILE))
    {
        avio_closep(
            &encoder->format_ctx->pb
        );
    }

    /*
     * --------------------------------------------------------
     * Free resources.
     * --------------------------------------------------------
     */

    if (encoder->rgb_to_yuv)
    {
        sws_freeContext(
            encoder->rgb_to_yuv
        );

        encoder->rgb_to_yuv = NULL;
    }

    if (encoder->frame)
    {
        av_frame_free(
            &encoder->frame
        );
    }

    if (encoder->packet)
    {
        av_packet_free(
            &encoder->packet
        );
    }

    if (encoder->codec_ctx)
    {
        avcodec_free_context(
            &encoder->codec_ctx
        );
    }

    if (encoder->format_ctx)
    {
        avformat_free_context(
            encoder->format_ctx
        );
    }

    free(encoder);

    return 0;
}