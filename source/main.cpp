#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <switch.h>
#include <sys/stat.h>
#include <unwind.h>

#include <ctime>
#include <fstream>
#include <iostream>

#ifdef DEBUG
#include <twili.h>
#endif

extern "C" {
#include "MenuChooser.h"
#include "SDL_helper.h"
#include "common.h"
#include "config.h"
#include "fs.h"
#include "menu_book_reader.h"
#include "textures.h"
}
#include <mupdf/fitz.h>

// ===== ЛОГИРОВАНИЕ =====
#define LOG_FILE "/switch/WookReader/log.txt"

std::ofstream logFile;

void Log_Init() {
  logFile.open(LOG_FILE, std::ios::out | std::ios::trunc);
  if (logFile.is_open()) {
    logFile << "=== WookReader Log Started ===" << std::endl;
    logFile.flush();
  }
}

void Log_Write(const std::string& msg) {
  if (logFile.is_open()) {
    logFile << "[LOG] " << msg << std::endl;
    logFile.flush();
  }
}

void Log_Error(const std::string& msg) {
  if (logFile.is_open()) {
    logFile << "[ERROR] " << msg << std::endl;
    logFile.flush();
  }
}

void Log_Close() {
  if (logFile.is_open()) {
    logFile << "=== WookReader Log Ended ===" << std::endl;
    logFile.close();
  }
}
// ===== КОНЕЦ ЛОГИРОВАНИЯ =====

// ===== STACK TRACE + TERMINATE HANDLER =====
extern "C" void _start(void);

struct BtState {
  void** cur;
  void** end;
};
static _Unwind_Reason_Code bt_cb(struct _Unwind_Context* ctx, void* arg) {
  BtState* s = static_cast<BtState*>(arg);
  if (s->cur == s->end) return _URC_END_OF_STACK;
  uintptr_t pc = _Unwind_GetIP(ctx);
  *s->cur++ = reinterpret_cast<void*>(pc);
  return _URC_NO_REASON;
}

static void log_stack_trace() {
  void* buf[32];
  BtState st = {buf, buf + 32};
  _Unwind_Backtrace(bt_cb, &st);
  size_t n = st.cur - buf;
  char line[64];
  snprintf(line, sizeof(line), "load_base=0x%lx",
           (unsigned long)(uintptr_t)_start);
  Log_Error(line);
  for (size_t i = 0; i < n; i++) {
    snprintf(line, sizeof(line), "  [%02zu] 0x%016lx", i,
             (unsigned long)(uintptr_t)buf[i]);
    Log_Error(line);
  }
}

static void terminate_handler() {
  Log_Error("TERMINATE: uncaught exception — see stack trace below");
  log_stack_trace();
  Log_Close();
  exit(1);
}
// ===== КОНЕЦ TERMINATE HANDLER =====

fz_context* ctx = NULL;
bool timeInitialized = false;

SDL_Renderer* RENDERER;
SDL_Window* WINDOW;
SDL_Event EVENT;
TTF_Font *ROBOTO_35, *ROBOTO_30, *ROBOTO_27, *ROBOTO_25, *ROBOTO_20, *ROBOTO_15;
bool configDarkMode;

void Term_Services() {
  Log_Write("Terminate Services");

  if (timeInitialized) {
    timeExit();
  }
  SDL_TextCache_Clear();  // must be before TTF_CloseFont
  TTF_CloseFont(ROBOTO_35);
  TTF_CloseFont(ROBOTO_30);
  TTF_CloseFont(ROBOTO_27);
  TTF_CloseFont(ROBOTO_25);
  TTF_CloseFont(ROBOTO_20);
  TTF_CloseFont(ROBOTO_15);
  TTF_Quit();

  if (ctx) {
    fz_drop_context(ctx);
    ctx = NULL;
  }

  Textures_Free();
  romfsExit();

  IMG_Quit();

  SDL_DestroyRenderer(RENDERER);
  SDL_DestroyWindow(WINDOW);
  SDL_Quit();

#ifdef DEBUG
  twiliExit();
#endif

  Log_Close();
}

bool Init_Services() {
#ifdef DEBUG
  twiliInitialize();
#endif

  // Однократная миграция: если /switch/eBookReader существует, а
  // /switch/WookReader ещё нет — переименовываем папку целиком (books, configs,
  // logs переезжают вместе).
  {
    struct stat st_old, st_new;
    bool old_exists =
        (stat("/switch/eBookReader", &st_old) == 0 && S_ISDIR(st_old.st_mode));
    bool new_exists =
        (stat("/switch/WookReader", &st_new) == 0 && S_ISDIR(st_new.st_mode));
    if (old_exists && !new_exists)
      rename("/switch/eBookReader", "/switch/WookReader");
  }

  // Сначала создаём папку и открываем лог
  FS_RecursiveMakeDir("/switch/WookReader");
  Log_Init();
  Log_Write("Initialize Services");

  romfsInit();
  Log_Write("Initialized RomFs");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
    Log_Error(std::string("SDL_Init failed: ") + SDL_GetError());
    Term_Services();
    return false;
  }
  Log_Write("Initialized SDL");

  timeInitialize();
  timeInitialized = true;
  Log_Write("Initialized Time");

  if (SDL_CreateWindowAndRenderer(1280, 720, 0, &WINDOW, &RENDERER) == -1) {
    Log_Error(std::string("SDL_CreateWindowAndRenderer failed: ") +
              SDL_GetError());
    Term_Services();
    return false;
  }
  Log_Write("Initialized Window and Renderer");

  SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

  if (!IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG)) {
    Log_Error(std::string("IMG_Init failed: ") + IMG_GetError());
    Term_Services();
    return false;
  }
  Log_Write("Initialized Image");

  // MuPDF context deferred to first PDF/EPUB/XPS open (BookReader.cpp)
  Log_Write("MuPDF init deferred to first use");

  if (TTF_Init() == -1) {
    Log_Error(std::string("TTF_Init failed: ") + TTF_GetError());
    Term_Services();
    return false;
  }
  Log_Write("Initialized TTF");

  Textures_Load();
  Log_Write("Loaded Textures");

  ROBOTO_35 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 35);
  ROBOTO_30 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 30);
  ROBOTO_27 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 27);
  ROBOTO_25 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 25);
  ROBOTO_20 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 20);
  ROBOTO_15 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 15);
  if (!ROBOTO_35 || !ROBOTO_30 || !ROBOTO_27 || !ROBOTO_25 || !ROBOTO_20 ||
      !ROBOTO_15) {
    Log_Error("Failed to load fonts");
    Term_Services();
    return false;
  }
  Log_Write("Retrieved Fonts");

  for (int i = 0; i < 2; i++) {
    if (SDL_JoystickOpen(i) == NULL) {
      Log_Error(std::string("SDL_JoystickOpen failed: ") + SDL_GetError());
      Term_Services();
      return false;
    }
  }
  Log_Write("Initialized Input");

  FS_RecursiveMakeDir("/switch/WookReader/books");
  Log_Write("Created book directory if needed");

  configDarkMode = true;

  Log_Write("All services initialized successfully!");
  return true;
}

int main(int argc, char* argv[]) {
  std::set_terminate(terminate_handler);

  if (!Init_Services()) return 1;

  Log_Write("argc = " + std::to_string(argc));
  for (int i = 0; i < argc; i++) {
    Log_Write("argv[" + std::to_string(i) + "] = " + std::string(argv[i]));
  }

  if (argc == 2) {
    Log_Write("Opening book: " + std::string(argv[1]));
    Menu_OpenBook(argv[1], 3, 0.3);
  } else {
    Log_Write("Starting file chooser");
    Menu_StartChoosing();
  }

  Term_Services();
  return 0;
}
