#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>

#include "camera_device.h"
#include "shadow_detector.h"
#include "video_encoder.h"


/* ============================================================
 * SETTINGS
 * ============================================================ */

#define WIDTH       640
#define HEIGHT      480
#define FPS          30

#define OUTPUT_FILE "output_shadow.mp4"


/*
 * ============================================================
 * SHADOW DETECTOR SETTINGS
 * ============================================================
 *
 * Начальные параметры.
 *
 * Если теней слишком мало:
 *
 *     уменьшить MIN_Y_SHADOW
 *
 * Если много ложных срабатываний:
 *
 *     увеличить MIN_Y_SHADOW
 *     уменьшить MAX_U
 *     уменьшить MAX_V
 *     увеличить MIN_AREA
 */

#define MIN_Y_SHADOW       0.25f
#define MAX_Y_SHADOW       0.75f

#define MAX_U              12
#define MAX_V              12

#define MIN_SHADOW_AREA    200

/*
 * Скорость адаптации background.
 */
#define BACKGROUND_ALPHA   0.01f


/* ============================================================
 * FFMPEG ERROR
 * ============================================================ */

static void print_ffmpeg_error(
    const char *where,
    int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE];

    av_strerror(
        error,
        buffer,
        sizeof(buffer));

    fprintf(
        stderr,
        "[FFmpeg] %s: %s (%d)\n",
        where,
        buffer,
        error);
}


/* ============================================================
 * SDL INIT
 * ============================================================ */

static int init_sdl(
    SDL_Window **window,
    SDL_Renderer **renderer,
    SDL_Texture **texture,
    int width,
    int height)
{
    if (!window ||
        !renderer ||
        !texture)
    {
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        fprintf(
            stderr,
            "SDL_Init failed: %s\n",
            SDL_GetError());

        return -1;
    }

    *window =
        SDL_CreateWindow(
            "Camera Shadow Detector",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            width,
            height,
            SDL_WINDOW_SHOWN);

    if (!*window)
    {
        fprintf(
            stderr,
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError());

        SDL_Quit();

        return -1;
    }

    *renderer =
        SDL_CreateRenderer(
            *window,
            -1,
            SDL_RENDERER_ACCELERATED |
            SDL_RENDERER_PRESENTVSYNC);

    if (!*renderer)
    {
        fprintf(
            stderr,
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError());

        SDL_DestroyWindow(*window);
        *window = NULL;

        SDL_Quit();

        return -1;
    }

    *texture =
        SDL_CreateTexture(
            *renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height);

    if (!*texture)
    {
        fprintf(
            stderr,
            "SDL_CreateTexture failed: %s\n",
            SDL_GetError());

        SDL_DestroyRenderer(*renderer);
        SDL_DestroyWindow(*window);

        *renderer = NULL;
        *window = NULL;

        SDL_Quit();

        return -1;
    }

    return 0;
}


/* ============================================================
 * SDL CLEANUP
 * ============================================================ */

static void close_sdl(
    SDL_Window *window,
    SDL_Renderer *renderer,
    SDL_Texture *texture)
{
    if (texture)
        SDL_DestroyTexture(texture);

    if (renderer)
        SDL_DestroyRenderer(renderer);

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}


/* ============================================================
 * DISPLAY RGB
 * ============================================================ */

static void display_rgb(
    SDL_Renderer *renderer,
    SDL_Texture *texture,
    const uint8_t *rgb,
    int width)
{
    if (!renderer ||
        !texture ||
        !rgb)
    {
        return;
    }

    SDL_UpdateTexture(
        texture,
        NULL,
        rgb,
        width * 3);

    SDL_RenderClear(renderer);

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        NULL);

    SDL_RenderPresent(renderer);
}


/* ============================================================
 * SDL EVENTS
 * ============================================================ */

static int process_events(
    ShadowDetector *detector)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
        {
            return 0;
        }

        if (event.type == SDL_KEYDOWN)
        {
            /*
             * ESC / Q
             */
            if (event.key.keysym.sym == SDLK_ESCAPE ||
                event.key.keysym.sym == SDLK_q)
            {
                return 0;
            }

            /*
             * R:
             *
             * Полностью перестроить background.
             */
            if (event.key.keysym.sym == SDLK_r)
            {
                printf(
                    "\n\n[main] Resetting background...\n");

                shadow_detector_reset_background(
                    detector);
            }
        }
    }

    return 1;
}


/* ============================================================
 * APPLY SHADOW MASK
 * ============================================================ */

static void apply_shadow_mask(
    uint8_t *rgb,
    const uint8_t *mask,
    int width,
    int height)
{
    if (!rgb ||
        !mask)
    {
        return;
    }

    for (int y = 0;
         y < height;
         y++)
    {
        for (int x = 0;
             x < width;
             x++)
        {
            int index =
                y * width + x;

            if (mask[index] == 0)
                continue;

            int rgb_index =
                index * 3;

            uint8_t r =
                rgb[rgb_index + 0];

            uint8_t g =
                rgb[rgb_index + 1];

            uint8_t b =
                rgb[rgb_index + 2];

            /*
             * Красная полупрозрачная маска.
             */
            rgb[rgb_index + 0] =
                (uint8_t)(
                    ((int)r * 35 +
                     255 * 65) / 100);

            rgb[rgb_index + 1] =
                (uint8_t)(
                    ((int)g * 35) / 100);

            rgb[rgb_index + 2] =
                (uint8_t)(
                    ((int)b * 35) / 100);
        }
    }
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(
    int argc,
    char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf(
        "========================================\n");

    printf(
        "       CAMERA SHADOW DETECTOR\n");

    printf(
        "========================================\n");

    printf("\n");


    /* ========================================================
     * FFMPEG DEVICES
     * ======================================================== */

    avdevice_register_all();


    /* ========================================================
     * SEARCH CAMERAS
     * ======================================================== */

    CameraDevice *cameras = NULL;

    int camera_count =
        camera_list_devices(
            &cameras);

    if (camera_count <= 0)
    {
        fprintf(
            stderr,
            "\nERROR: No video cameras found.\n");

        return 1;
    }


    printf(
        "========================================\n");

    printf(
        "          AVAILABLE CAMERAS\n");

    printf(
        "========================================\n");

    for (int i = 0;
         i < camera_count;
         i++)
    {
        printf(
            "[%d] %s\n",
            i,
            cameras[i].description);
    }

    printf(
        "========================================\n");


    /* ========================================================
     * SELECT CAMERA
     * ======================================================== */

    int camera_index = 0;

    printf(
        "Select camera [0-%d]: ",
        camera_count - 1);

    fflush(stdout);

    if (scanf(
            "%d",
            &camera_index) != 1)
    {
        fprintf(
            stderr,
            "ERROR: Invalid camera number.\n");

        camera_free_devices(
            cameras,
            camera_count);

        return 1;
    }

    if (camera_index < 0 ||
        camera_index >= camera_count)
    {
        fprintf(
            stderr,
            "ERROR: Camera index out of range.\n");

        camera_free_devices(
            cameras,
            camera_count);

        return 1;
    }


    printf(
        "\nSelected camera: %s\n",
        cameras[camera_index].description);


    /* ========================================================
     * OPEN CAMERA
     * ======================================================== */

    CameraCapture capture;

    memset(
        &capture,
        0,
        sizeof(capture));

    printf("\n");
    printf(
        "----------------------------------------\n");

    printf(
        "Opening camera...\n");

    printf(
        "----------------------------------------\n");

    int ret =
        camera_open(
            &capture,
            &cameras[camera_index],
            WIDTH,
            HEIGHT,
            FPS);

    if (ret < 0)
    {
        fprintf(
            stderr,
            "\nERROR: Could not open camera.\n");

        camera_free_devices(
            cameras,
            camera_count);

        return 1;
    }

    camera_free_devices(
        cameras,
        camera_count);

    cameras = NULL;


    printf("\n");

    printf(
        "[main] Camera opened successfully.\n");

    printf(
        "[main] Resolution: %dx%d\n",
        capture.width,
        capture.height);

    printf(
        "[main] Codec: %s\n",
        capture.codec
            ? capture.codec->name
            : "unknown");


    /* ========================================================
     * ACTUAL FRAME SIZE
     * ======================================================== */

    const int frame_width =
        capture.width;

    const int frame_height =
        capture.height;


    if (frame_width <= 0 ||
        frame_height <= 0)
    {
        fprintf(
            stderr,
            "ERROR: Invalid camera resolution.\n");

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * YUV BUFFERS
     * ======================================================== */

    const size_t y_size =
        (size_t)frame_width *
        (size_t)frame_height;

    const size_t uv_size =
        (size_t)(frame_width / 2) *
        (size_t)(frame_height / 2);


    uint8_t *y_plane =
        (uint8_t *)malloc(y_size);

    uint8_t *u_plane =
        (uint8_t *)malloc(uv_size);

    uint8_t *v_plane =
        (uint8_t *)malloc(uv_size);

    if (!y_plane ||
        !u_plane ||
        !v_plane)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate YUV buffers.\n");

        free(y_plane);
        free(u_plane);
        free(v_plane);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * RGB BUFFER
     * ======================================================== */

    const size_t rgb_size =
        y_size * 3;

    uint8_t *rgb =
        (uint8_t *)malloc(
            rgb_size);

    if (!rgb)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate RGB buffer.\n");

        free(y_plane);
        free(u_plane);
        free(v_plane);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * SHADOW MASK
     * ======================================================== */

    uint8_t *shadow_mask =
        (uint8_t *)malloc(
            y_size);

    if (!shadow_mask)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate shadow mask.\n");

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);

        camera_close(&capture);

        return 1;
    }

    memset(
        shadow_mask,
        0,
        y_size);


    /* ========================================================
     * SHADOW DETECTOR
     * ======================================================== */

    ShadowDetector detector;

    memset(
        &detector,
        0,
        sizeof(detector));

    if (shadow_detector_init(
            &detector,
            frame_width,
            frame_height) < 0)
    {
        fprintf(
            stderr,
            "ERROR: Could not initialize shadow detector.\n");

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    /*
     * Настраиваем алгоритм.
     */
    shadow_detector_set_parameters(
        &detector,
        MIN_Y_SHADOW,
        MAX_Y_SHADOW,
        MAX_U,
        MAX_V,
        MIN_SHADOW_AREA);

    detector.background_alpha =
        BACKGROUND_ALPHA;


    printf(
        "[main] Shadow detector initialized.\n");

    printf(
        "[main] Background alpha: %.3f\n",
        detector.background_alpha);

    printf(
        "[main] Y shadow range: %.2f - %.2f\n",
        detector.min_y_ratio,
        detector.max_y_ratio);

    printf(
        "[main] U threshold: %d\n",
        detector.max_u_diff);

    printf(
        "[main] V threshold: %d\n",
        detector.max_v_diff);

    printf(
        "[main] Minimum shadow area: %d px\n",
        detector.min_shadow_area);


    /* ========================================================
     * AVFRAME
     * ======================================================== */

    AVFrame *frame =
        av_frame_alloc();

    if (!frame)
    {
        fprintf(
            stderr,
            "ERROR: Could not allocate AVFrame.\n");

        shadow_detector_free(&detector);

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * SWScale: CAMERA -> YUV420P
     * ======================================================== */

    struct SwsContext *sws_yuv =
        sws_getContext(
            frame_width,
            frame_height,
            capture.codec_ctx->pix_fmt,

            frame_width,
            frame_height,
            AV_PIX_FMT_YUV420P,

            SWS_BILINEAR,
            NULL,
            NULL,
            NULL);

    if (!sws_yuv)
    {
        fprintf(
            stderr,
            "ERROR: Could not create YUV converter.\n");

        av_frame_free(&frame);

        shadow_detector_free(&detector);

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * SWScale: CAMERA -> RGB24
     * ======================================================== */

    struct SwsContext *sws_rgb =
        sws_getContext(
            frame_width,
            frame_height,
            capture.codec_ctx->pix_fmt,

            frame_width,
            frame_height,
            AV_PIX_FMT_RGB24,

            SWS_BILINEAR,
            NULL,
            NULL,
            NULL);

    if (!sws_rgb)
    {
        fprintf(
            stderr,
            "ERROR: Could not create RGB converter.\n");

        sws_freeContext(sws_yuv);

        av_frame_free(&frame);

        shadow_detector_free(&detector);

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * SDL
     * ======================================================== */

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    if (init_sdl(
            &window,
            &renderer,
            &texture,
            frame_width,
            frame_height) < 0)
    {
        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);

        av_frame_free(&frame);

        shadow_detector_free(&detector);

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    /* ========================================================
     * VIDEO ENCODER
     * ======================================================== */

    VideoEncoder *encoder =
        video_encoder_create(
            OUTPUT_FILE,
            frame_width,
            frame_height,
            FPS);

    if (!encoder)
    {
        fprintf(
            stderr,
            "ERROR: Could not create video encoder.\n");

        close_sdl(
            window,
            renderer,
            texture);

        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);

        av_frame_free(&frame);

        shadow_detector_free(&detector);

        free(y_plane);
        free(u_plane);
        free(v_plane);
        free(rgb);
        free(shadow_mask);

        camera_close(&capture);

        return 1;
    }


    printf(
        "[main] Recording to: %s\n",
        OUTPUT_FILE);

    printf("\n");

    printf("Controls:\n");
    printf("  R   - reset background\n");
    printf("  Q   - quit\n");
    printf("  ESC - quit\n");

    printf("\n");


    /* ========================================================
     * MAIN LOOP
     * ======================================================== */

    int running = 1;
    int frame_number = 0;

    int background_message_printed = 0;


    while (running)
    {
        /* ====================================================
         * READ CAMERA FRAME
         * ==================================================== */

        ret =
            camera_read_frame(
                &capture,
                frame);

        if (ret < 0)
        {
            print_ffmpeg_error(
                "camera_read_frame",
                ret);

            fprintf(
                stderr,
                "\n[main] Camera frame read failed.\n");

            break;
        }


        /* ====================================================
         * FRAME VALIDATION
         * ==================================================== */

        if (!frame->data[0] ||
            frame->width <= 0 ||
            frame->height <= 0)
        {
            fprintf(
                stderr,
                "[main] Invalid decoded frame.\n");

            break;
        }


        /* ====================================================
         * CAMERA -> YUV420P
         * ==================================================== */

        uint8_t *yuv_data[4] =
        {
            y_plane,
            u_plane,
            v_plane,
            NULL
        };

        int yuv_linesize[4] =
        {
            frame_width,
            frame_width / 2,
            frame_width / 2,
            0
        };


        int scale_result =
            sws_scale(
                sws_yuv,

                (const uint8_t * const *)
                    frame->data,

                frame->linesize,

                0,
                frame_height,

                yuv_data,
                yuv_linesize);


        if (scale_result <= 0)
        {
            fprintf(
                stderr,
                "[main] YUV conversion failed.\n");

            break;
        }


        /* ====================================================
         * BUILD INITIAL BACKGROUND
         * ==================================================== */

        if (!shadow_detector_background_ready(
                &detector))
        {
            shadow_detector_build_background(
                &detector,

                y_plane,
                u_plane,
                v_plane,

                frame_width,
                frame_width / 2,
                frame_width / 2);

            /*
             * Показываем прогресс.
             */
            if (detector.background_frames == 1 ||
                detector.background_frames % 5 == 0)
            {
                printf(
                    "\r[background] "
                    "Building: %d/%d frames",
                    detector.background_frames,
                    detector.background_min_frames);

                fflush(stdout);
            }

            background_message_printed = 0;
        }
        else
        {
            /*
             * Background готов.
             */
            if (!background_message_printed)
            {
                printf(
                    "\n[background] READY.\n");

                printf(
                    "[background] Adaptive update enabled.\n");

                background_message_printed = 1;
            }


            /* =================================================
             * SHADOW DETECTION
             * ================================================= */

            ret =
                shadow_detector_process(
                    &detector,

                    y_plane,
                    u_plane,
                    v_plane,

                    frame_width,
                    frame_width / 2,
                    frame_width / 2,

                    shadow_mask);


            if (ret < 0)
            {
                fprintf(
                    stderr,
                    "[main] Shadow detector failed.\n");

                break;
            }
        }


        /* ====================================================
         * CAMERA -> RGB24
         * ==================================================== */

        uint8_t *rgb_data[4] =
        {
            rgb,
            NULL,
            NULL,
            NULL
        };

        int rgb_linesize[4] =
        {
            frame_width * 3,
            0,
            0,
            0
        };


        scale_result =
            sws_scale(
                sws_rgb,

                (const uint8_t * const *)
                    frame->data,

                frame->linesize,

                0,
                frame_height,

                rgb_data,
                rgb_linesize);


        if (scale_result <= 0)
        {
            fprintf(
                stderr,
                "[main] RGB conversion failed.\n");

            break;
        }


        /* ====================================================
         * APPLY SHADOW MASK
         * ==================================================== */

        if (shadow_detector_background_ready(
                &detector))
        {
            apply_shadow_mask(
                rgb,
                shadow_mask,
                frame_width,
                frame_height);
        }


        /* ====================================================
         * DISPLAY
         * ==================================================== */

        display_rgb(
            renderer,
            texture,
            rgb,
            frame_width);


        /* ====================================================
         * RECORD VIDEO
         * ==================================================== */

        ret =
            video_encoder_write_rgb24(
                encoder,
                rgb,
                frame_width * 3);

        if (ret < 0)
        {
            fprintf(
                stderr,
                "[main] Video encoder error.\n");

            break;
        }


        /* ====================================================
         * UPDATE BACKGROUND
         * ==================================================== */

        if (shadow_detector_background_ready(
                &detector))
        {
            shadow_detector_update_background(
                &detector,

                y_plane,
                u_plane,
                v_plane,

                frame_width,
                frame_width / 2,
                frame_width / 2);
        }


        /* ====================================================
         * STATISTICS
         * ==================================================== */

        frame_number++;

        if ((frame_number % FPS) == 0)
        {
            int shadow_pixels =
                shadow_detector_get_shadow_pixels(
                    &detector);

            int components =
                shadow_detector_get_components(
                    &detector);

            int valid_components =
                shadow_detector_get_valid_components(
                    &detector);

            printf(
                "\r[main] "
                "Frames: %d | "
                "Shadow pixels: %d | "
                "Components: %d | "
                "Valid: %d",

                frame_number,
                shadow_pixels,
                components,
                valid_components);

            fflush(stdout);
        }


        /* ====================================================
         * SDL EVENTS
         * ==================================================== */

        running =
            process_events(
                &detector);
    }


    /* ========================================================
     * CLEANUP
     * ======================================================== */

    printf("\n\n");
    printf(
        "[main] Stopping...\n");


    video_encoder_close(
        encoder);


    close_sdl(
        window,
        renderer,
        texture);


    sws_freeContext(
        sws_rgb);

    sws_freeContext(
        sws_yuv);


    av_frame_free(
        &frame);


    shadow_detector_free(
        &detector);


    free(y_plane);
    free(u_plane);
    free(v_plane);
    free(rgb);
    free(shadow_mask);


    camera_close(
        &capture);


    printf(
        "[main] Finished.\n");

    return 0;
}