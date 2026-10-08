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
#include "ui_controls.h"


/* ============================================================
 * SETTINGS
 * ============================================================ */

#define WIDTH       640
#define HEIGHT      480
#define FPS          30

#define OUTPUT_FILE "output_shadow.mp4"

#define PANEL_WIDTH 340


/* ============================================================
 * SHADOW DETECTOR DEFAULTS
 * ============================================================ */

#define MIN_Y_SHADOW       0.25f
#define MAX_Y_SHADOW       0.75f

#define MAX_U              12
#define MAX_V              12

#define MIN_SHADOW_AREA    200

#define BACKGROUND_ALPHA   0.01f


enum
{
    SLIDER_MIN_Y = 0,
    SLIDER_MAX_Y,
    SLIDER_MAX_U,
    SLIDER_MAX_V,
    SLIDER_MIN_AREA,
    SLIDER_BG_ALPHA,
    SLIDER_COUNT
};


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
 * FONT SEARCH
 * ============================================================ */

static const char *find_font(void)
{
    static const char *candidates[] =
    {
        "arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/msys64/ucrt64/share/fonts/TTF/DejaVuSans.ttf",
        "C:/msys64/ucrt64/share/fonts/dejavu/DejaVuSans.ttf",
        NULL
    };

    for (int i = 0; candidates[i]; i++)
    {
        FILE *f = fopen(candidates[i], "rb");

        if (f)
        {
            fclose(f);
            return candidates[i];
        }
    }

    return NULL;
}


/* ============================================================
 * SDL INIT
 * ============================================================ */

static int init_sdl(
    SDL_Window **window,
    SDL_Renderer **renderer,
    SDL_Texture **texture,
    int video_width,
    int video_height,
    int total_width)
{
    if (!window || !renderer || !texture)
        return -1;

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
            total_width,
            video_height,
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
            video_width,
            video_height);

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
 * DISPLAY RGB (видео в левой части окна)
 * ============================================================ */

static void display_rgb(
    SDL_Renderer *renderer,
    SDL_Texture *texture,
    const uint8_t *rgb,
    int video_width,
    int video_height)
{
    if (!renderer || !texture || !rgb)
        return;

    SDL_UpdateTexture(
        texture,
        NULL,
        rgb,
        video_width * 3);

    /*
     * Всё окно — чёрный фон.
     */
    SDL_SetRenderDrawColor(
        renderer,
        0, 0, 0, 255);

    SDL_RenderClear(renderer);

    /*
     * Видео — в левый верхний угол.
     */
    SDL_Rect dst;

    dst.x = 0;
    dst.y = 0;
    dst.w = video_width;
    dst.h = video_height;

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        &dst);
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
    if (!rgb || !mask)
        return;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int index = y * width + x;

            if (mask[index] == 0)
                continue;

            int rgb_index = index * 3;

            uint8_t r = rgb[rgb_index + 0];
            uint8_t g = rgb[rgb_index + 1];
            uint8_t b = rgb[rgb_index + 2];

            rgb[rgb_index + 0] =
                (uint8_t)(((int)r * 35 + 255 * 65) / 100);
            rgb[rgb_index + 1] =
                (uint8_t)(((int)g * 35) / 100);
            rgb[rgb_index + 2] =
                (uint8_t)(((int)b * 35) / 100);
        }
    }
}


/* ============================================================
 * SLIDER CALLBACK
 * ============================================================ */

typedef struct
{
    UIControls *ui;
    ShadowDetector *detector;

} SliderContext;


static void apply_slider_values(
    UIControls *ui,
    ShadowDetector *detector)
{
    if (!ui || !detector)
        return;

    float min_y =
        ui_controls_get_value(ui, SLIDER_MIN_Y);

    float max_y =
        ui_controls_get_value(ui, SLIDER_MAX_Y);

    int max_u =
        (int)(ui_controls_get_value(ui, SLIDER_MAX_U) + 0.5f);

    int max_v =
        (int)(ui_controls_get_value(ui, SLIDER_MAX_V) + 0.5f);

    int min_area =
        (int)(ui_controls_get_value(ui, SLIDER_MIN_AREA) + 0.5f);

    float bg_alpha =
        ui_controls_get_value(ui, SLIDER_BG_ALPHA);

    shadow_detector_set_parameters(
        detector,
        min_y,
        max_y,
        max_u,
        max_v,
        min_area);

    detector->background_alpha = bg_alpha;
}


static void slider_callback(
    int index,
    float value,
    void *userdata)
{
    (void)index;
    (void)value;

    SliderContext *ctx = (SliderContext *)userdata;

    if (!ctx)
        return;

    apply_slider_values(ctx->ui, ctx->detector);
}


/* ============================================================
 * MAIN
 * ============================================================ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n");
    printf("========================================\n");
    printf("       CAMERA SHADOW DETECTOR\n");
    printf("========================================\n\n");

    avdevice_register_all();


    /* ========================================================
     * CAMERAS
     * ======================================================== */

    CameraDevice *cameras = NULL;

    int camera_count =
        camera_list_devices(&cameras);

    if (camera_count <= 0)
    {
        fprintf(stderr, "\nERROR: No video cameras found.\n");
        return 1;
    }

    printf("========================================\n");
    printf("          AVAILABLE CAMERAS\n");
    printf("========================================\n");

    for (int i = 0; i < camera_count; i++)
    {
        printf("[%d] %s\n", i, cameras[i].description);
    }

    printf("========================================\n");

    int camera_index = 0;

    printf("Select camera [0-%d]: ", camera_count - 1);
    fflush(stdout);

    if (scanf("%d", &camera_index) != 1)
    {
        fprintf(stderr, "ERROR: Invalid camera number.\n");
        camera_free_devices(cameras, camera_count);
        return 1;
    }

    if (camera_index < 0 || camera_index >= camera_count)
    {
        fprintf(stderr, "ERROR: Camera index out of range.\n");
        camera_free_devices(cameras, camera_count);
        return 1;
    }

    printf("\nSelected camera: %s\n",
           cameras[camera_index].description);


    /* ========================================================
     * OPEN CAMERA
     * ======================================================== */

    CameraCapture capture;
    memset(&capture, 0, sizeof(capture));

    printf("\n----------------------------------------\n");
    printf("Opening camera...\n");
    printf("----------------------------------------\n");

    int ret =
        camera_open(
            &capture,
            &cameras[camera_index],
            WIDTH,
            HEIGHT,
            FPS);

    if (ret < 0)
    {
        fprintf(stderr, "\nERROR: Could not open camera.\n");
        camera_free_devices(cameras, camera_count);
        return 1;
    }

    camera_free_devices(cameras, camera_count);
    cameras = NULL;

    printf("\n[main] Camera opened successfully.\n");
    printf("[main] Resolution: %dx%d\n",
           capture.width, capture.height);

    const int frame_width = capture.width;
    const int frame_height = capture.height;

    if (frame_width <= 0 || frame_height <= 0)
    {
        fprintf(stderr, "ERROR: Invalid camera resolution.\n");
        camera_close(&capture);
        return 1;
    }


    /* ========================================================
     * BUFFERS
     * ======================================================== */

    const size_t y_size =
        (size_t)frame_width * (size_t)frame_height;

    const size_t uv_size =
        (size_t)(frame_width / 2) * (size_t)(frame_height / 2);

    uint8_t *y_plane = (uint8_t *)malloc(y_size);
    uint8_t *u_plane = (uint8_t *)malloc(uv_size);
    uint8_t *v_plane = (uint8_t *)malloc(uv_size);

    if (!y_plane || !u_plane || !v_plane)
    {
        fprintf(stderr, "ERROR: Could not allocate YUV buffers.\n");
        free(y_plane); free(u_plane); free(v_plane);
        camera_close(&capture);
        return 1;
    }

    uint8_t *rgb =
        (uint8_t *)malloc(y_size * 3);

    if (!rgb)
    {
        fprintf(stderr, "ERROR: Could not allocate RGB buffer.\n");
        free(y_plane); free(u_plane); free(v_plane);
        camera_close(&capture);
        return 1;
    }

    uint8_t *shadow_mask =
        (uint8_t *)malloc(y_size);

    if (!shadow_mask)
    {
        fprintf(stderr, "ERROR: Could not allocate shadow mask.\n");
        free(y_plane); free(u_plane); free(v_plane); free(rgb);
        camera_close(&capture);
        return 1;
    }

    memset(shadow_mask, 0, y_size);


    /* ========================================================
     * SHADOW DETECTOR
     * ======================================================== */

    ShadowDetector detector;
    memset(&detector, 0, sizeof(detector));

    if (shadow_detector_init(
            &detector,
            frame_width,
            frame_height) < 0)
    {
        fprintf(stderr,
                "ERROR: Could not initialize shadow detector.\n");
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }

    shadow_detector_set_parameters(
        &detector,
        MIN_Y_SHADOW,
        MAX_Y_SHADOW,
        MAX_U,
        MAX_V,
        MIN_SHADOW_AREA);

    detector.background_alpha = BACKGROUND_ALPHA;


    /* ========================================================
     * AVFRAME
     * ======================================================== */

    AVFrame *frame = av_frame_alloc();

    if (!frame)
    {
        fprintf(stderr, "ERROR: Could not allocate AVFrame.\n");
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }


    /* ========================================================
     * SWScale
     * ======================================================== */

    struct SwsContext *sws_yuv =
        sws_getContext(
            frame_width, frame_height,
            capture.codec_ctx->pix_fmt,
            frame_width, frame_height,
            AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL);

    if (!sws_yuv)
    {
        fprintf(stderr, "ERROR: Could not create YUV converter.\n");
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }

    struct SwsContext *sws_rgb =
        sws_getContext(
            frame_width, frame_height,
            capture.codec_ctx->pix_fmt,
            frame_width, frame_height,
            AV_PIX_FMT_RGB24,
            SWS_BILINEAR, NULL, NULL, NULL);

    if (!sws_rgb)
    {
        fprintf(stderr, "ERROR: Could not create RGB converter.\n");
        sws_freeContext(sws_yuv);
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }


    /* ========================================================
     * SDL
     * ======================================================== */

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;

    int total_width = frame_width + PANEL_WIDTH;

    if (init_sdl(
            &window,
            &renderer,
            &texture,
            frame_width,
            frame_height,
            total_width) < 0)
    {
        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }


    /* ========================================================
     * FONT
     * ======================================================== */

    const char *font_path = find_font();

    if (!font_path)
    {
        fprintf(stderr,
                "ERROR: Could not find a TTF font.\n"
                "Put arial.ttf next to the exe, or install\n"
                "a system font.\n");
        close_sdl(window, renderer, texture);
        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }

    printf("[main] Font: %s\n", font_path);


    /* ========================================================
     * UI
     * ======================================================== */

    UIControls ui;

    if (ui_controls_init(
            &ui,
            frame_width,
            frame_height,
            font_path,
            15) < 0)
    {
        fprintf(stderr, "ERROR: Could not initialize UI.\n");
        close_sdl(window, renderer, texture);
        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }

    ui_controls_add_slider(
        &ui, "Min Y shadow",
        0.01f, 0.90f,
        detector.min_y_ratio, 1);

    ui_controls_add_slider(
        &ui, "Max Y shadow",
        0.02f, 0.99f,
        detector.max_y_ratio, 1);

    ui_controls_add_slider(
        &ui, "Max U diff",
        1.0f, 60.0f,
        (float)detector.max_u_diff, 0);

    ui_controls_add_slider(
        &ui, "Max V diff",
        1.0f, 60.0f,
        (float)detector.max_v_diff, 0);

    ui_controls_add_slider(
        &ui, "Min shadow area",
        10.0f,
        (float)(frame_width * frame_height / 4),
        (float)detector.min_shadow_area, 0);

    ui_controls_add_slider(
        &ui, "Background alpha",
        0.001f, 0.100f,
        detector.background_alpha, 1);

    SliderContext slider_context;
    slider_context.ui = &ui;
    slider_context.detector = &detector;

    ui_controls_set_callback(
        &ui,
        slider_callback,
        &slider_context);


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
        fprintf(stderr, "ERROR: Could not create video encoder.\n");
        ui_controls_free(&ui);
        close_sdl(window, renderer, texture);
        sws_freeContext(sws_rgb);
        sws_freeContext(sws_yuv);
        av_frame_free(&frame);
        shadow_detector_free(&detector);
        free(y_plane); free(u_plane); free(v_plane);
        free(rgb); free(shadow_mask);
        camera_close(&capture);
        return 1;
    }

    printf("\nControls:\n");
    printf("  R     - reset background\n");
    printf("  TAB   - show/hide sliders\n");
    printf("  Q/ESC - quit\n\n");


    /* ========================================================
     * MAIN LOOP
     * ======================================================== */

    int running = 1;
    int frame_number = 0;
    int background_message_printed = 0;

    while (running)
    {
        ret = camera_read_frame(&capture, frame);

        if (ret < 0)
        {
            print_ffmpeg_error("camera_read_frame", ret);
            fprintf(stderr, "\n[main] Camera frame read failed.\n");
            break;
        }

        if (!frame->data[0] ||
            frame->width <= 0 ||
            frame->height <= 0)
        {
            fprintf(stderr, "[main] Invalid decoded frame.\n");
            break;
        }


        /* ====================================================
         * CAMERA -> YUV420P
         * ==================================================== */

        uint8_t *yuv_data[4] =
            { y_plane, u_plane, v_plane, NULL };

        int yuv_linesize[4] =
            { frame_width, frame_width / 2,
              frame_width / 2, 0 };

        int scale_result =
            sws_scale(
                sws_yuv,
                (const uint8_t * const *)frame->data,
                frame->linesize,
                0, frame_height,
                yuv_data, yuv_linesize);

        if (scale_result <= 0)
        {
            fprintf(stderr, "[main] YUV conversion failed.\n");
            break;
        }


        /* ====================================================
         * BACKGROUND
         * ==================================================== */

        if (!shadow_detector_background_ready(&detector))
        {
            shadow_detector_build_background(
                &detector,
                y_plane, u_plane, v_plane,
                frame_width,
                frame_width / 2,
                frame_width / 2);

            if (detector.background_frames == 1 ||
                detector.background_frames % 5 == 0)
            {
                printf("\r[background] Building: %d/%d frames",
                       detector.background_frames,
                       detector.background_min_frames);
                fflush(stdout);
            }

            background_message_printed = 0;
        }
        else
        {
            if (!background_message_printed)
            {
                printf("\n[background] READY.\n");
                background_message_printed = 1;
            }

            ret = shadow_detector_process(
                &detector,
                y_plane, u_plane, v_plane,
                frame_width,
                frame_width / 2,
                frame_width / 2,
                shadow_mask);

            if (ret < 0)
            {
                fprintf(stderr, "[main] Shadow detector failed.\n");
                break;
            }
        }


        /* ====================================================
         * CAMERA -> RGB24
         * ==================================================== */

        uint8_t *rgb_data[4] = { rgb, NULL, NULL, NULL };
        int rgb_linesize[4] = { frame_width * 3, 0, 0, 0 };

        scale_result =
            sws_scale(
                sws_rgb,
                (const uint8_t * const *)frame->data,
                frame->linesize,
                0, frame_height,
                rgb_data, rgb_linesize);

        if (scale_result <= 0)
        {
            fprintf(stderr, "[main] RGB conversion failed.\n");
            break;
        }


        /* ====================================================
         * MASK
         * ==================================================== */

        if (shadow_detector_background_ready(&detector))
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
            frame_width,
            frame_height);


        /* ====================================================
         * UI RENDER
         * ==================================================== */

        ui_controls_render(&ui, renderer);

        SDL_RenderPresent(renderer);


        /* ====================================================
         * RECORD
         * ==================================================== */

        ret = video_encoder_write_rgb24(
            encoder,
            rgb,
            frame_width * 3);

        if (ret < 0)
        {
            fprintf(stderr, "[main] Video encoder error.\n");
            break;
        }


        /* ====================================================
         * UPDATE BACKGROUND
         * ==================================================== */

        if (shadow_detector_background_ready(&detector))
        {
            shadow_detector_update_background(
                &detector,
                y_plane, u_plane, v_plane,
                frame_width,
                frame_width / 2,
                frame_width / 2);
        }


        /* ====================================================
         * STATS
         * ==================================================== */

        frame_number++;

        if ((frame_number % FPS) == 0)
        {
            printf(
                "\r[main] Frames: %d | Shadow: %d | Comp: %d | Valid: %d",
                frame_number,
                shadow_detector_get_shadow_pixels(&detector),
                shadow_detector_get_components(&detector),
                shadow_detector_get_valid_components(&detector));
            fflush(stdout);
        }


        /* ====================================================
         * EVENTS
         * ==================================================== */

        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            int ui_handled =
                ui_controls_handle_event(&ui, &event);

            if (ui_handled)
                continue;

            if (event.type == SDL_QUIT)
            {
                running = 0;
                break;
            }

            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_ESCAPE ||
                    event.key.keysym.sym == SDLK_q)
                {
                    running = 0;
                    break;
                }

                if (event.key.keysym.sym == SDLK_r)
                {
                    printf("\n\n[main] Resetting background...\n");
                    shadow_detector_reset_background(&detector);
                }
            }
        }
    }


    /* ========================================================
     * CLEANUP
     * ======================================================== */

    printf("\n\n[main] Stopping...\n");

    ui_controls_free(&ui);
    video_encoder_close(encoder);
    close_sdl(window, renderer, texture);
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

    printf("[main] Finished.\n");
    return 0;
}