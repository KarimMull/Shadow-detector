#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <stdint.h>


typedef struct VideoEncoder VideoEncoder;


/*
 * Создать MP4 encoder.
 */
VideoEncoder *video_encoder_create(
    const char *filename,
    int width,
    int height,
    int fps
);


/*
 * Записать RGB24 кадр в MP4.
 *
 * RGB:
 *
 * R G B
 * R G B
 * R G B
 *
 */
int video_encoder_write_rgb24(
    VideoEncoder *encoder,
    const uint8_t *rgb,
    int rgb_stride
);


/*
 * Закрыть MP4.
 */
int video_encoder_close(
    VideoEncoder *encoder
);

#endif