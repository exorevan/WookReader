#include "BookReader.hpp"

#include <libconfig.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "CBZPageLayout.hpp"
#include "LandscapePageLayout.hpp"
#include "PageLayout.hpp"
#include "VerticalPageLayout.hpp"

extern void Log_Write(const std::string& msg);
extern void Log_Error(const std::string& msg);

extern "C" {
#include "SDL_helper.h"
#include "common.h"
#include "config.h"
#include "fs.h"
#include "status_bar.h"
#include "textures.h"
}

int windowX, windowY;
config_t* config = NULL;
const char* configFile = "/switch/WookReader/saved_pages.cfg";

static const char* NOTES_DIR = "/switch/WookReader/.notes";

static std::string notes_path(const char* book_name) {
  return std::string(NOTES_DIR) + "/" + book_name + ".txt";
}

static std::string notes_load(const char* book_name) {
  std::string path = notes_path(book_name);
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return "";
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return ""; }
  std::string text((size_t)sz, '\0');
  size_t got = fread(&text[0], 1, (size_t)sz, f);
  fclose(f);
  text.resize(got);
  text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

static void notes_save(const char* book_name, const std::string& text) {
  FS_RecursiveMakeDir(NOTES_DIR);
  std::string path = notes_path(book_name);
  if (text.empty()) {
    remove(path.c_str());
    return;
  }
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
}

static int load_last_page(const char* book_name) {
  if (!config) {
    config = (config_t*)malloc(sizeof(config_t));
    config_init(config);
    config_read_file(config, configFile);
  }

  config_setting_t* setting =
      config_setting_get_member(config_root_setting(config), book_name);

  if (setting) {
    return config_setting_get_int(setting);
  }

  return 0;
}

static void save_last_page(const char* book_name, int current_page) {
  config_setting_t* setting =
      config_setting_get_member(config_root_setting(config), book_name);

  if (!setting) {
    setting = config_setting_add(config_root_setting(config), book_name,
                                 CONFIG_TYPE_INT);
  }

  if (setting) {
    config_setting_set_int(setting, current_page);
    config_write_file(config, configFile);
  }
}

// Returns true if the path is a comic archive (CBZ/CBR/CBT/CB7,
// case-insensitive)
static bool path_is_cbz(const char* path) {
  const char* dot = strrchr(path, '.');
  if (!dot) return false;
  char ext[8] = {};
  for (int i = 0; i < 7 && dot[i]; i++)
    ext[i] = (char)tolower((unsigned char)dot[i]);
  return strcmp(ext, ".cbz") == 0 || strcmp(ext, ".cbr") == 0 ||
         strcmp(ext, ".cbt") == 0 || strcmp(ext, ".cb7") == 0;
}

BookReader::BookReader(const char* path, int* result) {
  SDL_GetWindowSize(WINDOW, &windowX, &windowY);

  book_path = path;
  book_name =
      std::string(path).substr(std::string(path).find_last_of("/\\") + 1);

  std::string invalid_chars = " :/?#[]@!$&'()*+,;=.";
  for (char& c : invalid_chars)
    book_name.erase(std::remove(book_name.begin(), book_name.end(), c),
                    book_name.end());

  _notes = notes_load(book_name.c_str());

  _is_cbz = path_is_cbz(path);

  if (_is_cbz) {
    Log_Write(std::string("BookReader: opening as comic ZIP: ") + path);
    int current_page = load_last_page(book_name.c_str());
    switch_current_page_layout(_currentPageLayout, current_page);
    if (!layout) {
      Log_Error(std::string("BookReader: CBZ/CBR layout creation failed: ") + path);
      *result = -1;
      return;
    }
    // Layout was created — enumeration may still be running in background.
    // Validity is checked in draw() after enumeration completes.
    if (current_page > 0) show_status_bar();
    return;
  }

  // MuPDF path (PDF, EPUB, XPS, ...)
  Log_Write(std::string("BookReader: opening via MuPDF: ") + path);

  // Lazy MuPDF init — deferred from startup to first PDF open
  if (ctx == NULL) {
    ctx = fz_new_context(NULL, NULL, 0);
    if (ctx) {
      fz_register_document_handlers(ctx);
      Log_Write("BookReader: initialized MuPDF context on first use");
    }
  }
  if (ctx == NULL) {
    Log_Error("BookReader: MuPDF context initialization failed");
    *result = -4;
    return;
  }

  fz_try(ctx) {
    doc = fz_open_document(ctx, path);

    if (!doc) {
      Log_Error(std::string("BookReader: fz_open_document returned null: ") +
                path);
      *result = -1;
      return;
    }

    int current_page = load_last_page(book_name.c_str());

    switch_current_page_layout(_currentPageLayout, current_page);

    if (!layout) {
      Log_Error(std::string("BookReader: MuPDF layout creation failed: ") +
                path);
      *result = -3;
      return;
    }

    Log_Write("BookReader: MuPDF opened OK, starting page=" +
              std::to_string(current_page));
    if (current_page > 0) show_status_bar();
  }
  fz_catch(ctx) {
    Log_Error(std::string("BookReader: fz_catch on open: ") + path);
    *result = -2;
    return;
  }
}

BookReader::~BookReader() {
  if (doc) {
    fz_drop_document(ctx, doc);
    doc = nullptr;
  }

  if (layout) {
    delete layout;
    layout = nullptr;
  }

  if (config) {
    config_destroy(config);
    free(config);
    config = nullptr;
  }
}

void BookReader::previous_page(int n) {
  if (!layout) return;
  layout->previous_page(n);
  show_status_bar();
  save_last_page(book_name.c_str(), layout->current_page());
}

void BookReader::next_page(int n) {
  if (!layout) return;
  layout->next_page(n);
  show_status_bar();
  save_last_page(book_name.c_str(), layout->current_page());
}

void BookReader::goto_page(int page_1indexed) {
  if (!layout) return;
  layout->goto_page(page_1indexed - 1);  // convert 1-indexed → 0-indexed
  show_status_bar();
  save_last_page(book_name.c_str(), layout->current_page());
}

void BookReader::set_notes(const std::string &text) {
  _notes = text;
  notes_save(book_name.c_str(), _notes);
}

void BookReader::zoom_in(float zoom_amount) {
  if (!layout) return;
  layout->zoom_in(zoom_amount);
  show_status_bar();
}

void BookReader::zoom_out(float zoom_amount) {
  if (!layout) return;
  layout->zoom_out(zoom_amount);
  show_status_bar();
}

void BookReader::zoom_at_point(float delta, float px, float py) {
  if (!layout) return;
  if (_is_cbz)
    static_cast<CBZPageLayout*>(layout)->zoom_at_point(delta, px, py);
  else
    layout->zoom_in(delta);  // MuPDF: fallback без якоря
  show_status_bar();
}

void BookReader::move_page_up(int scroll_speed) {
  if (!layout) return;
  layout->move_up(scroll_speed);
}

void BookReader::move_page_down(int scroll_speed) {
  if (!layout) return;
  layout->move_down(scroll_speed);
}

void BookReader::move_page_left(int scroll_speed) {
  if (!layout) return;
  layout->move_left(scroll_speed);
}

void BookReader::move_page_right(int scroll_speed) {
  if (!layout) return;
  layout->move_right(scroll_speed);
}

void BookReader::reset_page() {
  if (!layout) return;
  layout->reset();
  show_status_bar();
}

void BookReader::zoom_max() {
  if (!layout) return;
  layout->zoom_max();
}

void BookReader::switch_page_layout() {
  // CBZ: Y button toggles single-page / two-page spread
  if (_is_cbz) {
    if (layout) static_cast<CBZPageLayout*>(layout)->toggle_spread();
    return;
  }

  switch (_currentPageLayout) {
    case BookPageLayoutPortrait:
      switch_current_page_layout(BookPageLayoutLandscape, 0);
      break;
    case BookPageLayoutLandscape:
      switch_current_page_layout(BookPageLayoutVertical, 0);
      break;
    case BookPageLayoutVertical:
      switch_current_page_layout(BookPageLayoutPortrait, 0);
      break;
  }
}

void BookReader::draw(bool drawHelp, bool drawNotes) {
  if (configDarkMode == true) {
    SDL_ClearScreen(RENDERER, BLACK);
  } else {
    SDL_ClearScreen(RENDERER, WHITE);
  }

  SDL_RenderClear(RENDERER);

  // Check if layout is valid
  if (!layout) {
    if (ROBOTO_30) {
      SDL_DrawText(RENDERER, ROBOTO_30, 100, 350,
                   configDarkMode ? WHITE : BLACK,
                   "Error: Failed to load page.");
    }
    SDL_RenderPresent(RENDERER);
    return;
  }

  // CBZ: handle background archive enumeration state
  if (_is_cbz) {
    CBZPageLayout* cbz = static_cast<CBZPageLayout*>(layout);
    if (cbz->is_enumerating()) {
      // Show first page as soon as the enum thread has it ready.
      if (!cbz->is_valid() && cbz->is_first_image_ready())
        cbz->apply_first_image();
      if (cbz->is_valid()) {
        // First page is displayed — draw it with a "..." indicator.
        layout->draw_page();
        if (ROBOTO_25)
          SDL_DrawText(RENDERER, ROBOTO_25, 1220, 700,
                       configDarkMode ? WHITE : BLACK, "...");
      } else {
        if (ROBOTO_30) {
          const char* msg = "Opening archive...";
          int tw = 0, th = 0;
          TTF_SizeText(ROBOTO_30, msg, &tw, &th);
          // Nudge +4px right: TTF_SizeText underestimates vs actual render
          SDL_DrawText(RENDERER, ROBOTO_30, (1280 - tw) / 2 + 4, (720 - th) / 2,
                       configDarkMode ? WHITE : BLACK, msg);
        }
      }
      SDL_RenderPresent(RENDERER);
      return;
    }
    if (cbz->is_enumerating_internal()) {
      // Enum thread done but finish_enumeration() not yet called
      cbz->finish_enumeration();
      if (!cbz->is_valid()) {
        // Archive had no images or couldn't be opened
        if (ROBOTO_30)
          SDL_DrawText(RENDERER, ROBOTO_30, 100, 350,
                       configDarkMode ? WHITE : BLACK,
                       "Error: No images found in archive.");
        SDL_RenderPresent(RENDERER);
        return;
      }
    }
  }

  // Poll background work: MuPDF rasterization or CBZ prefetch-to-texture upload
  if (_is_cbz)
    static_cast<CBZPageLayout*>(layout)->poll_prefetch();
  else
    layout->poll_bg_render();

  layout->draw_page();

  if (drawHelp) {  // Help menu
    int helpWidth = 680;
    int helpHeight = 395;

    if (!configDarkMode) {  // Display a dimmed background if on light mode
      SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
    }

    SDL_DrawRect(RENDERER, (windowX - helpWidth) / 2,
                 (windowY - helpHeight) / 2, helpWidth, helpHeight,
                 configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

    int textX = (windowX - helpWidth) / 2 + 20;
    int textY = (windowY - helpHeight) / 2 + 87;
    SDL_Color textColor = configDarkMode ? WHITE : BLACK;
    SDL_DrawText(RENDERER, ROBOTO_30, textX, (windowY - helpHeight) / 2 + 10,
                 textColor, "Help Menu:");

    SDL_DrawButtonPrompt(RENDERER, button_b, ROBOTO_25, textColor,
                         "Stop reading / Close help menu.", textX, textY, 35,
                         35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_minus, ROBOTO_25, textColor,
                         "Switch to dark/light theme.", textX, textY + 38, 35,
                         35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, right_stick_up_down, ROBOTO_25, textColor,
                         "Zoom in/out.", textX, textY + 38 * 2, 35, 35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, left_stick_up_down, ROBOTO_25, textColor,
                         "Page up/down.", textX, textY + 38 * 3, 35, 35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_y, ROBOTO_25, textColor,
                         "Open notes.", textX, textY + 38 * 4, 35, 35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_x, ROBOTO_25, textColor,
                         "Keep status bar on.", textX, textY + 38 * 5, 35, 35,
                         5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_lt, ROBOTO_25, textColor,
                         "Previous page.", textX, textY + 38 * 6, 35, 35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_rt, ROBOTO_25, textColor,
                         "Next page.", textX, textY + 38 * 7, 35, 35, 5, 0);
    SDL_DrawButtonPrompt(RENDERER, button_a, ROBOTO_25, textColor,
                         "Jump to page.", textX, textY + 38 * 8, 35, 35, 5, 0);
  }

  if (drawNotes) {
    int noteWidth = 800;
    int noteHeight = 500;

    if (!configDarkMode) {
      SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
    }

    SDL_DrawRect(RENDERER, (windowX - noteWidth) / 2,
                 (windowY - noteHeight) / 2, noteWidth, noteHeight,
                 configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

    int nTextX = (windowX - noteWidth) / 2 + 20;
    int nTextY = (windowY - noteHeight) / 2 + 10;
    SDL_Color noteColor = configDarkMode ? WHITE : BLACK;

    SDL_DrawText(RENDERER, ROBOTO_30, nTextX, nTextY, noteColor, "Notes:");

    // Hint line at the bottom
    int hintY = (windowY + noteHeight) / 2 - 35;
    if (ROBOTO_15) {
      SDL_DrawText(RENDERER, ROBOTO_15, nTextX, hintY, noteColor,
                   "A = Edit    B = Close");
    }

    // Note body
    int bodyY = nTextY + 45;
    int bodyMaxH = hintY - bodyY - 10;
    if (_notes.empty()) {
      if (ROBOTO_25)
        SDL_DrawText(RENDERER, ROBOTO_25, nTextX, bodyY,
                     DARK_GRAY, "No notes yet. Press A to add one.");
    } else if (ROBOTO_20) {
      SDL_DrawTextWrapped(RENDERER, ROBOTO_20, nTextX, bodyY,
                          noteWidth - 40, bodyMaxH, noteColor, _notes.c_str());
    }
  }

  if (permStatusBar || --status_bar_visible_counter > 0) {
    char* title = layout->info();

    // Check if title and fonts are valid
    if (title && ROBOTO_15 && ROBOTO_25) {
      int title_width = 0, title_height = 0;
      TTF_SizeText(ROBOTO_15, title, &title_width, &title_height);

      SDL_Color color = configDarkMode ? STATUS_BAR_DARK : STATUS_BAR_LIGHT;

      if (_currentPageLayout == BookPageLayoutPortrait ||
          _currentPageLayout == BookPageLayoutVertical) {
        SDL_DrawRect(RENDERER, 0, 0, 1280, 45,
                     SDL_MakeColour(color.r, color.g, color.b, 180));
        SDL_DrawText(RENDERER, ROBOTO_25, (1280 - title_width) / 2,
                     (40 - title_height) / 2, WHITE, title);

        StatusBar_DisplayTime(false);
      } else if (_currentPageLayout == BookPageLayoutLandscape) {
        SDL_DrawRect(RENDERER, 1280 - 45, 0, 45, 720,
                     SDL_MakeColour(color.r, color.g, color.b, 180));
        int x = (1280 - title_width) - ((40 - title_height) / 2);
        int y = (720 - title_height) / 2;
        SDL_DrawRotatedText(RENDERER, ROBOTO_25, (double)90, x, y, WHITE,
                            title);

        StatusBar_DisplayTime(true);
      }
    }
  }

  SDL_RenderPresent(RENDERER);
}

void BookReader::show_status_bar() { status_bar_visible_counter = 200; }

void BookReader::switch_current_page_layout(BookPageLayout bookPageLayout,
                                            int current_page) {
  if (layout) {
    current_page = layout->current_page();
    delete layout;
    layout = nullptr;
  }

  _currentPageLayout = bookPageLayout;

  // CBZ: use CBZPageLayout (no MuPDF doc involved, landscape not yet supported)
  if (_is_cbz) {
    CBZPageLayout* cbz = new CBZPageLayout(book_path.c_str(), current_page);
    // Always assign layout — enumeration may still be running in background.
    // BookReader::draw() will call finish_enumeration() when is_enumerating() = false.
    layout = cbz;
    return;
  }

  // MuPDF path
  fz_try(ctx) {
    switch (bookPageLayout) {
      case BookPageLayoutPortrait:
        layout = new PageLayout(doc, current_page);
        break;
      case BookPageLayoutLandscape:
        layout = new LandscapePageLayout(doc, current_page);
        break;
      case BookPageLayoutVertical:
        layout = new VerticalPageLayout(doc, current_page);
        break;
    }
  }
  fz_catch(ctx) {
    Log_Error("BookReader: fz_catch creating MuPDF layout for page " +
              std::to_string(current_page));
    layout = nullptr;
  }

  // Enable background rasterization now that the layout is constructed.
  if (layout && !book_path.empty())
    layout->set_bg_path(book_path.c_str());
}
