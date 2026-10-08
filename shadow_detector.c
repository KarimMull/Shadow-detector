#include "shadow_detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ============================================================
 * DEFAULT SETTINGS
 * ============================================================ */

#define DEFAULT_BACKGROUND_ALPHA       0.01f
#define DEFAULT_BACKGROUND_FRAMES      30

#define DEFAULT_MIN_Y_RATIO            0.15f
#define DEFAULT_MAX_Y_RATIO            0.75f

#define DEFAULT_MAX_U_DIFF             18
#define DEFAULT_MAX_V_DIFF             18

#define DEFAULT_UV_ADAPTIVE_FACTOR     0.25f

#define DEFAULT_MIN_SHADOW_AREA        100

#define DEFAULT_MAX_COMPONENTS         4096

#define MIN_BACKGROUND_Y               5.0f

#define SHADOW_VALUE                   255
#define NON_SHADOW_VALUE               0


/* ============================================================
 * HELPERS
 * ============================================================ */

static int clamp_int(
    int value,
    int min_value,
    int max_value)
{
    if (value < min_value)
        return min_value;

    if (value > max_value)
        return max_value;

    return value;
}


static float clamp_float(
    float value,
    float min_value,
    float max_value)
{
    if (value < min_value)
        return min_value;

    if (value > max_value)
        return max_value;

    return value;
}


static int abs_int(int value)
{
    return value < 0 ? -value : value;
}


static int pixel_index(
    int x,
    int y,
    int width)
{
    return y * width + x;
}


/* ============================================================
 * MEMORY
 * ============================================================ */

static int allocate_buffers(
    ShadowDetector *detector)
{
    size_t pixels;

    if (!detector)
        return -1;

    pixels =
        (size_t)detector->width *
        (size_t)detector->height;

    detector->background_y =
        (float *)calloc(
            pixels,
            sizeof(float));

    detector->background_u =
        (float *)calloc(
            pixels,
            sizeof(float));

    detector->background_v =
        (float *)calloc(
            pixels,
            sizeof(float));

    detector->mask =
        (uint8_t *)calloc(
            pixels,
            sizeof(uint8_t));

    detector->temp_mask =
        (uint8_t *)calloc(
            pixels,
            sizeof(uint8_t));

    detector->component_labels =
        (int *)calloc(
            pixels,
            sizeof(int));

    detector->component_queue =
        (int *)malloc(
            pixels * sizeof(int));

    detector->component_sizes =
        (int *)malloc(
            (size_t)detector->max_components *
            sizeof(int));

    if (!detector->background_y ||
        !detector->background_u ||
        !detector->background_v ||
        !detector->mask ||
        !detector->temp_mask ||
        !detector->component_labels ||
        !detector->component_queue ||
        !detector->component_sizes)
    {
        return -1;
    }

    return 0;
}


/* ============================================================
 * INIT
 * ============================================================ */

int shadow_detector_init(
    ShadowDetector *detector,
    int width,
    int height)
{
    if (!detector)
        return -1;

    if (width <= 0 || height <= 0)
        return -1;

    memset(
        detector,
        0,
        sizeof(*detector));

    detector->width = width;
    detector->height = height;

    detector->background_alpha =
        DEFAULT_BACKGROUND_ALPHA;

    detector->background_min_frames =
        DEFAULT_BACKGROUND_FRAMES;

    detector->min_y_ratio =
        DEFAULT_MIN_Y_RATIO;

    detector->max_y_ratio =
        DEFAULT_MAX_Y_RATIO;

    detector->max_u_diff =
        DEFAULT_MAX_U_DIFF;

    detector->max_v_diff =
        DEFAULT_MAX_V_DIFF;

    detector->uv_adaptive_factor =
        DEFAULT_UV_ADAPTIVE_FACTOR;

    detector->min_shadow_area =
        DEFAULT_MIN_SHADOW_AREA;

    detector->max_components =
        DEFAULT_MAX_COMPONENTS;

    if (allocate_buffers(detector) < 0)
    {
        shadow_detector_free(detector);
        return -1;
    }

    return 0;
}


/* ============================================================
 * BUILD INITIAL BACKGROUND
 * ============================================================ */

int shadow_detector_build_background(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride)
{
    int x;
    int yy;

    if (!detector ||
        !y ||
        !u ||
        !v)
    {
        return -1;
    }

    if (y_stride <= 0 ||
        u_stride <= 0 ||
        v_stride <= 0)
    {
        return -1;
    }

    /*
     * Первый кадр копируем напрямую.
     */
    if (detector->accumulated_frames == 0)
    {
        for (yy = 0;
             yy < detector->height;
             yy++)
        {
            const uint8_t *y_row =
                y + yy * y_stride;

            const uint8_t *u_row =
                u + (yy / 2) * u_stride;

            const uint8_t *v_row =
                v + (yy / 2) * v_stride;

            for (x = 0;
                 x < detector->width;
                 x++)
            {
                int index =
                    pixel_index(
                        x,
                        yy,
                        detector->width);

                detector->background_y[index] =
                    (float)y_row[x];

                detector->background_u[index] =
                    (float)u_row[x / 2];

                detector->background_v[index] =
                    (float)v_row[x / 2];
            }
        }

        detector->accumulated_frames = 1;
        detector->background_frames = 1;

        return 0;
    }

    /*
     * Последующие кадры.
     *
     * Используем background_alpha.
     */
    float alpha =
        clamp_float(
            detector->background_alpha,
            0.0001f,
            1.0f);

    float one_minus_alpha =
        1.0f - alpha;

    for (yy = 0;
         yy < detector->height;
         yy++)
    {
        const uint8_t *y_row =
            y + yy * y_stride;

        const uint8_t *u_row =
            u + (yy / 2) * u_stride;

        const uint8_t *v_row =
            v + (yy / 2) * v_stride;

        for (x = 0;
             x < detector->width;
             x++)
        {
            int index =
                pixel_index(
                    x,
                    yy,
                    detector->width);

            float current_y =
                (float)y_row[x];

            float current_u =
                (float)u_row[x / 2];

            float current_v =
                (float)v_row[x / 2];

            detector->background_y[index] =
                detector->background_y[index] *
                one_minus_alpha +
                current_y * alpha;

            detector->background_u[index] =
                detector->background_u[index] *
                one_minus_alpha +
                current_u * alpha;

            detector->background_v[index] =
                detector->background_v[index] *
                one_minus_alpha +
                current_v * alpha;
        }
    }

    detector->accumulated_frames++;
    detector->background_frames++;

    return 0;
}


/* ============================================================
 * BACKGROUND READY
 * ============================================================ */

int shadow_detector_background_ready(
    const ShadowDetector *detector)
{
    if (!detector)
        return 0;

    return detector->background_frames >=
           detector->background_min_frames;
}


/* ============================================================
 * CREATE SHADOW CANDIDATES
 * ============================================================ */

static void create_shadow_candidates(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride)
{
    int x;
    int yy;

    for (yy = 0;
         yy < detector->height;
         yy++)
    {
        const uint8_t *y_row =
            y + yy * y_stride;

        const uint8_t *u_row =
            u + (yy / 2) * u_stride;

        const uint8_t *v_row =
            v + (yy / 2) * v_stride;

        for (x = 0;
             x < detector->width;
             x++)
        {
            int index =
                pixel_index(
                    x,
                    yy,
                    detector->width);

            float background_y =
                detector->background_y[index];

            float background_u =
                detector->background_u[index];

            float background_v =
                detector->background_v[index];

            float current_y =
                (float)y_row[x];

            float current_u =
                (float)u_row[x / 2];

            float current_v =
                (float)v_row[x / 2];

            /*
             * Защита от деления на слишком
             * маленький background Y.
             */
            if (background_y < MIN_BACKGROUND_Y)
            {
                detector->mask[index] =
                    NON_SHADOW_VALUE;

                continue;
            }

            /*
             * Относительная яркость.
             *
             * 1.0 = яркость не изменилась
             * 0.8 = стало на 20% темнее
             * 0.5 = стало на 50% темнее
             */
            float y_ratio =
                current_y / background_y;

            int u_diff =
                abs_int(
                    (int)current_u -
                    (int)background_u);

            int v_diff =
                abs_int(
                    (int)current_v -
                    (int)background_v);

            /*
             * Насколько сильно потемнел пиксель.
             */
            float darkness =
                1.0f - y_ratio;

            if (darkness < 0.0f)
                darkness = 0.0f;

            /*
             * Для более тёмных областей
             * допускаем немного большую
             * разницу U/V.
             */
            int adaptive_u =
                (int)(
                    detector->max_u_diff +
                    darkness *
                    detector->uv_adaptive_factor *
                    255.0f);

            int adaptive_v =
                (int)(
                    detector->max_v_diff +
                    darkness *
                    detector->uv_adaptive_factor *
                    255.0f);

            adaptive_u =
                clamp_int(
                    adaptive_u,
                    detector->max_u_diff,
                    255);

            adaptive_v =
                clamp_int(
                    adaptive_v,
                    detector->max_v_diff,
                    255);

            /*
             * Условия тени.
             *
             * Минимальное затемнение:
             *
             * y_ratio <= 1 - min_y_ratio
             *
             * Максимальное затемнение:
             *
             * y_ratio >= 1 - max_y_ratio
             */
            int shadow =
                (y_ratio <=
                 1.0f -
                 detector->min_y_ratio)
                &&
                (y_ratio >=
                 1.0f -
                 detector->max_y_ratio)
                &&
                (u_diff <= adaptive_u)
                &&
                (v_diff <= adaptive_v);

            detector->mask[index] =
                shadow
                    ? SHADOW_VALUE
                    : NON_SHADOW_VALUE;
        }
    }
}


/* ============================================================
 * MORPHOLOGICAL EROSION 3x3
 * ============================================================ */

static void morphology_erode(
    const uint8_t *src,
    uint8_t *dst,
    int width,
    int height)
{
    int x;
    int y;

    memset(
        dst,
        0,
        (size_t)width *
        (size_t)height);

    for (y = 1;
         y < height - 1;
         y++)
    {
        for (x = 1;
             x < width - 1;
             x++)
        {
            int all_set = 1;

            int dy;
            int dx;

            for (dy = -1;
                 dy <= 1;
                 dy++)
            {
                for (dx = -1;
                     dx <= 1;
                     dx++)
                {
                    int index =
                        pixel_index(
                            x + dx,
                            y + dy,
                            width);

                    if (src[index] == 0)
                    {
                        all_set = 0;
                        break;
                    }
                }

                if (!all_set)
                    break;
            }

            if (all_set)
            {
                dst[
                    pixel_index(
                        x,
                        y,
                        width)
                ] = SHADOW_VALUE;
            }
        }
    }
}


/* ============================================================
 * MORPHOLOGICAL DILATION 3x3
 * ============================================================ */

static void morphology_dilate(
    const uint8_t *src,
    uint8_t *dst,
    int width,
    int height)
{
    int x;
    int y;

    memset(
        dst,
        0,
        (size_t)width *
        (size_t)height);

    for (y = 1;
         y < height - 1;
         y++)
    {
        for (x = 1;
             x < width - 1;
             x++)
        {
            int found = 0;

            int dy;
            int dx;

            for (dy = -1;
                 dy <= 1;
                 dy++)
            {
                for (dx = -1;
                     dx <= 1;
                     dx++)
                {
                    int index =
                        pixel_index(
                            x + dx,
                            y + dy,
                            width);

                    if (src[index] != 0)
                    {
                        found = 1;
                        break;
                    }
                }

                if (found)
                    break;
            }

            if (found)
            {
                dst[
                    pixel_index(
                        x,
                        y,
                        width)
                ] = SHADOW_VALUE;
            }
        }
    }
}


/* ============================================================
 * OPENING
 *
 * erosion -> dilation
 *
 * Удаляет мелкий шум.
 * ============================================================ */

static void morphology_opening(
    ShadowDetector *detector)
{
    morphology_erode(
        detector->mask,
        detector->temp_mask,
        detector->width,
        detector->height);

    morphology_dilate(
        detector->temp_mask,
        detector->mask,
        detector->width,
        detector->height);
}


/* ============================================================
 * CLOSING
 *
 * dilation -> erosion
 *
 * Заполняет небольшие отверстия.
 * ============================================================ */

static void morphology_closing(
    ShadowDetector *detector)
{
    morphology_dilate(
        detector->mask,
        detector->temp_mask,
        detector->width,
        detector->height);

    morphology_erode(
        detector->temp_mask,
        detector->mask,
        detector->width,
        detector->height);
}


/* ============================================================
 * CONNECTED COMPONENT
 * ============================================================ */

static int find_component(
    ShadowDetector *detector,
    int start_index)
{
    const int width =
        detector->width;

    const int height =
        detector->height;

    int queue_size = 0;
    int component_size = 0;

    int *queue =
        detector->component_queue;

    int *labels =
        detector->component_labels;

    queue[queue_size++] =
        start_index;

    labels[start_index] = 1;

    while (queue_size > 0)
    {
        int current =
            queue[--queue_size];

        int x =
            current % width;

        int y =
            current / width;

        component_size++;

        /*
         * 4-связность.
         */
        const int neighbours[4][2] =
        {
            {  1,  0 },
            { -1,  0 },
            {  0,  1 },
            {  0, -1 }
        };

        int i;

        for (i = 0;
             i < 4;
             i++)
        {
            int nx =
                x + neighbours[i][0];

            int ny =
                y + neighbours[i][1];

            if (nx < 0 ||
                nx >= width ||
                ny < 0 ||
                ny >= height)
            {
                continue;
            }

            int index =
                pixel_index(
                    nx,
                    ny,
                    width);

            if (detector->mask[index] == 0)
                continue;

            if (labels[index] != 0)
                continue;

            if (queue_size >=
                width * height)
            {
                continue;
            }

            labels[index] = 1;

            queue[queue_size++] =
                index;
        }
    }

    (void)height;

    return component_size;
}


/* ============================================================
 * CONNECTED COMPONENT FILTER
 * ============================================================ */

static void filter_components(
    ShadowDetector *detector)
{
    int x;
    int y;

    int component_count = 0;
    int valid_components = 0;

    int width =
        detector->width;

    int height =
        detector->height;

    size_t pixels =
        (size_t)width *
        (size_t)height;

    /*
     * Первый проход.
     *
     * Находим размеры компонент.
     */
    memset(
        detector->component_labels,
        0,
        pixels * sizeof(int));

    for (y = 0;
         y < height;
         y++)
    {
        for (x = 0;
             x < width;
             x++)
        {
            int index =
                pixel_index(
                    x,
                    y,
                    width);

            if (detector->mask[index] == 0)
                continue;

            if (detector->component_labels[index] != 0)
                continue;

            if (component_count >=
                detector->max_components)
            {
                detector->mask[index] = 0;
                continue;
            }

            int size =
                find_component(
                    detector,
                    index);

            detector->component_sizes[
                component_count
            ] = size;

            component_count++;
        }
    }

    /*
     * Второй проход.
     *
     * Удаляем маленькие компоненты.
     */
    memset(
        detector->component_labels,
        0,
        pixels * sizeof(int));

    int component_index = 0;

    for (y = 0;
         y < height;
         y++)
    {
        for (x = 0;
             x < width;
             x++)
        {
            int start =
                pixel_index(
                    x,
                    y,
                    width);

            if (detector->mask[start] == 0)
                continue;

            if (detector->component_labels[start] != 0)
                continue;

            if (component_index >=
                component_count)
            {
                detector->mask[start] = 0;
                continue;
            }

            int component_size =
                detector->component_sizes[
                    component_index
                ];

            int queue_size = 0;

            detector->component_queue[
                queue_size++
            ] = start;

            detector->component_labels[start] =
                1;

            if (component_size <
                detector->min_shadow_area)
            {
                /*
                 * Удаляем всю компоненту.
                 */
                while (queue_size > 0)
                {
                    int current =
                        detector->component_queue[
                            --queue_size
                        ];

                    int cx =
                        current % width;

                    int cy =
                        current / width;

                    detector->mask[current] =
                        NON_SHADOW_VALUE;

                    const int neighbours[4][2] =
                    {
                        {  1,  0 },
                        { -1,  0 },
                        {  0,  1 },
                        {  0, -1 }
                    };

                    int n;

                    for (n = 0;
                         n < 4;
                         n++)
                    {
                        int nx =
                            cx +
                            neighbours[n][0];

                        int ny =
                            cy +
                            neighbours[n][1];

                        if (nx < 0 ||
                            nx >= width ||
                            ny < 0 ||
                            ny >= height)
                        {
                            continue;
                        }

                        int ni =
                            pixel_index(
                                nx,
                                ny,
                                width);

                        if (detector->mask[ni] == 0)
                            continue;

                        if (detector->component_labels[ni] != 0)
                            continue;

                        detector->component_labels[ni] =
                            1;

                        detector->component_queue[
                            queue_size++
                        ] = ni;
                    }
                }
            }
            else
            {
                /*
                 * Компонента достаточно большая.
                 *
                 * Оставляем её.
                 */
                valid_components++;

                while (queue_size > 0)
                {
                    int current =
                        detector->component_queue[
                            --queue_size
                        ];

                    int cx =
                        current % width;

                    int cy =
                        current / width;

                    const int neighbours[4][2] =
                    {
                        {  1,  0 },
                        { -1,  0 },
                        {  0,  1 },
                        {  0, -1 }
                    };

                    int n;

                    for (n = 0;
                         n < 4;
                         n++)
                    {
                        int nx =
                            cx +
                            neighbours[n][0];

                        int ny =
                            cy +
                            neighbours[n][1];

                        if (nx < 0 ||
                            nx >= width ||
                            ny < 0 ||
                            ny >= height)
                        {
                            continue;
                        }

                        int ni =
                            pixel_index(
                                nx,
                                ny,
                                width);

                        if (detector->mask[ni] == 0)
                            continue;

                        if (detector->component_labels[ni] != 0)
                            continue;

                        detector->component_labels[ni] =
                            1;

                        detector->component_queue[
                            queue_size++
                        ] = ni;
                    }
                }
            }

            component_index++;
        }
    }

    detector->last_components =
        component_count;

    detector->last_valid_components =
        valid_components;
}


/* ============================================================
 * COUNT SHADOW PIXELS
 * ============================================================ */

static int count_shadow_pixels(
    const ShadowDetector *detector)
{
    size_t pixels =
        (size_t)detector->width *
        (size_t)detector->height;

    size_t i;
    int count = 0;

    for (i = 0;
         i < pixels;
         i++)
    {
        if (detector->mask[i] != 0)
            count++;
    }

    return count;
}


/* ============================================================
 * MAIN DETECTION
 * ============================================================ */

int shadow_detector_process(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride,
    uint8_t *shadow_mask)
{
    if (!detector ||
        !y ||
        !u ||
        !v ||
        !shadow_mask)
    {
        return -1;
    }

    if (!shadow_detector_background_ready(detector))
    {
        memset(
            shadow_mask,
            0,
            (size_t)detector->width *
            (size_t)detector->height);

        return 0;
    }

    /*
     * 1. Кандидаты теней.
     */
    create_shadow_candidates(
        detector,
        y,
        u,
        v,
        y_stride,
        u_stride,
        v_stride);

    /*
     * 2. Opening.
     *
     * Удаляет шум.
     */
    morphology_opening(detector);

    /*
     * 3. Closing.
     *
     * Заполняет небольшие дырки.
     */
    morphology_closing(detector);

    /*
     * 4. Connected components.
     */
    filter_components(detector);

    /*
     * 5. Результат.
     */
    memcpy(
        shadow_mask,
        detector->mask,
        (size_t)detector->width *
        (size_t)detector->height);

    /*
     * 6. Статистика.
     */
    detector->last_shadow_pixels =
        count_shadow_pixels(detector);

    return 0;
}


/* ============================================================
 * ADAPTIVE BACKGROUND
 * ============================================================ */

void shadow_detector_update_background(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride)
{
    int x;
    int yy;

    if (!detector ||
        !y ||
        !u ||
        !v)
    {
        return;
    }

    float alpha =
        clamp_float(
            detector->background_alpha,
            0.0001f,
            1.0f);

    float one_minus_alpha =
        1.0f - alpha;

    for (yy = 0;
         yy < detector->height;
         yy++)
    {
        const uint8_t *y_row =
            y + yy * y_stride;

        const uint8_t *u_row =
            u + (yy / 2) * u_stride;

        const uint8_t *v_row =
            v + (yy / 2) * v_stride;

        for (x = 0;
             x < detector->width;
             x++)
        {
            int index =
                pixel_index(
                    x,
                    yy,
                    detector->width);

            /*
             * КЛЮЧЕВОЙ МОМЕНТ:
             *
             * если пиксель является тенью,
             * он НЕ обновляет background.
             */
            if (detector->mask[index] != 0)
                continue;

            float current_y =
                (float)y_row[x];

            float current_u =
                (float)u_row[x / 2];

            float current_v =
                (float)v_row[x / 2];

            detector->background_y[index] =
                detector->background_y[index] *
                one_minus_alpha +
                current_y * alpha;

            detector->background_u[index] =
                detector->background_u[index] *
                one_minus_alpha +
                current_u * alpha;

            detector->background_v[index] =
                detector->background_v[index] *
                one_minus_alpha +
                current_v * alpha;
        }
    }

    detector->accumulated_frames++;
}


/* ============================================================
 * RESET BACKGROUND
 * ============================================================ */

void shadow_detector_reset_background(
    ShadowDetector *detector)
{
    if (!detector)
        return;

    size_t pixels =
        (size_t)detector->width *
        (size_t)detector->height;

    memset(
        detector->background_y,
        0,
        pixels * sizeof(float));

    memset(
        detector->background_u,
        0,
        pixels * sizeof(float));

    memset(
        detector->background_v,
        0,
        pixels * sizeof(float));

    memset(
        detector->mask,
        0,
        pixels);

    memset(
        detector->temp_mask,
        0,
        pixels);

    memset(
        detector->component_labels,
        0,
        pixels * sizeof(int));

    detector->background_frames = 0;
    detector->accumulated_frames = 0;

    detector->last_shadow_pixels = 0;
    detector->last_components = 0;
    detector->last_valid_components = 0;
}


/* ============================================================
 * PARAMETERS
 * ============================================================ */

void shadow_detector_set_parameters(
    ShadowDetector *detector,
    float y_min_ratio,
    float y_max_ratio,
    int max_u,
    int max_v,
    int min_area)
{
    if (!detector)
        return;

    detector->min_y_ratio =
        clamp_float(
            y_min_ratio,
            0.01f,
            0.95f);

    detector->max_y_ratio =
        clamp_float(
            y_max_ratio,
            detector->min_y_ratio,
            0.99f);

    detector->max_u_diff =
        clamp_int(
            max_u,
            1,
            255);

    detector->max_v_diff =
        clamp_int(
            max_v,
            1,
            255);

    detector->min_shadow_area =
        clamp_int(
            min_area,
            1,
            detector->width *
            detector->height);
}


/* ============================================================
 * GETTERS
 * ============================================================ */

int shadow_detector_get_shadow_pixels(
    const ShadowDetector *detector)
{
    if (!detector)
        return 0;

    return detector->last_shadow_pixels;
}


int shadow_detector_get_components(
    const ShadowDetector *detector)
{
    if (!detector)
        return 0;

    return detector->last_components;
}


int shadow_detector_get_valid_components(
    const ShadowDetector *detector)
{
    if (!detector)
        return 0;

    return detector->last_valid_components;
}


/* ============================================================
 * FREE
 * ============================================================ */

void shadow_detector_free(
    ShadowDetector *detector)
{
    if (!detector)
        return;

    free(detector->background_y);
    free(detector->background_u);
    free(detector->background_v);

    free(detector->mask);
    free(detector->temp_mask);

    free(detector->component_queue);
    free(detector->component_sizes);
    free(detector->component_labels);

    memset(
        detector,
        0,
        sizeof(*detector));
}