#include "status_bar.h"

#include <switch.h>
#include <time.h>

#include "SDL_helper.h"
#include "common.h"
#include "textures.h"

// Throttle counters — updated once per frame in StatusBar_DisplayTime()
static int g_clock_tick   = 0;
static int g_bat_tick     = 0;
#define CLOCK_INTERVAL 60    // re-poll every ~1 s at 60 fps
#define BAT_INTERVAL   300   // re-poll every ~5 s at 60 fps

static char g_clock_str[32] = "--:--";

static char* Clock_GetCurrentTime(void) {
  if (g_clock_tick % CLOCK_INTERVAL == 0) {
    time_t unixTime = time(NULL);
    struct tm* timeStruct = localtime((const time_t*)&unixTime);
    if (timeStruct) {
      int hours   = timeStruct->tm_hour;
      int minutes = timeStruct->tm_min;
      bool amOrPm = (hours < 12);
      if (hours == 0)       hours = 12;
      else if (hours > 12)  hours = hours - 12;
      snprintf(g_clock_str, 32, "%2i:%02i %s", hours, minutes, amOrPm ? "AM" : "PM");
    }
  }
  return g_clock_str;
}

static void StatusBar_GetBatteryStatus(int x, int y) {
  static u32 s_percent = 0;
  static PsmChargerType s_state = 0;
  static bool s_valid = false;

  // Only call PSM system functions every BAT_INTERVAL frames
  if (!s_valid || g_bat_tick % BAT_INTERVAL == 0) {
    if (R_FAILED(psmGetChargerType(&s_state))) s_state = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&s_percent))) s_valid = true;
  }

  u32 percent = s_percent;
  PsmChargerType state = s_state;
  int width = 0;
  char buf[5];

  SDL_Texture* batteryImage = battery_unknown;

  if (s_valid) {
    if (percent < 20) {
      // SDL_DrawImage(RENDERER, battery_low, x, 3);
      batteryImage = battery_low;
    } else if ((percent >= 20) && (percent < 30)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_20_charging, x, 3);
        batteryImage = battery_20_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_20, x, 3);
        batteryImage = battery_20;
      }
    } else if ((percent >= 30) && (percent < 50)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_50_charging, x, 3);
        batteryImage = battery_30_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_50, x, 3);
        batteryImage = battery_30;
      }
    } else if ((percent >= 50) && (percent < 60)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_50_charging, x, 3);
        batteryImage = battery_50_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_50, x, 3);
        batteryImage = battery_50;
      }
    } else if ((percent >= 60) && (percent < 80)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_60_charging, x, 3);
        batteryImage = battery_60_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_60, x, 3);
        batteryImage = battery_60;
      }
    } else if ((percent >= 80) && (percent < 90)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_80_charging, x, 3);
        batteryImage = battery_80_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_80, x, 3);
        batteryImage = battery_80;
      }
    } else if ((percent >= 90) && (percent < 100)) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_90_charging, x, 3);
        batteryImage = battery_90_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_90, x, 3);
        batteryImage = battery_90;
      }
    } else if (percent == 100) {
      if (state != 0) {
        // SDL_DrawImage(RENDERER, battery_full_charging, x, 3);
        batteryImage = battery_full_charging;
      } else {
        // SDL_DrawImage(RENDERER, battery_full, x, 3);
        batteryImage = battery_full;
      }
    }

    snprintf(buf, 5, "%d%%", percent);
    TTF_SizeText(ROBOTO_20, buf, &width, NULL);
    SDL_DrawHorizonalAlignedImageText(RENDERER, batteryImage, ROBOTO_20, WHITE,
                                      buf, (x + width - 30), y, 34, 34, -2, 0);
    // SDL_DrawText(RENDERER, ROBOTO_20, (x + width + 5), y, WHITE, buf);
  } else {
    snprintf(buf, 5, "%d%%", percent);
    TTF_SizeText(ROBOTO_20, buf, &width, NULL);
    SDL_DrawHorizonalAlignedImageText(RENDERER, battery_unknown, ROBOTO_20,
                                      WHITE, buf, x, y, 34, 34, -2, 0);
    /*SDL_DrawText(RENDERER, ROBOTO_20, (x + width + 5), y, WHITE, buf);
    SDL_DrawImage(RENDERER, battery_unknown, x, 1);*/
  }
}

void StatusBar_DisplayTime(bool portriat) {
  // Advance throttle counters (once per frame)
  g_clock_tick++;
  g_bat_tick++;

  int timeWidth = 0, timeHeight = 0;
  TTF_SizeText(ROBOTO_25, Clock_GetCurrentTime(), &timeWidth, &timeHeight);
  int helpWidth, helpHeight;
  TTF_SizeText(ROBOTO_20, "\"+\" - Help", &helpWidth, &helpHeight);

  if (portriat) {
    int timeX = (1280 - timeWidth) + timeHeight;
    int timeY = (720 - timeWidth) + 15;
    SDL_DrawRotatedText(RENDERER, ROBOTO_25, (double)90, timeX, timeY, WHITE,
                        Clock_GetCurrentTime());

    int helpX = (1280 - helpWidth) + 21;
    int helpY = (720 - helpHeight) - (720 - timeY) - 75;
    SDL_DrawRotatedText(RENDERER, ROBOTO_20, (double)90, helpX, helpY, WHITE,
                        "\"+\" - Help");
    // SDL_DrawRotatedText(RENDERER, ROBOTO_25, (double) 90, 1270 - width, (720
    // - height), WHITE, Clock_GetCurrentTime());
  } else {
    SDL_DrawText(RENDERER, ROBOTO_25, 1260 - timeWidth, (40 - timeHeight) / 2,
                 WHITE, Clock_GetCurrentTime());
    SDL_DrawText(RENDERER, ROBOTO_20, 1260 - helpWidth - timeWidth - 25,
                 (40 - helpHeight) / 2, WHITE, "\"+\" - Help");
    StatusBar_GetBatteryStatus(
        1260 - (timeWidth + helpWidth) - 110,
        (40 - timeHeight) / 2 + 34);  // 34 is height of battery img
  }
}
