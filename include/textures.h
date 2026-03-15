#ifndef WOOK_READER_TEXTURES_H
#define WOOK_READER_TEXTURES_H

#include <SDL2/SDL.h>

extern SDL_Texture *battery_20, *battery_20_charging, *battery_30, *battery_30_charging, *battery_50, *battery_50_charging, \
			*battery_60, *battery_60_charging, *battery_80, *battery_80_charging, *battery_90, *battery_90_charging, \
			*battery_full, *battery_full_charging, *battery_low, *battery_unknown, *error, *warning, *button_a, *button_b, \
			*button_x, *button_y, *button_dpad_up_down, *button_dpad_left_right, *button_lb, *button_rb, *button_lt, \
			*button_rt, *button_minus, *button_plus, *left_stick_up_down, *right_stick_up_down;

/*Folder Images*/
extern SDL_Texture *adwaita_folder, *black_folder, *blue_grey_folder, *blue_folder, *breeze_folder,
                   *brown_folder, *carmine_folder, *cyan_folder, *dark_cyan_folder, *deep_orange_folder,
                   *green_folder, *grey_folder, *indigo_folder, *magenta_folder, *nordic_folder,
                   *orange_folder, *pale_brown_folder, *pale_orange_folder, *pink_folder, *red_folder,
                   *teal_folder, *violet_folder, *white_folder, *yaru_folder, *yellow_folder;

/*Book Images*/
extern SDL_Texture *adwaita_book, *black_book, *blue_grey_book, *blue_book, *breeze_book,
                   *brown_book, *carmine_book, *cyan_book, *dark_cyan_book, *deep_orange_book,
                   *green_book, *grey_book, *indigo_book, *magenta_book, *nordic_book,
                   *orange_book, *pale_brown_book, *pale_orange_book, *pink_book, *red_book,
                   *teal_book, *violet_book, *white_book, *yaru_book, *yellow_book;

void Textures_Load(void);
void Textures_Free(void);

#endif
