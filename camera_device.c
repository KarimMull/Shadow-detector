#include "camera_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavdevice/avdevice.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>


/* ============================================================
 * BUILD DIRECTSHOW INPUT NAME
 * ============================================================ */

static void make_dshow_input_name(
    const CameraDevice *device,
    char *buffer,
    size_t buffer_size
)
{
    if (!device || !buffer || buffer_size == 0)
        return;

    snprintf(
        buffer,
        buffer_size,
        "video=%s",
        device->description
    );
}


/* ============================================================
 * LIST CAMERAS
 * ============================================================ */

int camera_list_devices(CameraDevice **devices)
{
    AVDeviceInfoList *device_list = NULL;
    const AVInputFormat *input_format;
    int ret;
    int count = 0;

    if (!devices)
        return -1;

    *devices = NULL;

    input_format = av_find_input_format("dshow");

    if (!input_format)
    {
        fprintf(
            stderr,
            "ERROR: DirectShow input format not found.\n"
        );

        return -1;
    }

    ret = avdevice_list_input_sources(
        input_format,
        NULL,
        NULL,
        &device_list
    );

    if (ret < 0)
    {
        char error[AV_ERROR_MAX_STRING_SIZE];

        av_strerror(
            ret,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "ERROR: Cannot enumerate cameras: %s\n",
            error
        );

        return -1;
    }

    if (!device_list)
    {
        fprintf(
            stderr,
            "ERROR: Device list is empty.\n"
        );

        return 0;
    }

    /*
     * Count video devices.
     */
    for (int i = 0; i < device_list->nb_devices; i++)
    {
        AVDeviceInfo *info =
            device_list->devices[i];

        if (!info)
            continue;

        if (info->media_types &&
            *info->media_types == AVMEDIA_TYPE_VIDEO)
        {
            count++;
        }
    }

    if (count == 0)
    {
        avdevice_free_list_devices(
            &device_list
        );

        return 0;
    }

    /*
     * Allocate camera array.
     */
    *devices = calloc(
        (size_t)count,
        sizeof(CameraDevice)
    );

    if (!*devices)
    {
        avdevice_free_list_devices(
            &device_list
        );

        fprintf(
            stderr,
            "ERROR: Cannot allocate camera list.\n"
        );

        return -1;
    }

    /*
     * Fill camera array.
     */
    int index = 0;

    for (int i = 0;
         i < device_list->nb_devices;
         i++)
    {
        AVDeviceInfo *info =
            device_list->devices[i];

        if (!info)
            continue;

        if (!info->media_types ||
            *info->media_types != AVMEDIA_TYPE_VIDEO)
        {
            continue;
        }

        snprintf(
            (*devices)[index].name,
            CAMERA_NAME_SIZE,
            "%s",
            info->device_name
                ? info->device_name
                : ""
        );

        snprintf(
            (*devices)[index].description,
            CAMERA_DESCRIPTION_SIZE,
            "%s",
            info->device_description
                ? info->device_description
                : (
                    info->device_name
                        ? info->device_name
                        : ""
                )
        );

        index++;
    }

    avdevice_free_list_devices(
        &device_list
    );

    return count;
}


/* ============================================================
 * FREE CAMERA LIST
 * ============================================================ */

void camera_free_devices(
    CameraDevice *devices,
    int count
)
{
    (void)count;

    free(devices);
}


/* ============================================================
 * OPEN CAMERA
 * ============================================================ */

int camera_open(
    CameraCapture *camera,
    const CameraDevice *device,
    int width,
    int height,
    int fps
)
{
    if (!camera || !device)
    {
        fprintf(
            stderr,
            "[camera] ERROR: Invalid camera/device pointer.\n"
        );

        return -1;
    }

    memset(
        camera,
        0,
        sizeof(*camera)
    );

    camera->format_ctx = NULL;
    camera->codec_ctx = NULL;
    camera->codec = NULL;
    camera->packet = NULL;
    camera->video_stream_index = -1;

    camera->width = width;
    camera->height = height;

    snprintf(
        camera->device_name,
        sizeof(camera->device_name),
        "%s",
        device->name
    );

    snprintf(
        camera->device_description,
        sizeof(camera->device_description),
        "%s",
        device->description
    );

    char input_name[CAMERA_NAME_SIZE + 16];
    char error[AV_ERROR_MAX_STRING_SIZE];

    make_dshow_input_name(
        device,
        input_name,
        sizeof(input_name)
    );

    printf("\n");
    printf("========================================\n");
    printf("          OPENING CAMERA\n");
    printf("========================================\n");

    printf(
        "Name: %s\n",
        device->name
    );

    printf(
        "Description: %s\n",
        device->description
    );

    printf(
        "Input: %s\n",
        input_name
    );

    printf(
        "Requested mode: %dx%d @ %d FPS\n",
        width,
        height,
        fps
    );

    printf(
        "Pixel format: yuyv422\n"
    );

    printf(
        "Buffer: 64 MB\n"
    );

    printf(
        "========================================\n"
    );

    fflush(stdout);

    const AVInputFormat *input_format =
        av_find_input_format("dshow");

    if (!input_format)
    {
        fprintf(
            stderr,
            "[camera] ERROR: DirectShow format not found.\n"
        );

        return -1;
    }


    /* ========================================================
     * ATTEMPT 1
     * ======================================================== */

    printf(
        "[camera] Attempt 1/2\n"
    );

    printf(
        "[camera] Creating AVDictionary...\n"
    );

    fflush(stdout);

    AVDictionary *options = NULL;

    av_dict_set(
        &options,
        "video_size",
        "640x480",
        0
    );

    av_dict_set(
        &options,
        "framerate",
        "30",
        0
    );

    av_dict_set(
        &options,
        "pixel_format",
        "yuyv422",
        0
    );

    av_dict_set(
        &options,
        "rtbufsize",
        "64M",
        0
    );

    printf(
        "[camera] Options prepared.\n"
    );

    printf(
        "[camera] video_size=640x480\n"
    );

    printf(
        "[camera] framerate=30\n"
    );

    printf(
        "[camera] pixel_format=yuyv422\n"
    );

    printf(
        "[camera] rtbufsize=64M\n"
    );

    fflush(stdout);

    printf(
        "[camera] About to call avformat_open_input()\n"
    );

    printf(
        "[camera] Input: %s\n",
        input_name
    );

    fflush(stdout);

    int ret = avformat_open_input(
        &camera->format_ctx,
        input_name,
        input_format,
        &options
    );

    printf(
        "[camera] avformat_open_input() returned: %d\n",
        ret
    );

    fflush(stdout);

    av_dict_free(
        &options
    );

    if (ret < 0)
    {
        av_strerror(
            ret,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "[camera] Attempt 1 failed: %s\n",
            error
        );

        if (camera->format_ctx)
        {
            avformat_close_input(
                &camera->format_ctx
            );
        }

        camera->format_ctx = NULL;
    }
    else
    {
        printf(
            "[camera] Camera input opened successfully.\n"
        );

        fflush(stdout);
    }


    /* ========================================================
     * ATTEMPT 2
     * ======================================================== */

    if (!camera->format_ctx)
    {
        printf("\n");

        printf(
            "[camera] Attempt 2/2\n"
        );

        printf(
            "[camera] Opening camera without explicit mode...\n"
        );

        fflush(stdout);

        options = NULL;

        av_dict_set(
            &options,
            "rtbufsize",
            "64M",
            0
        );

        printf(
            "[camera] About to call avformat_open_input()...\n"
        );

        fflush(stdout);

        ret = avformat_open_input(
            &camera->format_ctx,
            input_name,
            input_format,
            &options
        );

        printf(
            "[camera] avformat_open_input() returned: %d\n",
            ret
        );

        fflush(stdout);

        av_dict_free(
            &options
        );

        if (ret < 0)
        {
            av_strerror(
                ret,
                error,
                sizeof(error)
            );

            fprintf(
                stderr,
                "[camera] Attempt 2 failed: %s\n",
                error
            );

            if (camera->format_ctx)
            {
                avformat_close_input(
                    &camera->format_ctx
                );
            }

            camera->format_ctx = NULL;

            return -1;
        }

        printf(
            "[camera] Camera input opened successfully.\n"
        );

        fflush(stdout);
    }


    /* ========================================================
     * FIND STREAM INFORMATION
     * ======================================================== */

    printf(
        "[camera] Calling avformat_find_stream_info()...\n"
    );

    fflush(stdout);

    ret = avformat_find_stream_info(
        camera->format_ctx,
        NULL
    );

    printf(
        "[camera] avformat_find_stream_info() returned: %d\n",
        ret
    );

    fflush(stdout);

    if (ret < 0)
    {
        av_strerror(
            ret,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "[camera] ERROR: Cannot find stream info: %s\n",
            error
        );

        camera_close(camera);

        return -1;
    }

    printf(
        "[camera] Stream information received.\n"
    );

    fflush(stdout);


    /* ========================================================
     * FIND VIDEO STREAM
     * ======================================================== */

    for (unsigned int i = 0;
         i < camera->format_ctx->nb_streams;
         i++)
    {
        AVStream *stream =
            camera->format_ctx->streams[i];

        if (stream->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO)
        {
            camera->video_stream_index =
                (int)i;

            break;
        }
    }

    if (camera->video_stream_index < 0)
    {
        fprintf(
            stderr,
            "[camera] ERROR: Video stream not found.\n"
        );

        camera_close(camera);

        return -1;
    }

    printf(
        "[camera] Video stream index: %d\n",
        camera->video_stream_index
    );

    fflush(stdout);

    AVStream *stream =
        camera->format_ctx->streams[
            camera->video_stream_index
        ];


    /* ========================================================
     * FIND DECODER
     * ======================================================== */

    camera->codec =
        avcodec_find_decoder(
            stream->codecpar->codec_id
        );

    if (!camera->codec)
    {
        fprintf(
            stderr,
            "[camera] ERROR: Decoder not found.\n"
        );

        camera_close(camera);

        return -1;
    }

    printf(
        "[camera] Decoder: %s\n",
        camera->codec->name
    );

    fflush(stdout);


    /* ========================================================
     * ALLOCATE CODEC CONTEXT
     * ======================================================== */

    camera->codec_ctx =
        avcodec_alloc_context3(
            camera->codec
        );

    if (!camera->codec_ctx)
    {
        fprintf(
            stderr,
            "[camera] ERROR: Cannot allocate codec context.\n"
        );

        camera_close(camera);

        return -1;
    }


    /* ========================================================
     * COPY CODEC PARAMETERS
     * ======================================================== */

    ret = avcodec_parameters_to_context(
        camera->codec_ctx,
        stream->codecpar
    );

    if (ret < 0)
    {
        av_strerror(
            ret,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "[camera] ERROR: "
            "avcodec_parameters_to_context: %s\n",
            error
        );

        camera_close(camera);

        return -1;
    }


    /* ========================================================
     * OPEN DECODER
     * ======================================================== */

    ret = avcodec_open2(
        camera->codec_ctx,
        camera->codec,
        NULL
    );

    if (ret < 0)
    {
        av_strerror(
            ret,
            error,
            sizeof(error)
        );

        fprintf(
            stderr,
            "[camera] ERROR: avcodec_open2: %s\n",
            error
        );

        camera_close(camera);

        return -1;
    }

    printf(
        "[camera] Decoder opened.\n"
    );

    fflush(stdout);


    /* ========================================================
     * USE ACTUAL CAMERA FRAME SIZE
     * ======================================================== */

    camera->width =
        camera->codec_ctx->width;

    camera->height =
        camera->codec_ctx->height;


    /* ========================================================
     * ALLOCATE PACKET
     * ======================================================== */

    camera->packet =
        av_packet_alloc();

    if (!camera->packet)
    {
        fprintf(
            stderr,
            "[camera] ERROR: Cannot allocate packet.\n"
        );

        camera_close(camera);

        return -1;
    }


    /* ========================================================
     * FRAME RATE
     * ======================================================== */

    camera->frame_rate =
        stream->avg_frame_rate;

    if (camera->frame_rate.den == 0)
    {
        camera->frame_rate =
            stream->r_frame_rate;
    }


    /* ========================================================
     * PIXEL FORMAT
     * ======================================================== */

    const char *pixel_format_name =
        av_get_pix_fmt_name(
            camera->codec_ctx->pix_fmt
        );

    if (!pixel_format_name)
    {
        pixel_format_name = "unknown";
    }


    /* ========================================================
     * CAMERA READY
     * ======================================================== */

    printf("\n");

    printf(
        "========================================\n"
    );

    printf(
        "             CAMERA READY\n"
    );

    printf(
        "========================================\n"
    );

    printf(
        "Camera: %s\n",
        camera->device_name
    );

    printf(
        "Resolution: %dx%d\n",
        camera->width,
        camera->height
    );

    printf(
        "FPS: %.2f\n",
        av_q2d(camera->frame_rate)
    );

    printf(
        "Codec: %s\n",
        camera->codec->name
    );

    printf(
        "Pixel format: %s\n",
        pixel_format_name
    );

    printf(
        "========================================\n"
    );

    fflush(stdout);

    return 0;
}


/* ============================================================
 * READ ONE VIDEO FRAME
 * ============================================================ */

int camera_read_frame(
    CameraCapture *camera,
    AVFrame *frame
)
{
    if (!camera)
    {
        fprintf(
            stderr,
            "[camera_read_frame] ERROR: camera == NULL\n"
        );

        return -1;
    }

    if (!camera->format_ctx)
    {
        fprintf(
            stderr,
            "[camera_read_frame] ERROR: "
            "format_ctx == NULL\n"
        );

        return -1;
    }

    if (!camera->codec_ctx)
    {
        fprintf(
            stderr,
            "[camera_read_frame] ERROR: "
            "codec_ctx == NULL\n"
        );

        return -1;
    }

    if (!camera->packet)
    {
        fprintf(
            stderr,
            "[camera_read_frame] ERROR: "
            "packet == NULL\n"
        );

        return -1;
    }

    if (!frame)
    {
        fprintf(
            stderr,
            "[camera_read_frame] ERROR: "
            "frame == NULL\n"
        );

        return -1;
    }


    while (1)
    {
        /* ====================================================
         * READ PACKET
         * ==================================================== */

        int ret =
            av_read_frame(
                camera->format_ctx,
                camera->packet
            );

        if (ret < 0)
        {
            char error[AV_ERROR_MAX_STRING_SIZE];

            av_strerror(
                ret,
                error,
                sizeof(error)
            );

            fprintf(
                stderr,
                "\n"
                "[camera_read_frame] "
                "av_read_frame() FAILED\n"
                "[camera_read_frame] ret = %d\n"
                "[camera_read_frame] error = %s\n",
                ret,
                error
            );

            return ret;
        }

        fprintf(
            stdout,
            "[camera_read_frame] "
            "Packet received: size=%d stream=%d\n",
            camera->packet->size,
            camera->packet->stream_index
        );

        fflush(stdout);


        /* ====================================================
         * IGNORE NON-VIDEO PACKETS
         * ==================================================== */

        if (camera->packet->stream_index !=
            camera->video_stream_index)
        {
            fprintf(
                stdout,
                "[camera_read_frame] "
                "Non-video packet, skipping.\n"
            );

            fflush(stdout);

            av_packet_unref(
                camera->packet
            );

            continue;
        }


        /* ====================================================
         * SEND PACKET TO DECODER
         * ==================================================== */

        ret =
            avcodec_send_packet(
                camera->codec_ctx,
                camera->packet
            );

        av_packet_unref(
            camera->packet
        );

        if (ret < 0)
        {
            char error[AV_ERROR_MAX_STRING_SIZE];

            av_strerror(
                ret,
                error,
                sizeof(error)
            );

            fprintf(
                stderr,
                "\n"
                "[camera_read_frame] "
                "avcodec_send_packet() FAILED\n"
                "[camera_read_frame] ret = %d\n"
                "[camera_read_frame] error = %s\n",
                ret,
                error
            );

            return ret;
        }

        fprintf(
            stdout,
            "[camera_read_frame] "
            "Packet sent to decoder.\n"
        );

        fflush(stdout);


        /* ====================================================
         * RECEIVE DECODED FRAME
         * ==================================================== */

        while (1)
        {
            ret =
                avcodec_receive_frame(
                    camera->codec_ctx,
                    frame
                );

            /*
             * Successfully decoded.
             */
            if (ret == 0)
            {
                fprintf(
                    stdout,
                    "[camera_read_frame] "
                    "FRAME RECEIVED: %dx%d format=%d\n",
                    frame->width,
                    frame->height,
                    frame->format
                );

                fflush(stdout);

                return 0;
            }

            /*
             * Need another packet.
             */
            if (ret == AVERROR(EAGAIN))
            {
                fprintf(
                    stdout,
                    "[camera_read_frame] "
                    "Decoder returned EAGAIN. "
                    "Reading next packet...\n"
                );

                fflush(stdout);

                break;
            }

            /*
             * End of stream.
             */
            if (ret == AVERROR_EOF)
            {
                fprintf(
                    stderr,
                    "\n"
                    "[camera_read_frame] "
                    "Decoder returned EOF.\n"
                );

                return ret;
            }

            /*
             * Other decoder error.
             */
            {
                char error[AV_ERROR_MAX_STRING_SIZE];

                av_strerror(
                    ret,
                    error,
                    sizeof(error)
                );

                fprintf(
                    stderr,
                    "\n"
                    "[camera_read_frame] "
                    "avcodec_receive_frame() FAILED\n"
                    "[camera_read_frame] ret = %d\n"
                    "[camera_read_frame] error = %s\n",
                    ret,
                    error
                );
            }

            return ret;
        }
    }
}


/* ============================================================
 * CLOSE CAMERA
 * ============================================================ */

void camera_close(
    CameraCapture *camera
)
{
    if (!camera)
        return;

    if (camera->packet)
    {
        av_packet_free(
            &camera->packet
        );
    }

    if (camera->codec_ctx)
    {
        avcodec_free_context(
            &camera->codec_ctx
        );
    }

    if (camera->format_ctx)
    {
        avformat_close_input(
            &camera->format_ctx
        );
    }

    camera->codec = NULL;

    camera->video_stream_index = -1;

    camera->width = 0;
    camera->height = 0;
}