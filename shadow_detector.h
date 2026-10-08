#ifndef SHADOW_DETECTOR_H
#define SHADOW_DETECTOR_H

#include <stdint.h>

typedef struct
{
    int width;
    int height;

    /* ========================================================
     * BACKGROUND
     * ======================================================== */

    float *background_y;
    float *background_u;
    float *background_v;

    int background_frames;
    int accumulated_frames;

    /*
     * Скорость адаптации фона.
     *
     * 0.005 - очень медленно
     * 0.01  - нормальный режим
     * 0.02  - быстро
     */
    float background_alpha;

    /*
     * Сколько кадров требуется для
     * первоначального построения фона.
     */
    int background_min_frames;

    /* ========================================================
     * SHADOW PARAMETERS
     * ======================================================== */

    /*
     * Минимальное относительное затемнение.
     *
     * 0.15 = пиксель должен стать минимум на 15% темнее.
     */
    float min_y_ratio;

    /*
     * Максимальное относительное затемнение.
     *
     * 0.75 = допускается затемнение до 75%.
     *
     * Это помогает не принимать очень тёмные объекты
     * за тени.
     */
    float max_y_ratio;

    /*
     * Максимальная разница цветовых компонентов.
     */
    int max_u_diff;
    int max_v_diff;

    /*
     * Дополнительная адаптация U/V
     * для более тёмных участков.
     */
    float uv_adaptive_factor;

    /*
     * Минимальная площадь связной области.
     */
    int min_shadow_area;

    /*
     * Максимальное количество компонент.
     */
    int max_components;

    /* ========================================================
     * TEMPORARY BUFFERS
     * ======================================================== */

    uint8_t *mask;
    uint8_t *temp_mask;

    /*
     * Connected Components.
     */
    int *component_queue;
    int *component_sizes;
    int *component_labels;

    /* ========================================================
     * STATISTICS
     * ======================================================== */

    int last_shadow_pixels;
    int last_components;
    int last_valid_components;

} ShadowDetector;


/*
 * Инициализация.
 */
int shadow_detector_init(
    ShadowDetector *detector,
    int width,
    int height
);


/*
 * Построение первоначального background.
 */
int shadow_detector_build_background(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride
);


/*
 * Проверка готовности background.
 *
 * 1 = готов
 * 0 = ещё строится.
 */
int shadow_detector_background_ready(
    const ShadowDetector *detector
);


/*
 * Основное обнаружение теней.
 *
 * shadow_mask:
 *
 * 0   = обычный пиксель
 * 255 = тень
 */
int shadow_detector_process(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride,
    uint8_t *shadow_mask
);


/*
 * Адаптивное обновление background.
 *
 * Области тени НЕ обновляют background.
 */
void shadow_detector_update_background(
    ShadowDetector *detector,
    const uint8_t *y,
    const uint8_t *u,
    const uint8_t *v,
    int y_stride,
    int u_stride,
    int v_stride
);


/*
 * Полный сброс background.
 */
void shadow_detector_reset_background(
    ShadowDetector *detector
);


/*
 * Освобождение памяти.
 */
void shadow_detector_free(
    ShadowDetector *detector
);


/*
 * Настройка чувствительности.
 *
 * y_min_ratio:
 *     минимальное затемнение.
 *
 * y_max_ratio:
 *     максимальное затемнение.
 *
 * max_u:
 *     максимальное изменение U.
 *
 * max_v:
 *     максимальное изменение V.
 *
 * min_area:
 *     минимальная площадь тени.
 */
void shadow_detector_set_parameters(
    ShadowDetector *detector,
    float y_min_ratio,
    float y_max_ratio,
    int max_u,
    int max_v,
    int min_area
);


/*
 * Статистика.
 */
int shadow_detector_get_shadow_pixels(
    const ShadowDetector *detector
);

int shadow_detector_get_components(
    const ShadowDetector *detector
);

int shadow_detector_get_valid_components(
    const ShadowDetector *detector
);

#endif