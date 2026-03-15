extern "C"
{
#include "menu_book_reader.h"
#include "MenuChooser.h"
#include "common.h"
#include "config.h"
#include "SDL_helper.h"
}

#include <iostream>
#include <string>
#include <cmath>
#include <cstdlib>
#include "BookReader.hpp"

extern void Log_Write(const std::string &msg);
extern void Log_Error(const std::string &msg);

void Menu_OpenBook(char *path, int scroll_speed, float zoom_amount)
{
    Log_Write(std::string("Menu_OpenBook: ") + path);

    BookReader *reader = NULL;
    int result = 0;

    reader = new BookReader(path, &result);

    if (result < 0)
    {
        Log_Error("Menu_OpenBook: failed to load, result=" + std::to_string(result) + " path=" + path);
        delete reader;
        return;
    }

    /*TouchInfo touchInfo;
    Touch_Init(&touchInfo);*/
    hidInitializeTouchScreen();

    // Touch gesture state
    float touch_prev_x0    = 0.0f, touch_prev_y0 = 0.0f;
    float touch_prev_x1    = 0.0f, touch_prev_y1 = 0.0f;
    int   touch_prev_count = 0;
    float touch_start_x    = 0.0f, touch_start_y = 0.0f;
    bool  touch_dragging   = false;

    bool helpMenu = false;

    // Configure our supported input layout: a single player with standard controller syles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);
    // Touch_Process(&touchInfo);

    while (result >= 0 && appletMainLoop())
    {
        SDL_TextCache_NextFrame();
        SDL_PumpEvents();  // drain SDL's internal event queue to prevent HID interference
        reader->draw(helpMenu);

        padUpdate(&pad);

        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        u64 kUp = padGetButtonsUp(&pad);

        HidTouchScreenState state = {0};
        hidGetTouchScreenStates(&state, 1);

        if (!helpMenu)
        {
            if (state.count == 1)
            {
                float cx = (float)state.touches[0].x;
                float cy = (float)state.touches[0].y;

                if (touch_prev_count == 0)
                {
                    // Finger just touched down — record start position
                    touch_start_x  = cx;
                    touch_start_y  = cy;
                    touch_dragging = false;
                }
                else if (touch_prev_count == 1)
                {
                    float dx    = cx - touch_prev_x0;
                    float dy    = cy - touch_prev_y0;
                    float moved = sqrtf((cx - touch_start_x) * (cx - touch_start_x) +
                                       (cy - touch_start_y) * (cy - touch_start_y));
                    if (moved > 18.0f)
                        touch_dragging = true;

                    if (touch_dragging)
                    {
                        // Image follows finger like a phone gallery
                        // roundf() prevents sub-pixel truncation that caused "sticking" near edges
                        if (dx > 0.5f)       reader->move_page_right((int)roundf(dx));
                        else if (dx < -0.5f) reader->move_page_left((int)roundf(-dx));
                        if (dy > 0.5f)       reader->move_page_up((int)roundf(dy));
                        else if (dy < -0.5f) reader->move_page_down((int)roundf(-dy));
                    }
                }

                touch_prev_x0 = cx;
                touch_prev_y0 = cy;
            }
            else if (state.count == 2)
            {
                float cx0 = (float)state.touches[0].x, cy0 = (float)state.touches[0].y;
                float cx1 = (float)state.touches[1].x, cy1 = (float)state.touches[1].y;

                if (touch_prev_count == 2)
                {
                    // Pinch-to-zoom: compare finger distance between frames
                    float prev_d = sqrtf((touch_prev_x1 - touch_prev_x0) * (touch_prev_x1 - touch_prev_x0) +
                                        (touch_prev_y1 - touch_prev_y0) * (touch_prev_y1 - touch_prev_y0));
                    float curr_d = sqrtf((cx1 - cx0) * (cx1 - cx0) + (cy1 - cy0) * (cy1 - cy0));
                    float delta  = (curr_d - prev_d) * 0.002f;
                    if (fabsf(delta) > 0.001f)
                    {
                        // Zoom anchored at midpoint between the two fingers
                        float mid_x = (cx0 + cx1) / 2.0f;
                        float mid_y = (cy0 + cy1) / 2.0f;
                        reader->zoom_at_point(delta, mid_x, mid_y);
                    }
                }

                touch_dragging = true; // prevents accidental tap on finger lift
                touch_prev_x0 = cx0;  touch_prev_y0 = cy0;
                touch_prev_x1 = cx1;  touch_prev_y1 = cy1;
            }
            else if (state.count == 0 && touch_prev_count == 1 && !touch_dragging)
            {
                // Finger lifted without meaningful movement → tap: zone-based navigation
                float             tx     = touch_start_x, ty = touch_start_y;
                BookPageLayout    layout = reader->currentPageLayout();

                if (tx > 1000 && ty > 200 && ty < 500)
                {
                    if (layout == BookPageLayoutPortrait || layout == BookPageLayoutVertical)
                        reader->next_page(1);
                    else if (layout == BookPageLayoutLandscape)
                        reader->zoom_in(zoom_amount);
                }
                else if (tx < 280 && ty > 200 && ty < 500)
                {
                    if (layout == BookPageLayoutPortrait || layout == BookPageLayoutVertical)
                        reader->previous_page(1);
                    else if (layout == BookPageLayoutLandscape)
                        reader->zoom_out(zoom_amount);
                }
            }
        }

        touch_prev_count = state.count;

        if (!helpMenu && kDown & HidNpadButton_ZL)
        {
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->previous_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->zoom_out(zoom_amount);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->previous_page(1);
            }
        }
        else if (!helpMenu && kDown & HidNpadButton_ZR)
        {
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->next_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->zoom_in(zoom_amount);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->next_page(1);
            }
        }

        /*next page is set to 9 as hitting ZR or ZL will naturally turn a single page
         * 9 + 1 = 10*/
        if (!helpMenu && kHeld & HidNpadButton_R && kDown & HidNpadButton_ZR)
        {
            reader->next_page(9);
        }
        else if (!helpMenu && kHeld & HidNpadButton_L && kDown & HidNpadButton_ZL)
        {
            reader->previous_page(9);
        }

        if (!helpMenu && ((kDown & HidNpadButton_Up)))
        {
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->zoom_max();
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->previous_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->previous_page(1);
            }
        }
        if (!helpMenu && (kHeld & HidNpadButton_StickRUp))
        {
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->zoom_in(zoom_amount * 0.07f);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->previous_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->previous_page(1);
            }
        }
        else if (!helpMenu && (kHeld & HidNpadButton_StickRDown))
        {
            // Right stick down: reduced zoom sensitivity in Portrait, next page elsewhere.
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->zoom_out(zoom_amount * 0.07f);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->next_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->next_page(1);
            }
        }

        // DPad Down: full zoom-out in Portrait, next page in Landscape/Vertical.
        // Kept separate from StickR so it always fires at full sensitivity.
        if (!helpMenu && (kDown & HidNpadButton_Down))
        {
            if (reader->currentPageLayout() == BookPageLayoutPortrait)
            {
                reader->zoom_out(zoom_amount);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutLandscape))
            {
                reader->next_page(1);
            }
            else if ((reader->currentPageLayout() == BookPageLayoutVertical))
            {
                reader->next_page(1);
            }
        }

        // DPad Left/Right: previous/next page in all layout modes.
        if (!helpMenu && (kDown & HidNpadButton_Left))
        {
            reader->previous_page(1);
        }
        if (!helpMenu && (kDown & HidNpadButton_Right))
        {
            reader->next_page(1);
        }

        // Left analog stick: full 360° scrolling (analog, proportional speed)
        if (!helpMenu)
        {
            HidAnalogStickState stick_l = padGetStickPos(&pad, 0);
            const int deadzone = 4000;

            if (stick_l.x > deadzone || stick_l.x < -deadzone ||
                stick_l.y > deadzone || stick_l.y < -deadzone)
            {
                // Normalize to -1.0..+1.0 (deadzone-adjusted)
                float nx = 0.0f, ny = 0.0f;
                if (stick_l.x > deadzone)
                    nx = (float)(stick_l.x - deadzone) / (32767 - deadzone);
                else if (stick_l.x < -deadzone)
                    nx = (float)(stick_l.x + deadzone) / (32767 - deadzone);
                if (stick_l.y > deadzone)
                    ny = (float)(stick_l.y - deadzone) / (32767 - deadzone);
                else if (stick_l.y < -deadzone)
                    ny = (float)(stick_l.y + deadzone) / (32767 - deadzone);

                // Scale by scroll_speed * 2 for snappier analog response;
                // both axes evaluated independently → full diagonal support
                float speed = scroll_speed * 2.0f;
                float sx = nx * speed;
                float sy = ny * speed;

                // "Camera" style: stick direction = viewport pan direction
                // move_page_left/right move the IMAGE, so swap for camera feel
                if (reader->currentPageLayout() == BookPageLayoutPortrait ||
                    reader->currentPageLayout() == BookPageLayoutVertical)
                {
                    if (sx > 0.5f)        reader->move_page_left((int)roundf(sx));
                    else if (sx < -0.5f)  reader->move_page_right((int)roundf(-sx));
                    if (sy > 0.5f)        reader->move_page_up((int)roundf(sy));
                    else if (sy < -0.5f)  reader->move_page_down((int)roundf(-sy));
                }
                else if (reader->currentPageLayout() == BookPageLayoutLandscape)
                {
                    // Rotated axes for landscape
                    if (sx > 0.5f)        reader->move_page_up((int)roundf(sx));
                    else if (sx < -0.5f)  reader->move_page_down((int)roundf(-sx));
                    if (sy > 0.5f)        reader->move_page_left((int)roundf(sy));
                    else if (sy < -0.5f)  reader->move_page_right((int)roundf(-sy));
                }
            }
        }

        if (!helpMenu && kDown & HidNpadButton_LeftSR)
            reader->next_page(10);
        else if (!helpMenu && kDown & HidNpadButton_LeftSL)
            reader->previous_page(10);

        if (kUp & HidNpadButton_B)
        {
            if (helpMenu)
            {
                helpMenu = !helpMenu;
            }
            else
            {
                break;
            }
        }

        if (!helpMenu && kDown & HidNpadButton_X)
        {
            reader->permStatusBar = !reader->permStatusBar;
        }

        if (!helpMenu && kDown & HidNpadButton_A)
        {
            SwkbdConfig swkbd;
            swkbdCreate(&swkbd, 0);
            swkbdConfigSetType(&swkbd, SwkbdType_NumPad);
            swkbdConfigSetHeaderText(&swkbd, "Go to page");
            swkbdConfigSetStringLenMax(&swkbd, 6);
            swkbdConfigSetStringLenMin(&swkbd, 1);
            swkbdConfigSetBlurBackground(&swkbd, 1);
            char buf[16] = {0};
            Result rc = swkbdShow(&swkbd, buf, sizeof(buf));
            swkbdClose(&swkbd);
            if (R_SUCCEEDED(rc) && buf[0] != '\0')
                reader->goto_page(atoi(buf));
        }

        if ((!helpMenu && kDown & HidNpadButton_StickL) || kDown & HidNpadButton_StickR)
        {
            reader->reset_page();
        }

        if (!helpMenu && kDown & HidNpadButton_Y)
        {
            reader->switch_page_layout();
        }

        if (!helpMenu && kUp & HidNpadButton_Minus)
        {
            configDarkMode = !configDarkMode;
            reader->previous_page(0);
        }

        if (kDown & HidNpadButton_Plus)
        {
            helpMenu = !helpMenu;
        }

        /*if (touchInfo.state == TouchEnded && touchInfo.tapType != TapNone) {
            float tapRegion = 120;

            switch (reader->currentPageLayout()) {
                case BookPageLayoutPortrait:
                    if (tapped_inside(touchInfo, 0, 0, tapRegion, 720))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 1280 - tapRegion, 0, 1280, 720))
                        reader->next_page(1);
                    break;
                case BookPageLayoutLandscape:
                    if (tapped_inside(touchInfo, 0, 0, 1280, tapRegion))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 0, 720 - tapRegion, 1280, 720))
                        reader->next_page(1);
                    reader->reset_page();
                    break;
            }
        }*/
    }

    std::cout << "Exiting reader" << std::endl;
    std::cout << "Opening chooser" << std::endl;
    /* Menu_StartChoosing(); */
    delete reader;
    // consoleExit(NULL);
}
