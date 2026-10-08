#ifndef UI_CONTROLS_H
#define UI_CONTROLS_H

#include <stdint.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/*
 * Панель управления с ползунками
 * для SDL2.
 *
 * Возможности:
 *   - подпись ползунка
 *   - числовое значение
 *   - ввод значения с клавиатуры
 *     (клик по числу -> редактирование)
 *   - перетаскивание мыши
 *   - колесо мыши для тонкой подстройки
 */

#define UI_MAX_SLIDERS 12

typedef struct
{
    const char *label;

    float min_value;
    float max_value;
    float value;

    /*
     * 0 - целое
     * 1 - float (3 знака после запятой)
     */
    int is_float;

    /*
     * Внутреннее состояние перетаскивания.
     */
    int dragging;

    /*
     * Редактирование значения вручную.
     */
    int editing;
    char edit_buffer[32];
    int edit_cursor;

    /*
     * Кэш текстуры значения
     * (чтобы не пересоздавать каждый кадр).
     */
    char cached_text[64];
    SDL_Texture *cached_value_texture;
    int cached_value_w;
    int cached_value_h;

} UISlider;

typedef struct
{
    /*
     * Панель.
     */
    int panel_x;
    int panel_y;
    int panel_width;
    int panel_height;

    int visible;

    /*
     * Ползунки.
     */
    UISlider sliders[UI_MAX_SLIDERS];
    int slider_count;

    /*
     * Активный ползунок
     * (для перетаскивания).
     */
    int active_slider;

    /*
     * Шрифт.
     */
    TTF_Font *font;

    /*
     * Callback при изменении.
     */
    void (*on_change)(
        int index,
        float value,
        void *userdata);

    void *userdata;

} UIControls;


/*
 * Инициализация.
 *
 * font_path - путь к TTF файлу.
 *
 * Панель размещается справа
 * от области видео.
 * video_width - ширина видео,
 * screen_height - высота окна.
 */
int ui_controls_init(
    UIControls *ui,
    int video_width,
    int screen_height,
    const char *font_path,
    int font_size);


/*
 * Добавить ползунок.
 */
int ui_controls_add_slider(
    UIControls *ui,
    const char *label,
    float min_value,
    float max_value,
    float value,
    int is_float);


/*
 * Callback.
 */
void ui_controls_set_callback(
    UIControls *ui,
    void (*on_change)(
        int index,
        float value,
        void *userdata),
    void *userdata);


/*
 * Значение ползунка.
 */
float ui_controls_get_value(
    const UIControls *ui,
    int index);


/*
 * Установить значение
 * (без вызова callback).
 */
void ui_controls_set_value(
    UIControls *ui,
    int index,
    float value);


/*
 * Обработка SDL события.
 *
 * Возвращает 1, если событие
 * было поглощено панелью.
 */
int ui_controls_handle_event(
    UIControls *ui,
    const SDL_Event *event);


/*
 * Отрисовка панели.
 */
void ui_controls_render(
    UIControls *ui,
    SDL_Renderer *renderer);


/*
 * Показать/скрыть панель.
 */
void ui_controls_toggle(
    UIControls *ui);


/*
 * Освобождение ресурсов.
 */
void ui_controls_free(
    UIControls *ui);


#endif