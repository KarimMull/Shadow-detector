#ifndef CAMERA_DEVICE_H
#define CAMERA_DEVICE_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>

#define CAMERA_NAME_SIZE 256
#define CAMERA_DESCRIPTION_SIZE 256

typedef struct
{
    char name[CAMERA_NAME_SIZE];
    char description[CAMERA_DESCRIPTION_SIZE];
} CameraDevice;

typedef struct
{
    AVFormatContext *format_ctx;

    AVCodecContext *codec_ctx;

    const AVCodec *codec;

    AVPacket *packet;

    int video_stream_index;

    int width;
    int height;

    AVRational frame_rate;

    char device_name[CAMERA_NAME_SIZE];

    char device_description[
        CAMERA_DESCRIPTION_SIZE
    ];

} CameraCapture;


int camera_list_devices(
    CameraDevice **devices
);


void camera_free_devices(
    CameraDevice *devices,
    int count
);


int camera_open(
    CameraCapture *camera,
    const CameraDevice *device,
    int width,
    int height,
    int fps
);


int camera_read_frame(
    CameraCapture *camera,
    AVFrame *frame
);


void camera_close(
    CameraCapture *camera
);


#endif