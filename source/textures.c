#include "textures.h"
#include "common.h"
#include "SDL_helper.h"

SDL_Texture *battery_20, *battery_20_charging, *battery_30, *battery_30_charging, *battery_50, *battery_50_charging,
	*battery_60, *battery_60_charging, *battery_80, *battery_80_charging, *battery_90, *battery_90_charging,
	*battery_full, *battery_full_charging, *battery_low, *battery_unknown, *error, *warning, *magenta_book, *carmine_folder, *button_a, *button_b,
	*button_x, *button_y, *button_dpad_up_down, *button_dpad_left_right, *button_lb, *button_rb, *button_lt,
	*button_rt, *button_minus, *button_plus, *left_stick_up_down, *right_stick_up_down;
/*Folder Images*/
SDL_Texture *adwaita_folder, *black_folder, *blue_grey_folder, *blue_folder, *breeze_folder,
	*brown_folder, *carmine_folder, *cyan_folder, *dark_cyan_folder, *deep_orange_folder,
	*green_folder, *grey_folder, *indigo_folder, *magenta_folder, *nordic_folder,
	*orange_folder, *pale_brown_folder, *pale_orange_folder, *pink_folder, *red_folder,
	*teal_folder, *violet_folder, *white_folder, *yaru_folder, *yellow_folder;

/*Book Images*/
SDL_Texture *adwaita_book, *black_book, *blue_grey_book, *blue_book, *breeze_book,
	*brown_book, *carmine_book, *cyan_book, *dark_cyan_book, *deep_orange_book,
	*green_book, *grey_book, *indigo_book, *magenta_book, *nordic_book,
	*orange_book, *pale_brown_book, *pale_orange_book, *pink_book, *red_book,
	*teal_book, *violet_book, *white_book, *yaru_book, *yellow_book;

void Textures_Load(void)
{
	// Battery images
	SDL_LoadImage(&battery_20, "romfs:/resources/images/battery/battery_20.png");
	SDL_LoadImage(&battery_20_charging, "romfs:/resources/images/battery/battery_20_charging.png");
	SDL_LoadImage(&battery_30, "romfs:/resources/images/battery/battery_30.png");
	SDL_LoadImage(&battery_30_charging, "romfs:/resources/images/battery/battery_30_charging.png");
	SDL_LoadImage(&battery_50, "romfs:/resources/images/battery/battery_50.png");
	SDL_LoadImage(&battery_50_charging, "romfs:/resources/images/battery/battery_50_charging.png");
	SDL_LoadImage(&battery_60, "romfs:/resources/images/battery/battery_60.png");
	SDL_LoadImage(&battery_60_charging, "romfs:/resources/images/battery/battery_60_charging.png");
	SDL_LoadImage(&battery_80, "romfs:/resources/images/battery/battery_80.png");
	SDL_LoadImage(&battery_80_charging, "romfs:/resources/images/battery/battery_80_charging.png");
	SDL_LoadImage(&battery_90, "romfs:/resources/images/battery/battery_90.png");
	SDL_LoadImage(&battery_90_charging, "romfs:/resources/images/battery/battery_90_charging.png");
	SDL_LoadImage(&battery_full, "romfs:/resources/images/battery/battery_full.png");
	SDL_LoadImage(&battery_full_charging, "romfs:/resources/images/battery/battery_full_charging.png");
	SDL_LoadImage(&battery_low, "romfs:/resources/images/battery/battery_low.png");
	SDL_LoadImage(&battery_unknown, "romfs:/resources/images/battery/battery_unknown.png");

	// Info images
	SDL_LoadImage(&error, "romfs:/resources/images/info/error.png");
	SDL_LoadImage(&warning, "romfs:/resources/images/info/warning.png");

	// Folder images
	SDL_LoadImage(&adwaita_folder, "romfs:/resources/images/folders/Adwaitafolder.png");
	SDL_LoadImage(&black_folder, "romfs:/resources/images/folders/Blackfolder.png");
	SDL_LoadImage(&blue_grey_folder, "romfs:/resources/images/folders/BlueGreyfolder.png");
	SDL_LoadImage(&blue_folder, "romfs:/resources/images/folders/Bluefolder.png");
	SDL_LoadImage(&breeze_folder, "romfs:/resources/images/folders/Breezefolder.png");
	SDL_LoadImage(&brown_folder, "romfs:/resources/images/folders/Brownfolder.png");
	SDL_LoadImage(&carmine_folder, "romfs:/resources/images/folders/Carminefolder.png");
	SDL_LoadImage(&cyan_folder, "romfs:/resources/images/folders/Cyanfolder.png");
	SDL_LoadImage(&dark_cyan_folder, "romfs:/resources/images/folders/DarkCyanfolder.png");
	SDL_LoadImage(&deep_orange_folder, "romfs:/resources/images/folders/DeepOrangefolder.png");
	SDL_LoadImage(&green_folder, "romfs:/resources/images/folders/Greenfolder.png");
	SDL_LoadImage(&grey_folder, "romfs:/resources/images/folders/Greyfolder.png");
	SDL_LoadImage(&indigo_folder, "romfs:/resources/images/folders/Indigofolder.png");
	SDL_LoadImage(&magenta_folder, "romfs:/resources/images/folders/Magentafolder.png");
	SDL_LoadImage(&nordic_folder, "romfs:/resources/images/folders/Nordicfolder.png");
	SDL_LoadImage(&orange_folder, "romfs:/resources/images/folders/Orangefolder.png");
	SDL_LoadImage(&pale_brown_folder, "romfs:/resources/images/folders/PaleBrownfolder.png");
	SDL_LoadImage(&pale_orange_folder, "romfs:/resources/images/folders/PaleOrangefolder.png");
	SDL_LoadImage(&pink_folder, "romfs:/resources/images/folders/Pinkfolder.png");
	SDL_LoadImage(&red_folder, "romfs:/resources/images/folders/Redfolder.png");
	SDL_LoadImage(&teal_folder, "romfs:/resources/images/folders/Tealfolder.png");
	SDL_LoadImage(&violet_folder, "romfs:/resources/images/folders/Violetfolder.png");
	SDL_LoadImage(&white_folder, "romfs:/resources/images/folders/Whitefolder.png");
	SDL_LoadImage(&yaru_folder, "romfs:/resources/images/folders/Yarufolder.png");
	SDL_LoadImage(&yellow_folder, "romfs:/resources/images/folders/Yellowfolder.png");

	// Book images
	SDL_LoadImage(&adwaita_book, "romfs:/resources/images/books/Adwaitabook.png");
	SDL_LoadImage(&black_book, "romfs:/resources/images/books/Blackbook.png");
	SDL_LoadImage(&blue_grey_book, "romfs:/resources/images/books/BlueGreybook.png");
	SDL_LoadImage(&blue_book, "romfs:/resources/images/books/Bluebook.png");
	SDL_LoadImage(&breeze_book, "romfs:/resources/images/books/Breezebook.png");
	SDL_LoadImage(&brown_book, "romfs:/resources/images/books/Brownbook.png");
	SDL_LoadImage(&carmine_book, "romfs:/resources/images/books/Carminebook.png");
	SDL_LoadImage(&cyan_book, "romfs:/resources/images/books/Cyanbook.png");
	SDL_LoadImage(&dark_cyan_book, "romfs:/resources/images/books/DarkCyanbook.png");
	SDL_LoadImage(&deep_orange_book, "romfs:/resources/images/books/DeepOrangebook.png");
	SDL_LoadImage(&green_book, "romfs:/resources/images/books/Greenbook.png");
	SDL_LoadImage(&grey_book, "romfs:/resources/images/books/Greybook.png");
	SDL_LoadImage(&indigo_book, "romfs:/resources/images/books/Indigobook.png");
	SDL_LoadImage(&magenta_book, "romfs:/resources/images/books/Magentabook.png");
	SDL_LoadImage(&nordic_book, "romfs:/resources/images/books/Nordicbook.png");
	SDL_LoadImage(&orange_book, "romfs:/resources/images/books/Orangebook.png");
	SDL_LoadImage(&pale_brown_book, "romfs:/resources/images/books/PaleBrownbook.png");
	SDL_LoadImage(&pale_orange_book, "romfs:/resources/images/books/PaleOrangebook.png");
	SDL_LoadImage(&pink_book, "romfs:/resources/images/books/Pinkbook.png");
	SDL_LoadImage(&red_book, "romfs:/resources/images/books/Redbook.png");
	SDL_LoadImage(&teal_book, "romfs:/resources/images/books/Tealbook.png");
	SDL_LoadImage(&violet_book, "romfs:/resources/images/books/Violetbook.png");
	SDL_LoadImage(&white_book, "romfs:/resources/images/books/Whitebook.png");
	SDL_LoadImage(&yaru_book, "romfs:/resources/images/books/Yarubook.png");
	SDL_LoadImage(&yellow_book, "romfs:/resources/images/books/Yellowbook.png");

	// Button images
	SDL_LoadImage(&button_a, "romfs:/resources/images/control/Switch_A.png");
	SDL_LoadImage(&button_b, "romfs:/resources/images/control/Switch_B.png");
	SDL_LoadImage(&button_x, "romfs:/resources/images/control/Switch_X.png");
	SDL_LoadImage(&button_y, "romfs:/resources/images/control/Switch_Y.png");
	SDL_LoadImage(&button_dpad_up_down, "romfs:/resources/images/control/Switch_Dpad_Up_down.png");
	SDL_LoadImage(&button_dpad_left_right, "romfs:/resources/images/control/Switch_Dpad_Left_right.png");
	SDL_LoadImage(&button_lb, "romfs:/resources/images/control/Switch_LB.png");
	SDL_LoadImage(&button_rb, "romfs:/resources/images/control/Switch_RB.png");
	SDL_LoadImage(&button_lt, "romfs:/resources/images/control/Switch_LT.png");
	SDL_LoadImage(&button_rt, "romfs:/resources/images/control/Switch_RT.png");
	SDL_LoadImage(&button_minus, "romfs:/resources/images/control/Switch_Minus.png");
	SDL_LoadImage(&button_plus, "romfs:/resources/images/control/Switch_Plus.png");
	SDL_LoadImage(&left_stick_up_down, "romfs:/resources/images/control/Switch_Left_Stick_up_down.png");
	SDL_LoadImage(&right_stick_up_down, "romfs:/resources/images/control/Switch_Right_Stick_up_down.png");
}

void Textures_Free(void)
{
	// Button images
	SDL_DestroyTexture(button_a);
	SDL_DestroyTexture(button_b);
	SDL_DestroyTexture(button_x);
	SDL_DestroyTexture(button_y);
	SDL_DestroyTexture(button_dpad_up_down);
	SDL_DestroyTexture(button_dpad_left_right);
	SDL_DestroyTexture(button_lb);
	SDL_DestroyTexture(button_rb);
	SDL_DestroyTexture(button_lt);
	SDL_DestroyTexture(button_rt);
	SDL_DestroyTexture(button_minus);
	SDL_DestroyTexture(button_plus);
	SDL_DestroyTexture(left_stick_up_down);
	SDL_DestroyTexture(right_stick_up_down);

	// Info images
	SDL_DestroyTexture(warning);
	SDL_DestroyTexture(error);

	// Battery images
	SDL_DestroyTexture(battery_unknown);
	SDL_DestroyTexture(battery_low);
	SDL_DestroyTexture(battery_full_charging);
	SDL_DestroyTexture(battery_full);
	SDL_DestroyTexture(battery_90_charging);
	SDL_DestroyTexture(battery_90);
	SDL_DestroyTexture(battery_80_charging);
	SDL_DestroyTexture(battery_80);
	SDL_DestroyTexture(battery_60_charging);
	SDL_DestroyTexture(battery_60);
	SDL_DestroyTexture(battery_50_charging);
	SDL_DestroyTexture(battery_50);
	SDL_DestroyTexture(battery_30_charging);
	SDL_DestroyTexture(battery_30);
	SDL_DestroyTexture(battery_20_charging);
	SDL_DestroyTexture(battery_20);

	// Folder images
	SDL_DestroyTexture(adwaita_folder);
	SDL_DestroyTexture(black_folder);
	SDL_DestroyTexture(blue_grey_folder);
	SDL_DestroyTexture(blue_folder);
	SDL_DestroyTexture(breeze_folder);
	SDL_DestroyTexture(brown_folder);
	SDL_DestroyTexture(carmine_folder);
	SDL_DestroyTexture(cyan_folder);
	SDL_DestroyTexture(dark_cyan_folder);
	SDL_DestroyTexture(deep_orange_folder);
	SDL_DestroyTexture(green_folder);
	SDL_DestroyTexture(grey_folder);
	SDL_DestroyTexture(indigo_folder);
	SDL_DestroyTexture(magenta_folder);
	SDL_DestroyTexture(nordic_folder);
	SDL_DestroyTexture(orange_folder);
	SDL_DestroyTexture(pale_brown_folder);
	SDL_DestroyTexture(pale_orange_folder);
	SDL_DestroyTexture(pink_folder);
	SDL_DestroyTexture(red_folder);
	SDL_DestroyTexture(teal_folder);
	SDL_DestroyTexture(violet_folder);
	SDL_DestroyTexture(white_folder);
	SDL_DestroyTexture(yaru_folder);
	SDL_DestroyTexture(yellow_folder);

	// Book images
	SDL_DestroyTexture(adwaita_book);
	SDL_DestroyTexture(black_book);
	SDL_DestroyTexture(blue_grey_book);
	SDL_DestroyTexture(blue_book);
	SDL_DestroyTexture(breeze_book);
	SDL_DestroyTexture(brown_book);
	SDL_DestroyTexture(carmine_book);
	SDL_DestroyTexture(cyan_book);
	SDL_DestroyTexture(dark_cyan_book);
	SDL_DestroyTexture(deep_orange_book);
	SDL_DestroyTexture(green_book);
	SDL_DestroyTexture(grey_book);
	SDL_DestroyTexture(indigo_book);
	SDL_DestroyTexture(magenta_book);
	SDL_DestroyTexture(nordic_book);
	SDL_DestroyTexture(orange_book);
	SDL_DestroyTexture(pale_brown_book);
	SDL_DestroyTexture(pale_orange_book);
	SDL_DestroyTexture(pink_book);
	SDL_DestroyTexture(red_book);
	SDL_DestroyTexture(teal_book);
	SDL_DestroyTexture(violet_book);
	SDL_DestroyTexture(white_book);
	SDL_DestroyTexture(yaru_book);
	SDL_DestroyTexture(yellow_book);
}
