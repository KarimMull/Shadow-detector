#include "ui_controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>


/* ============================================================
 * КОНСТАНТЫ
 * ============================================================ */

#define SLIDER_SPACING       70
#define SLIDER_TOP_MARGIN    60
#define SLIDER_SIDE_MARGIN   15

#define LABEL_HEIGHT         20
#define VALUE_HEIGHT         28

#define TRACK_HEIGHT          8
#define KNOB_WIDTH           12
#define KNOB_HEIGHT          22

#define VALUE_BOX_WIDTH     110
#define VALUE_BOX_HEIGHT     26

#define BG_COLOR_R           25
#define BG_COLOR_G           25
#define BG_COLOR_B           35
#define BG_COLOR_A          220

#define LABEL_COLOR_R       230
#define LABEL_COLOR_G       230
#define LABEL_COLOR_B       230
#define LABEL_COLOR_A       255

#define VALUE_COLOR_R       255
#define VALUE_COLOR_G       220
#define VALUE_COLOR_B       120
#define VALUE_COLOR_A       255

#define VALUE_BOX_R          40
#define VALUE_BOX_G          40
#define VALUE_BOX_B          55
#define VALUE_BOX_A         255

#define VALUE_BOX_EDIT_R     80
#define VALUE_BOX_EDIT_G     60
#define VALUE_BOX_EDIT_B    130
#define VALUE_BOX_EDIT_A    255

#define TRACK_BG_R           70
#define TRACK_BG_G           70
#define TRACK_BG_B           85
#define TRACK_BG_A          255

#define TRACK_FILL_R         80
#define TRACK_FILL_G        180
#define TRACK_FILL_B        255
#define TRACK_FILL_A        255

#define KNOB_R              230
#define KNOB_G              230
#define KNOB_B              240
#define KNOB_A              255


/* ============================================================
 * HELPERS
 * ============================================================ */

static void free_cached_texture(
    UISlider *slider)
{
    if (!slider)
        return;

    if (slider->cached_value_texture)
    {
        SDL_DestroyTexture(
            slider->cached_value_texture);

        slider->cached_value_texture = NULL;
    }

    slider->cached_value_w = 0;
    slider->cached_value_h = 0;
    slider->cached_text[0] = '\0';
}


static void format_value(
    const UISlider *slider,
    char *buffer,
    size_t size)
{
    if (slider->is_float)
    {
        snprintf(
            buffer,
            size,
            "%.3f",
            slider->value);
    }
    else
    {
        snprintf(
            buffer,
            size,
            "%d",
            (int)(slider->value + 0.5f));
    }
}


static int value_to_x(
    const UIControls *ui,
    const UISlider *slider)
{
    int track_x =
        ui->panel_x + SLIDER_SIDE_MARGIN;
    int track_w =
        ui->panel_width - 2 * SLIDER_SIDE_MARGIN;

    float ratio =
        (slider->value - slider->min_value) /
        (slider->max_value - slider->min_value);

    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    return track_x +
           (int)(ratio * (float)track_w);
}


static float x_to_value(
    const UIControls *ui,
    const UISlider *slider,
    int x)
{
    int track_x =
        ui->panel_x + SLIDER_SIDE_MARGIN;
    int track_w =
        ui->panel_width - 2 * SLIDER_SIDE_MARGIN;

    float ratio =
        (float)(x - track_x) /
        (float)track_w;

    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    return slider->min_value +
           ratio * (slider->max_value - slider->min_value);
}


static int slider_y(
    const UIControls *ui,
    int index)
{
    return ui->panel_y +
           SLIDER_TOP_MARGIN +
           index * SLIDER_SPACING;
}


/*
 * Прямоугольник числового поля.
 */
static SDL_Rect value_box_rect(
    const UIControls *ui,
    int index)
{
    SDL_Rect rect;

    rect.w = VALUE_BOX_WIDTH;
    rect.h = VALUE_BOX_HEIGHT;

    rect.x =
        ui->panel_x +
        ui->panel_width -
        SLIDER_SIDE_MARGIN -
        VALUE_BOX_WIDTH;

    rect.y =
        slider_y(ui, index) +
        LABEL_HEIGHT - 2;

    return rect;
}


/* ============================================================
 * INIT
 * ============================================================ */

int ui_controls_init(
    UIControls *ui,
    int video_width,
    int screen_height,
    const char *font_path,
    int font_size)
{
    if (!ui)
        return -1;

    memset(
        ui,
        0,
        sizeof(*ui));

    ui->panel_width = 340;

    ui->panel_height = screen_height;

    ui->panel_x = video_width;
    ui->panel_y = 0;

    if (ui->panel_x + ui->panel_width >
        video_width + ui->panel_width)
    {
        /* всё нормально */
    }

    ui->visible = 1;
    ui->active_slider = -1;

    /*
     * Шрифт.
     */
    if (TTF_Init() != 0)
    {
        fprintf(
            stderr,
            "TTF_Init failed: %s\n",
            TTF_GetError());

        return -1;
    }

    if (!font_path)
    {
        fprintf(
            stderr,
            "ui_controls_init: font_path is NULL\n");

        return -1;
    }

    ui->font =
        TTF_OpenFont(
            font_path,
            font_size);

    if (!ui->font)
    {
        fprintf(
            stderr,
            "TTF_OpenFont(%s) failed: %s\n",
            font_path,
            TTF_GetError());

        return -1;
    }

    return 0;
}


/* ============================================================
 * ADD SLIDER
 * ============================================================ */

int ui_controls_add_slider(
    UIControls *ui,
    const char *label,
    float min_value,
    float max_value,
    float value,
    int is_float)
{
    if (!ui)
        return -1;

    if (ui->slider_count >= UI_MAX_SLIDERS)
        return -1;

    if (max_value <= min_value)
        return -1;

    int index = ui->slider_count;

    UISlider *slider =
        &ui->sliders[index];

    memset(
        slider,
        0,
        sizeof(*slider));

    slider->label = label;

    slider->min_value = min_value;
    slider->max_value = max_value;
    slider->value = value;

    slider->is_float = is_float;
    slider->dragging = 0;
    slider->editing = 0;
    slider->edit_buffer[0] = '\0';
    slider->edit_cursor = 0;

    ui->slider_count++;

    return index;
}


/* ============================================================
 * CALLBACK
 * ============================================================ */

void ui_controls_set_callback(
    UIControls *ui,
    void (*on_change)(
        int index,
        float value,
        void *userdata),
    void *userdata)
{
    if (!ui)
        return;

    ui->on_change = on_change;
    ui->userdata = userdata;
}


/* ============================================================
 * GET / SET VALUE
 * ============================================================ */

float ui_controls_get_value(
    const UIControls *ui,
    int index)
{
    if (!ui)
        return 0.0f;

    if (index < 0 || index >= ui->slider_count)
        return 0.0f;

    return ui->sliders[index].value;
}


void ui_controls_set_value(
    UIControls *ui,
    int index,
    float value)
{
    if (!ui)
        return;

    if (index < 0 || index >= ui->slider_count)
        return;

    UISlider *slider =
        &ui->sliders[index];

    if (value < slider->min_value)
        value = slider->min_value;

    if (value > slider->max_value)
        value = slider->max_value;

    slider->value = value;
}


/* ============================================================
 * EDIT MODE
 * ============================================================ */

static void start_editing(
    UIControls *ui,
    int index)
{
    if (!ui)
        return;

    if (index < 0 || index >= ui->slider_count)
        return;

    /* Сбрасываем предыдущее редактирование. */
    for (int i = 0;
         i < ui->slider_count;
         i++)
    {
        ui->sliders[i].editing = 0;
    }

    UISlider *slider =
        &ui->sliders[index];

    slider->editing = 1;

    format_value(
        slider,
        slider->edit_buffer,
        sizeof(slider->edit_buffer));

    slider->edit_cursor =
        (int)strlen(slider->edit_buffer);

    if (SDL_IsTextInputActive() == SDL_FALSE)
    {
        SDL_StartTextInput();
    }

    printf(
        "[ui] Editing slider %d (%s), current value: %s\n",
        index,
        slider->label,
        slider->edit_buffer);
}


static void stop_editing(
    UIControls *ui,
    int apply)
{
    if (!ui)
        return;

    for (int i = 0;
         i < ui->slider_count;
         i++)
    {
        UISlider *slider =
            &ui->sliders[i];

        if (!slider->editing)
            continue;

        if (apply)
        {
            float v =
                (float)atof(slider->edit_buffer);

            if (v < slider->min_value)
                v = slider->min_value;

            if (v > slider->max_value)
                v = slider->max_value;

            slider->value = v;

            if (ui->on_change)
            {
                ui->on_change(
                    i,
                    v,
                    ui->userdata);
            }

            printf(
                "[ui] Slider %d (%s) set to %.3f\n",
                i,
                slider->label,
                v);
        }

        slider->editing = 0;
        slider->edit_buffer[0] = '\0';
        slider->edit_cursor = 0;
    }

    if (SDL_IsTextInputActive())
    {
        SDL_StopTextInput();
    }
}


/* ============================================================
 * EVENT HANDLING
 * ============================================================ */

int ui_controls_handle_event(
    UIControls *ui,
    const SDL_Event *event)
{
    if (!ui || !event)
        return 0;

    if (!ui->visible)
        return 0;

    /*
     * TAB — показать/скрыть панель.
     */
    if (event->type == SDL_KEYDOWN &&
        event->key.keysym.sym == SDLK_TAB)
    {
        ui_controls_toggle(ui);
        return 1;
    }

    /*
     * Ввод текста в редактируемое поле.
     */
    for (int i = 0;
         i < ui->slider_count;
         i++)
    {
        UISlider *slider =
            &ui->sliders[i];

        if (!slider->editing)
            continue;

        /*
         * Enter — применить.
         */
        if (event->type == SDL_KEYDOWN &&
            event->key.keysym.sym == SDLK_RETURN)
        {
            stop_editing(ui, 1);
            return 1;
        }

        /*
         * Escape — отмена.
         */
        if (event->type == SDL_KEYDOWN &&
            event->key.keysym.sym == SDLK_ESCAPE)
        {
            stop_editing(ui, 0);
            return 1;
        }

        /*
         * Backspace.
         */
        if (event->type == SDL_KEYDOWN &&
            event->key.keysym.sym == SDLK_BACKSPACE)
        {
            int len =
                (int)strlen(slider->edit_buffer);

            if (len > 0)
            {
                slider->edit_buffer[len - 1] = '\0';
                slider->edit_cursor = len - 1;
            }

            return 1;
        }

        /*
         * Ввод символов.
         */
        if (event->type == SDL_TEXTINPUT)
        {
            const char *text =
                event->text.text;

            /*
             * Разрешаем цифры, точку и минус.
             */
            for (const char *p = text; *p; p++)
            {
                char c = *p;

                if ((c >= '0' && c <= '9') ||
                    c == '.' ||
                    c == '-')
                {
                    size_t len =
                        strlen(slider->edit_buffer);

                    if (len + 1 <
                        sizeof(slider->edit_buffer))
                    {
                        slider->edit_buffer[len] = c;
                        slider->edit_buffer[len + 1] = '\0';
                        slider->edit_cursor = (int)(len + 1);
                    }
                }
            }

            return 1;
        }

        /*
         * Блокируем остальные события,
         * пока идёт редактирование.
         */
        if (event->type == SDL_KEYDOWN)
        {
            return 1;
        }
    }

    /*
     * Клик мышью.
     */
    if (event->type == SDL_MOUSEBUTTONDOWN &&
        event->button.button == SDL_BUTTON_LEFT)
    {
        int mx = event->button.x;
        int my = event->button.y;

        /*
         * Клик вне панели.
         */
        if (mx < ui->panel_x ||
            mx >= ui->panel_x + ui->panel_width)
        {
            /* Клик по видео — сбросим редактирование. */
            stop_editing(ui, 1);
            return 0;
        }

        for (int i = 0;
             i < ui->slider_count;
             i++)
        {
            UISlider *slider =
                &ui->sliders[i];

            int sy = slider_y(ui, i);

            /*
             * Клик по числовому полю —
             * включаем редактирование.
             */
            SDL_Rect vbox =
                value_box_rect(ui, i);

            if (mx >= vbox.x &&
                mx < vbox.x + vbox.w &&
                my >= vbox.y &&
                my < vbox.y + vbox.h)
            {
                start_editing(ui, i);
                return 1;
            }

            /*
             * Клик по треку.
             */
            int track_top =
                sy + LABEL_HEIGHT + 4;
            int track_bottom =
                track_top + TRACK_HEIGHT + 14;

            if (my >= track_top - 4 &&
                my < track_bottom + 4)
            {
                /* Выходим из режима редактирования. */
                stop_editing(ui, 1);

                slider->dragging = 1;
                ui->active_slider = i;

                float v =
                    x_to_value(ui, slider, mx);

                slider->value = v;

                if (ui->on_change)
                {
                    ui->on_change(
                        i,
                        v,
                        ui->userdata);
                }

                return 1;
            }
        }
    }

    /*
     * Отпускание кнопки.
     */
    if (event->type == SDL_MOUSEBUTTONUP &&
        event->button.button == SDL_BUTTON_LEFT)
    {
        if (ui->active_slider >= 0)
        {
            ui->sliders[ui->active_slider].dragging = 0;
            ui->active_slider = -1;
        }

        return 1;
    }

    /*
     * Перетаскивание.
     */
    if (event->type == SDL_MOUSEMOTION &&
        (event->motion.state & SDL_BUTTON_LMASK))
    {
        if (ui->active_slider >= 0)
        {
            UISlider *slider =
                &ui->sliders[ui->active_slider];

            float v =
                x_to_value(
                    ui,
                    slider,
                    event->motion.x);

            slider->value = v;

            if (ui->on_change)
            {
                ui->on_change(
                    ui->active_slider,
                    v,
                    ui->userdata);
            }

            return 1;
        }
    }

    /*
     * Колесо мыши над панелью.
     */
    if (event->type == SDL_MOUSEWHEEL)
    {
        int mx, my;
        SDL_GetMouseState(&mx, &my);

        if (mx >= ui->panel_x &&
            mx < ui->panel_x + ui->panel_width)
        {
            for (int i = 0;
                 i < ui->slider_count;
                 i++)
            {
                UISlider *slider =
                    &ui->sliders[i];

                int sy = slider_y(ui, i);

                if (my >= sy - 10 &&
                    my < sy + SLIDER_SPACING - 10)
                {
                    float step =
                        (slider->max_value -
                         slider->min_value) / 100.0f;

                    float v =
                        slider->value +
                        (event->wheel.y > 0 ?
                         step : -step);

                    if (v < slider->min_value)
                        v = slider->min_value;

                    if (v > slider->max_value)
                        v = slider->max_value;

                    slider->value = v;

                    if (ui->on_change)
                    {
                        ui->on_change(
                            i,
                            v,
                            ui->userdata);
                    }

                    return 1;
                }
            }
        }
    }

    return 0;
}


/* ============================================================
 * TEXT RENDERING HELPERS
 * ============================================================ */

static void draw_text(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const char *text,
    int x,
    int y,
    SDL_Color color)
{
    if (!renderer || !font || !text)
        return;

    if (text[0] == '\0')
        return;

    SDL_Surface *surface =
        TTF_RenderUTF8_Blended(
            font,
            text,
            color);

    if (!surface)
        return;

    SDL_Texture *texture =
        SDL_CreateTextureFromSurface(
            renderer,
            surface);

    if (!texture)
    {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dst;

    dst.x = x;
    dst.y = y;
    dst.w = surface->w;
    dst.h = surface->h;

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        &dst);

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}


/* ============================================================
 * RENDER
 * ============================================================ */

static void render_slider(
    SDL_Renderer *renderer,
    UIControls *ui,
    int index)
{
    UISlider *slider =
        &ui->sliders[index];

    int sy = slider_y(ui, index);

    int track_x =
        ui->panel_x + SLIDER_SIDE_MARGIN;
    int track_w =
        ui->panel_width - 2 * SLIDER_SIDE_MARGIN;

    /*
     * Подпись.
     */
    SDL_Color label_color =
        { LABEL_COLOR_R,
          LABEL_COLOR_G,
          LABEL_COLOR_B,
          LABEL_COLOR_A };

    draw_text(
        renderer,
        ui->font,
        slider->label,
        track_x,
        sy,
        label_color);

    /*
     * Трек.
     */
    int track_y =
        sy + LABEL_HEIGHT + 6;

    SDL_Rect track_rect;

    track_rect.x = track_x;
    track_rect.y = track_y;
    track_rect.w = track_w;
    track_rect.h = TRACK_HEIGHT;

    SDL_SetRenderDrawColor(
        renderer,
        TRACK_BG_R,
        TRACK_BG_G,
        TRACK_BG_B,
        TRACK_BG_A);

    SDL_RenderFillRect(
        renderer,
        &track_rect);

    /*
     * Заполненная часть.
     */
    int knob_x = value_to_x(ui, slider);

    SDL_Rect filled;

    filled.x = track_x;
    filled.y = track_y;
    filled.w = knob_x - track_x;
    filled.h = TRACK_HEIGHT;

    SDL_SetRenderDrawColor(
        renderer,
        TRACK_FILL_R,
        TRACK_FILL_G,
        TRACK_FILL_B,
        TRACK_FILL_A);

    SDL_RenderFillRect(
        renderer,
        &filled);

    /*
     * Ручка.
     */
    SDL_Rect knob;

    knob.x = knob_x - KNOB_WIDTH / 2;
    knob.y = track_y + TRACK_HEIGHT / 2 - KNOB_HEIGHT / 2;
    knob.w = KNOB_WIDTH;
    knob.h = KNOB_HEIGHT;

    SDL_SetRenderDrawColor(
        renderer,
        KNOB_R,
        KNOB_G,
        KNOB_B,
        KNOB_A);

    SDL_RenderFillRect(
        renderer,
        &knob);

    /*
     * Числовое поле.
     */
    SDL_Rect vbox =
        value_box_rect(ui, index);

    if (slider->editing)
    {
        SDL_SetRenderDrawColor(
            renderer,
            VALUE_BOX_EDIT_R,
            VALUE_BOX_EDIT_G,
            VALUE_BOX_EDIT_B,
            VALUE_BOX_EDIT_A);
    }
    else
    {
        SDL_SetRenderDrawColor(
            renderer,
            VALUE_BOX_R,
            VALUE_BOX_G,
            VALUE_BOX_B,
            VALUE_BOX_A);
    }

    SDL_RenderFillRect(
        renderer,
        &vbox);

    SDL_SetRenderDrawColor(
        renderer,
        120, 120, 140, 255);

    SDL_RenderDrawRect(
        renderer,
        &vbox);

    /*
     * Текст значения.
     */
    char display[64];

    if (slider->editing)
    {
        snprintf(
            display,
            sizeof(display),
            "%s_",
            slider->edit_buffer);
    }
    else
    {
        format_value(
            slider,
            display,
            sizeof(display));
    }

    SDL_Color value_color =
        { VALUE_COLOR_R,
          VALUE_COLOR_G,
          VALUE_COLOR_B,
          VALUE_COLOR_A };

    /*
     * Выравнивание по центру поля.
     */
    int text_w = 0;
    int text_h = 0;

    TTF_SizeUTF8(
        ui->font,
        display,
        &text_w,
        &text_h);

    int tx =
        vbox.x + (vbox.w - text_w) / 2;
    int ty =
        vbox.y + (vbox.h - text_h) / 2;

    draw_text(
        renderer,
        ui->font,
        display,
        tx,
        ty,
        value_color);
}


void ui_controls_render(
    UIControls *ui,
    SDL_Renderer *renderer)
{
    if (!ui || !renderer)
        return;

    if (!ui->visible)
        return;

    /*
     * Фон панели.
     */
    SDL_Rect panel;

    panel.x = ui->panel_x;
    panel.y = ui->panel_y;
    panel.w = ui->panel_width;
    panel.h = ui->panel_height;

    SDL_SetRenderDrawBlendMode(
        renderer,
        SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(
        renderer,
        BG_COLOR_R,
        BG_COLOR_G,
        BG_COLOR_B,
        BG_COLOR_A);

    SDL_RenderFillRect(
        renderer,
        &panel);

    /*
     * Разделитель.
     */
    SDL_SetRenderDrawColor(
        renderer,
        100, 100, 120, 255);

    SDL_RenderDrawLine(
        renderer,
        ui->panel_x,
        0,
        ui->panel_x,
        ui->panel_height);

    /*
     * Заголовок.
     */
    SDL_Color title_color =
        { 255, 255, 255, 255 };

    draw_text(
        renderer,
        ui->font,
        "SHADOW DETECTOR",
        ui->panel_x + SLIDER_SIDE_MARGIN,
        15,
        title_color);

    /*
     * Ползунки.
     */
    for (int i = 0;
         i < ui->slider_count;
         i++)
    {
        render_slider(
            renderer,
            ui,
            i);
    }

    /*
     * Подсказка внизу.
     */
    SDL_Color hint_color =
        { 160, 160, 170, 255 };

    int hint_y =
        ui->panel_y +
        SLIDER_TOP_MARGIN +
        ui->slider_count * SLIDER_SPACING +
        20;

    draw_text(
        renderer,
        ui->font,
        "Click value to edit",
        ui->panel_x + SLIDER_SIDE_MARGIN,
        hint_y,
        hint_color);

    draw_text(
        renderer,
        ui->font,
        "Enter - apply, Esc - cancel",
        ui->panel_x + SLIDER_SIDE_MARGIN,
        hint_y + 22,
        hint_color);

    draw_text(
        renderer,
        ui->font,
        "TAB - hide/show panel",
        ui->panel_x + SLIDER_SIDE_MARGIN,
        hint_y + 44,
        hint_color);
}


/* ============================================================
 * TOGGLE
 * ============================================================ */

void ui_controls_toggle(
    UIControls *ui)
{
    if (!ui)
        return;

    ui->visible = !ui->visible;
}


/* ============================================================
 * FREE
 * ============================================================ */

void ui_controls_free(
    UIControls *ui)
{
    if (!ui)
        return;

    for (int i = 0;
         i < ui->slider_count;
         i++)
    {
        free_cached_texture(
            &ui->sliders[i]);
    }

    if (ui->font)
    {
        TTF_CloseFont(ui->font);
        ui->font = NULL;
    }

    if (TTF_WasInit())
    {
        TTF_Quit();
    }

    memset(
        ui,
        0,
        sizeof(*ui));
}