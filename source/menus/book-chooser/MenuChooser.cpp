extern "C" {
#include "MenuChooser.h"

#include "SDL_helper.h"
#include "common.h"
#include "config.h"
#include "fs.h"
#include "menu_book_reader.h"
#include "textures.h"
}

#include <SDL2/SDL_image.h>
#include <bits/stdc++.h>
#include <libconfig.h>
#include <switch.h>
#include <archive.h>
#include <archive_entry.h>
#include <mupdf/fitz.h>
#include "../book/stb_image.h"  // declarations only (implementation in CBZPageLayout.cpp)
#include <turbojpeg.h>

#include <filesystem>
#include <iostream>

using namespace std;
namespace fs = filesystem;

extern void Log_Write(const std::string& msg);
extern void Log_Error(const std::string& msg);
extern fz_context *ctx;

// ── Per-comic notes helpers ───────────────────────────────────────────────────
static const char* CHOOSER_NOTES_DIR = "/switch/WookReader/.notes";

static string chooser_sanitize(const string& fpath) {
  string name = fpath.substr(fpath.find_last_of("/\\") + 1);
  const string bad = " :/?#[]@!$&'()*+,;=.";
  for (char c : bad)
    name.erase(remove(name.begin(), name.end(), c), name.end());
  return name;
}

static string chooser_note_path(const string& fpath) {
  return string(CHOOSER_NOTES_DIR) + "/" + chooser_sanitize(fpath) + ".txt";
}

static bool chooser_note_exists(const string& fpath) {
  return FS_FileExists(chooser_note_path(fpath).c_str());
}

static string chooser_note_read(const string& fpath) {
  string p = chooser_note_path(fpath);
  FILE* f = fopen(p.c_str(), "r");
  if (!f) return "";
  fseek(f, 0, SEEK_END);
  long sz = ftell(f); fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return ""; }
  string text((size_t)sz, '\0');
  size_t got = fread(&text[0], 1, (size_t)sz, f);
  fclose(f);
  text.resize(got);
  text.erase(remove(text.begin(), text.end(), '\r'), text.end());
  return text;
}

static void chooser_note_save(const string& fpath, const string& text) {
  FS_RecursiveMakeDir(CHOOSER_NOTES_DIR);
  string p = chooser_note_path(fpath);
  if (text.empty()) { remove(p.c_str()); return; }
  FILE* f = fopen(p.c_str(), "w");
  if (!f) return;
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
}

/*This is for the colors array, don't have a great way of calculating it so i
 * use this definition*/
#define COLORS_SIZE 24

/*Default Scroll Speed*/
#define SCROLL 3

/*Default Zoom Amount*/
#define ZOOM 0.3

/*configFile For Options*/
config_t* optionConfig = NULL;
const char* optionFile = "/switch/WookReader/options.cfg";

// ── Grid layout ──────────────────────────────────────────────────────────────
// COLS/CELL_W/CELL_H/THUMB_W/THUMB_H are dynamic (ZL/ZR zoom) — local vars in Menu_StartChoosing
static const int FOLDER_H = 40;   // folder row height (fixed)
static const int BOTTOM_H = 70;   // bottom bar height (fixed)

// ── LRU texture cache ─────────────────────────────────────────────────────────
// Key = "abs_path|cols" — covers are loaded at the right size per zoom level
// and cached so zoom changes are instant if that level was seen before.
struct LRUTextureCache {
  static const int MAX_ENTRIES = 300;
  list<string> order;  // front = most-recently-used
  unordered_map<string, pair<SDL_Texture*, list<string>::iterator>> entries;

  SDL_Texture* get(const string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) return nullptr;
    order.erase(it->second.second);
    order.push_front(key);
    it->second.second = order.begin();
    return it->second.first;
  }

  // Stores tex and returns it (nullptr is a no-op).
  SDL_Texture* put(const string& key, SDL_Texture* tex) {
    if (!tex) return nullptr;
    // Evict LRU entries until under the limit
    while ((int)entries.size() >= MAX_ENTRIES) {
      SDL_DestroyTexture(entries[order.back()].first);
      entries.erase(order.back());
      order.pop_back();
    }
    order.push_front(key);
    entries[key] = {tex, order.begin()};
    return tex;
  }

  void clear() {
    for (auto& [k, v] : entries) SDL_DestroyTexture(v.first);
    entries.clear();
    order.clear();
  }
};
static LRUTextureCache g_cover_cache;

template <typename T>
bool contains(list<T>& listOfElements, const T& element) {
  auto it = find(listOfElements.begin(), listOfElements.end(), element);
  return it != listOfElements.end();
}

extern TTF_Font *ROBOTO_35, *ROBOTO_25, *ROBOTO_15;

static int load_config(unsigned int* chosenFolderColor,
                       unsigned int* chosenBookColor, int* scroll_speed,
                       int* zoom_amount, bool* configDarkMode) {
  if (!optionConfig) {
    optionConfig = (config_t*)malloc(sizeof(config_t));
    config_init(optionConfig);
    config_read_file(optionConfig, optionFile);
  }

  config_setting_t* folder = config_setting_get_member(
      config_root_setting(optionConfig), "FolderColor");
  config_setting_t* book =
      config_setting_get_member(config_root_setting(optionConfig), "BookColor");
  config_setting_t* scroll = config_setting_get_member(
      config_root_setting(optionConfig), "ScrollSpeed");
  config_setting_t* zoom = config_setting_get_member(
      config_root_setting(optionConfig), "ZoomAmount");
  config_setting_t* dark =
      config_setting_get_member(config_root_setting(optionConfig), "DarkMode");

  if (folder && book && scroll && zoom) {
    *chosenFolderColor =
        std::clamp(config_setting_get_int(folder), 0, COLORS_SIZE);
    *chosenBookColor = std::clamp(config_setting_get_int(book), 0, COLORS_SIZE);
    *scroll_speed = std::clamp(config_setting_get_int(scroll), 1, 4);
    *zoom_amount = std::clamp(config_setting_get_int(zoom), 1, 4);
    *configDarkMode = config_setting_get_bool(dark);
  }

  return 0;
}

static void save_config(unsigned int chosenFolderColor,
                        unsigned int chosenBookColor, int scroll_speed,
                        int zoom_amount, bool configDarkMode) {
  config_setting_t* folder = config_setting_get_member(
      config_root_setting(optionConfig), "FolderColor");
  config_setting_t* book =
      config_setting_get_member(config_root_setting(optionConfig), "BookColor");
  config_setting_t* scroll = config_setting_get_member(
      config_root_setting(optionConfig), "ScrollSpeed");
  config_setting_t* zoom = config_setting_get_member(
      config_root_setting(optionConfig), "ZoomAmount");
  config_setting_t* dark =
      config_setting_get_member(config_root_setting(optionConfig), "DarkMode");

  if (!folder || !book || !scroll || !zoom || !dark) {
    folder = config_setting_add(config_root_setting(optionConfig),
                                "FolderColor", CONFIG_TYPE_INT);
    book = config_setting_add(config_root_setting(optionConfig), "BookColor",
                              CONFIG_TYPE_INT);
    scroll = config_setting_add(config_root_setting(optionConfig),
                                "ScrollSpeed", CONFIG_TYPE_INT);
    zoom = config_setting_add(config_root_setting(optionConfig), "ZoomAmount",
                              CONFIG_TYPE_INT);
    dark = config_setting_add(config_root_setting(optionConfig), "DarkMode",
                              CONFIG_TYPE_BOOL);
  }

  if (folder && book && scroll && zoom && dark) {
    config_setting_set_int(folder, chosenFolderColor);
    config_setting_set_int(book, chosenBookColor);
    config_setting_set_int(scroll, scroll_speed);
    config_setting_set_int(zoom, zoom_amount);
    config_setting_set_bool(dark, configDarkMode);
    config_write_file(optionConfig, optionFile);
  }
}

// Natural sort: compare strings so that embedded numbers sort numerically.
// "file2" < "file11" < "file20", case-insensitive.
static bool natural_less(const string& a, const string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    bool a_dig = isdigit((unsigned char)a[i]);
    bool b_dig = isdigit((unsigned char)b[j]);
    if (a_dig && b_dig) {
      // Skip leading zeros
      size_t ai = i, bi = j;
      while (ai < a.size() && a[ai] == '0') ai++;
      while (bi < b.size() && b[bi] == '0') bi++;
      // Find end of digit runs
      size_t ae = ai, be = bi;
      while (ae < a.size() && isdigit((unsigned char)a[ae])) ae++;
      while (be < b.size() && isdigit((unsigned char)b[be])) be++;
      size_t alen = ae - ai, blen = be - bi;
      if (alen != blen) return alen < blen;  // more digits = bigger number
      int cmp = a.compare(ai, alen, b, bi, blen);
      if (cmp != 0) return cmp < 0;
      i = ae; j = be;  // digit runs were equal — advance past them
    } else {
      char ca = tolower((unsigned char)a[i]);
      char cb = tolower((unsigned char)b[j]);
      if (ca != cb) return ca < cb;
      i++; j++;
    }
  }
  return a.size() < b.size();
}

static vector<fs::path> get_sorted_entries(const string& path,
                                             list<string> allowedExtentions,
                                             int *out_num_folders) {
  // Collect entries with is_directory and lowercase name cached during iteration
  // to avoid O(N log N) stat() calls in the sort comparator.
  struct EntryInfo { fs::path p; bool is_dir; string name_lower; };
  vector<EntryInfo> infos;

  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(path, ec)) {
    if (ec) {
      Log_Error("get_sorted_entries dir_iter error: " + ec.message());
      break;
    }
    std::error_code ec2;
    bool is_dir = entry.is_directory(ec2);
    string filename = entry.path().filename().string();
    string ext;
    if (filename.find('.') != string::npos)
      ext = filename.substr(filename.find_last_of("."));
    else
      ext = is_dir ? "directory" : "none";

    if (!contains(allowedExtentions, ext)) continue;

    string name_lower = filename;
    transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
    infos.push_back({entry.path(), is_dir, std::move(name_lower)});
  }

  // Sort using cached values — zero stat() calls in comparator; natural sort for numbers
  sort(infos.begin(), infos.end(), [](const EntryInfo& a, const EntryInfo& b) {
    if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
    return natural_less(a.name_lower, b.name_lower);
  });

  // Count folders from cached is_dir flags — no extra stat() calls
  int nf = 0;
  for (const auto& info : infos) {
    if (!info.is_dir) break;
    nf++;
  }
  *out_num_folders = nf;

  vector<fs::path> entries;
  entries.reserve(infos.size());
  for (auto& info : infos)
    entries.push_back(std::move(info.p));
  return entries;
}

// ── Cover thumbnail loading ───────────────────────────────────────────────────

static const char PAGECACHE_DIR[] = "/switch/WookReader/.pagecache";

// FNV-1a key shared with CBZPageLayout — must stay in sync.
static uint64_t comic_cache_key(const char* path) {
    struct stat st;
    uint64_t h = 14695981039346656037ULL;
    if (stat(path, &st) == 0) {
        for (const char* p = path; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ULL; }
        h ^= (uint64_t)(uint32_t)st.st_size;  h *= 1099511628211ULL;
        h ^= (uint64_t)(uint32_t)st.st_mtime; h *= 1099511628211ULL;
    }
    return h;
}

// Read first line of the page-name cache (.lst) — written by CBZPageLayout
// after a full enumeration. Returns the alphabetically first page name, or "".
static string cover_name_from_page_cache(const char* path) {
    char lst[512];
    snprintf(lst, sizeof(lst), "%s/%016llx.lst", PAGECACHE_DIR,
             (unsigned long long)comic_cache_key(path));
    FILE* f = fopen(lst, "r");
    if (!f) return "";
    char line[512] = {};
    bool got = fgets(line, sizeof(line), f) != nullptr;
    fclose(f);
    if (!got) return "";
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
    return string(line, len);
}

// Cover image disk cache (.cvr / .cvrn):
//   <key>.cvrn  — page name the bytes came from (text, one line)
//   <key>.cvr   — raw JPEG/PNG bytes of the cover image
// This eliminates both archive passes on every folder browse after the first.
// The cache auto-invalidates via the FNV-1a key (includes file size + mtime).

static bool load_cover_cache(uint64_t key, string& out_name, vector<uint8_t>& out_data) {
    char npath[512], dpath[512];
    snprintf(npath, sizeof(npath), "%s/%016llx.cvrn", PAGECACHE_DIR, (unsigned long long)key);
    snprintf(dpath, sizeof(dpath), "%s/%016llx.cvr",  PAGECACHE_DIR, (unsigned long long)key);
    FILE* fn = fopen(npath, "r");
    if (!fn) return false;
    char line[512] = {};
    bool got = fgets(line, sizeof(line), fn) != nullptr;
    fclose(fn);
    if (!got) return false;
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
    FILE* fd = fopen(dpath, "rb");
    if (!fd) return false;
    fseek(fd, 0, SEEK_END); long sz = ftell(fd); fseek(fd, 0, SEEK_SET);
    if (sz <= 0) { fclose(fd); return false; }
    out_data.resize((size_t)sz);
    bool ok = fread(out_data.data(), 1, (size_t)sz, fd) == (size_t)sz;
    fclose(fd);
    if (!ok) { out_data.clear(); return false; }
    out_name = string(line, len);
    return true;
}

static void save_cover_cache(uint64_t key, const string& name, const vector<uint8_t>& data) {
    if (name.empty() || data.empty()) return;
    mkdir(PAGECACHE_DIR, 0755);
    char npath[512], dpath[512];
    snprintf(npath, sizeof(npath), "%s/%016llx.cvrn", PAGECACHE_DIR, (unsigned long long)key);
    snprintf(dpath, sizeof(dpath), "%s/%016llx.cvr",  PAGECACHE_DIR, (unsigned long long)key);
    FILE* fn = fopen(npath, "w");
    if (fn) { fprintf(fn, "%s\n", name.c_str()); fclose(fn); }
    FILE* fd = fopen(dpath, "wb");
    if (fd) { fwrite(data.data(), 1, data.size(), fd); fclose(fd); }
}

static bool is_image_file(const char* name) {
  if (!name) return false;
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  char ext[8] = {};
  for (int i = 0; i < 7 && dot[i]; i++)
    ext[i] = (char)tolower((unsigned char)dot[i]);
  return strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0 ||
         strcmp(ext, ".png")  == 0 || strcmp(ext, ".webp") == 0 ||
         strcmp(ext, ".bmp")  == 0 || strcmp(ext, ".gif")  == 0;
}

// Returns true if name looks like the first page (suffix 0 or 1, or "cover").
// Used to short-circuit the cover scan for sorted archives.
static bool looks_like_first_page(const string& name) {
  // Check for "cover" anywhere in the basename
  string lower = name;
  for (char& c : lower) c = (char)tolower((unsigned char)c);
  if (lower.find("cover") != string::npos) return true;
  // Check last digit before extension is 0 or 1
  const char* dot = strrchr(name.c_str(), '.');
  const char* base = dot ? dot : name.c_str() + name.size();
  // walk backwards past the digit suffix
  while (base > name.c_str() && isdigit((unsigned char)*(base - 1))) base--;
  if (base < (dot ? dot : name.c_str() + name.size())) {
    // parse the number
    long num = strtol(base, nullptr, 10);
    if (num == 0 || num == 1) return true;
  }
  return false;
}

// Scan a RAR3 archive's file headers WITHOUT decompression.
// Jumps between blocks using PACK_SIZE from each file header (a raw fseek).
// Works identically for solid and non-solid RAR3 — no decompressor involved.
// Returns the alphabetically-first image entry name, or "" if not RAR3 / no image.
static string find_cover_in_rar3(const char* path) {
  string best;
  FILE* f = fopen(path, "rb");
  if (!f) return best;

  // RAR3 marker block (also serves as magic): 52 61 72 21 1A 07 00
  uint8_t magic[7];
  if (fread(magic, 1, 7, f) != 7 ||
      memcmp(magic, "\x52\x61\x72\x21\x1a\x07\x00", 7) != 0) {
    fclose(f); return best;  // not RAR3 (RAR5 has 01 at byte 6 → fails here)
  }

  // 3-second budget to prevent hangs on corrupt/huge archives
  static const uint64_t BUDGET_TICKS = 57600000ULL;  // ~3s at 19.2 MHz
  uint64_t t0 = armGetSystemTick();

  long pos = 7;  // byte offset of first real block (archive header)
  for (;;) {
    if (armGetSystemTick() - t0 > BUDGET_TICKS) break;
    if (fseek(f, pos, SEEK_SET) != 0) break;

    // Base block header: HEAD_CRC(2) HEAD_TYPE(1) HEAD_FLAGS(2) HEAD_SIZE(2)
    uint8_t h[7];
    if (fread(h, 1, 7, f) != 7) break;

    const uint8_t  btype = h[2];
    const uint16_t flags = (uint16_t)(h[3] | (h[4] << 8));
    const uint16_t hsize = (uint16_t)(h[5] | (h[6] << 8));
    if (hsize < 7) break;           // corrupt
    if (btype == 0x7B) break;       // end-of-archive marker

    // add_size: for all blocks, stored as 4 bytes at pos+7 when flags & 0x8000.
    // For file blocks (0x74) it equals PACK_SIZE (compressed data size).
    uint32_t add_size = 0;
    if (flags & 0x8000) {
      uint8_t s[4];
      if (fseek(f, pos + 7, SEEK_SET) != 0) break;
      if (fread(s, 1, 4, f) != 4) break;
      add_size = (uint32_t)s[0] | (uint32_t)s[1]<<8 |
                 (uint32_t)s[2]<<16 | (uint32_t)s[3]<<24;
    }

    if (btype == 0x74) {  // file block
      // RAR3 file header layout from pos:
      //  0- 6  base header (already read)
      //  7-10  PACK_SIZE   (= add_size)
      // 11-14  UNP_SIZE
      // 15     HOST_OS
      // 16-19  FILE_CRC
      // 20-23  FTIME
      // 24     UNP_VER
      // 25     METHOD
      // 26-27  NAME_SIZE
      // 28-31  ATTR
      // [32-39 HIGH_PACK/UNP if flags & 0x0100]
      // 32 (or 40)  filename bytes
      uint8_t ns[2];
      if (fseek(f, pos + 26, SEEK_SET) == 0 && fread(ns, 1, 2, f) == 2) {
        const uint16_t name_size = (uint16_t)(ns[0] | (ns[1] << 8));
        if (name_size > 0 && name_size < 512) {
          const long name_off = pos + 32 + ((flags & 0x0100) ? 8 : 0);
          char namebuf[513];
          if (fseek(f, name_off, SEEK_SET) == 0 &&
              (size_t)fread(namebuf, 1, name_size, f) == name_size) {
            namebuf[name_size] = '\0';
            for (int i = 0; namebuf[i]; i++)
              if (namebuf[i] == '\\') namebuf[i] = '/';
            if (is_image_file(namebuf)) {
              string sn(namebuf);
              if (best.empty() || sn < best) best = sn;
            }
          }
        }
      }
    }

    const long next = pos + (long)hsize + (long)add_size;
    if (next <= pos) break;  // no forward progress — corrupt archive
    pos = next;
  }
  fclose(f);
  return best;
}

// Extract cover from a comic archive as pre-scaled RGBA pixels.
// Background-thread safe: no SDL renderer calls, no shared fz_context.
// Returns malloc'd RGBA pixels (stride = iw*4), or nullptr. Caller must free().
//
// Cover selection priority (fastest to slowest):
//   1. Cover disk cache (.cvr) matches page-name cache → instant, correct
//   2. Cover disk cache exists, no page-name cache → instant, best-effort
//   3. Page-name cache exists, cover cache stale/absent → re-extract correct page
//   4. No caches → take first image entry from archive (fast), save to cover cache
static unsigned char* extract_cbz_cover_pixels(const char* path, int tw, int th,
                                                int* out_iw, int* out_ih) {
  uint64_t key = comic_cache_key(path);

  // ── Step 1: check what the page-name cache says the cover should be ─────────
  string pcache_name = cover_name_from_page_cache(path);

  // ── Step 2: check cover disk cache ──────────────────────────────────────────
  string cvr_name;
  vector<uint8_t> cover_data;
  bool cvr_hit = load_cover_cache(key, cvr_name, cover_data);

  if (cvr_hit) {
    // Cover cache is valid if it matches the page-name cache, or if we have no
    // page-name cache yet (best-effort is fine until the comic is opened).
    if (pcache_name.empty() || pcache_name == cvr_name) {
      // Fast path: decode cached bytes directly, no archive access.
      goto decode;
    }
    // Page-name cache disagrees → cover cache is stale. Fall through to re-extract.
    cover_data.clear();
  }

  // ── Step 3/4: extract from archive ──────────────────────────────────────────
  {
    // Determine target entry name.
    string target_name = pcache_name;  // correct if comic was opened before

    if (target_name.empty()) {
      // Fast path for RAR3 (CBR): scan file headers directly using PACK_SIZE fseeks.
      // Zero decompression — correct for both solid and non-solid, completes in ms.
      target_name = find_cover_in_rar3(path);
    }

    // Single-pass libarchive: find target (if not known) AND extract in one traversal.
    {
      struct archive* a = archive_read_new();
      archive_read_support_format_all(a);
      archive_read_support_filter_all(a);
      if (archive_read_open_filename(a, path, 1048576) != ARCHIVE_OK) {
        archive_read_free(a); return nullptr;
      }

      // Helper lambda: read entry data into cover_data
      auto read_entry_data = [&](struct archive* ar, struct archive_entry* e) {
        la_int64_t sz = archive_entry_size(e);
        if (sz > 0) {
          cover_data.resize((size_t)sz);
          uint8_t* dst = cover_data.data(); size_t rem = (size_t)sz;
          while (rem > 0) { la_ssize_t n = archive_read_data(ar, dst, rem); if (n <= 0) break; dst += n; rem -= (size_t)n; }
          cover_data.resize((size_t)sz - rem);
        } else {
          cover_data.reserve(4 * 1024 * 1024);
          uint8_t chunk[65536]; la_ssize_t n;
          while ((n = archive_read_data(ar, chunk, sizeof(chunk))) > 0)
            cover_data.insert(cover_data.end(), chunk, chunk + (size_t)n);
        }
      };

      struct archive_entry* entry;
      if (!target_name.empty()) {
        // Target known (from page-name cache or RAR3 scan): seek directly.
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
          if (archive_entry_filetype(entry) == AE_IFDIR) { archive_read_data_skip(a); continue; }
          const char* name = archive_entry_pathname(entry);
          if (!name || string(name) != target_name) { archive_read_data_skip(a); continue; }
          read_entry_data(a, entry);
          break;
        }
      } else {
        // Target unknown: scan for first image (sorted), extract on first encounter.
        // For formats with sorted entries (most CBZ), the first image IS the cover.
        // For unsorted archives, we take the first image found (3s budget).
        static const uint64_t BUDGET_TICKS = 57600000ULL; // ~3s at 19.2 MHz
        uint64_t t0 = armGetSystemTick();
        string best_name;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
          if (archive_entry_filetype(entry) == AE_IFDIR) { archive_read_data_skip(a); continue; }
          const char* n = archive_entry_pathname(entry);
          if (!n || !is_image_file(n)) { archive_read_data_skip(a); continue; }
          string sn(n);
          if (best_name.empty() || sn < best_name) {
            best_name = sn;
            // Extract this candidate (may be overwritten if a better name found,
            // but most archives have sorted entries so this is usually final)
            cover_data.clear();
            read_entry_data(a, entry);
          } else {
            archive_read_data_skip(a);
          }
          if (armGetSystemTick() - t0 > BUDGET_TICKS) break;
        }
        target_name = best_name;
      }
      archive_read_free(a);
      if (cover_data.empty()) return nullptr;
    }

    // Save to cover disk cache so future folder browses are instant.
    save_cover_cache(key, target_name, cover_data);
    cvr_name = target_name;
  }

  decode:

  // Decode image — use libjpeg-turbo for JPEG (2–3x faster than stb_image on ARM NEON).
  const uint8_t* d = cover_data.data();
  bool is_jpeg = (cover_data.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8);
  int src_w = 0, src_h = 0;
  unsigned char* src_px = nullptr;
  if (is_jpeg) {
    tjhandle tj = tjInitDecompress();
    if (tj) {
      int subsamp = 0, cs = 0;
      if (tjDecompressHeader3(tj, d, (unsigned long)cover_data.size(),
                              &src_w, &src_h, &subsamp, &cs) == 0) {
        src_px = (unsigned char*)malloc((size_t)src_w * src_h * 4);
        if (src_px && tjDecompress2(tj, d, (unsigned long)cover_data.size(),
                                    src_px, src_w, 0, src_h, TJPF_RGBA,
                                    TJFLAG_FASTDCT) != 0) {
          free(src_px); src_px = nullptr;
        }
      }
      tjDestroy(tj);
    }
  }
  if (!src_px) {  // non-JPEG or turbo failed — fall back to stb_image
    int ch = 0;
    src_px = stbi_load_from_memory(d, (int)cover_data.size(), &src_w, &src_h, &ch, 4);
    is_jpeg = false;  // track allocator: stbi_image_free vs free
  }
  if (!src_px) return nullptr;

  // Stretch to exact cell dimensions — uniform grid, ratios are close enough
  int dw = tw;
  int dh = th;

  SDL_Surface* src_surf = SDL_CreateRGBSurfaceFrom(
      src_px, src_w, src_h, 32, src_w * 4,
      0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
  SDL_Surface* dst_surf = SDL_CreateRGBSurface(
      0, dw, dh, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

  unsigned char* result = nullptr;
  if (src_surf && dst_surf) {
    SDL_BlitScaled(src_surf, NULL, dst_surf, NULL);
    result = (unsigned char*)malloc((size_t)dw * dh * 4);
    if (result) { memcpy(result, dst_surf->pixels, (size_t)dw * dh * 4); }
    *out_iw = dw; *out_ih = dh;
  }
  SDL_FreeSurface(dst_surf);
  SDL_FreeSurface(src_surf);
  if (is_jpeg) free(src_px); else stbi_image_free(src_px);
  return result;
}

// Extract cover from a PDF/EPUB as pre-scaled RGBA pixels.
// Background-thread safe: uses fz_clone_context (independent MuPDF context).
// Returns malloc'd RGBA pixels, or nullptr. Caller must free().
static unsigned char* extract_pdf_cover_pixels(const char* path, int tw, int th,
                                                int* out_iw, int* out_ih) {
  if (!ctx) return nullptr;
  fz_context* bg_ctx = fz_clone_context(ctx);
  if (!bg_ctx) return nullptr;

  fz_document* doc  = nullptr;
  fz_page*     page = nullptr;
  fz_pixmap*   pix  = nullptr;
  unsigned char* result = nullptr;

  fz_try(bg_ctx) {
    doc = fz_open_document(bg_ctx, path);
    if (!doc || fz_count_pages(bg_ctx, doc) == 0)
      fz_throw(bg_ctx, FZ_ERROR_GENERIC, "no pages");
    page = fz_load_page(bg_ctx, doc, 0);
    fz_rect bounds = fz_bound_page(bg_ctx, page);
    float scale = fminf((float)tw / bounds.x1, (float)th / bounds.y1);
    pix = fz_new_pixmap_from_page_contents(
        bg_ctx, page, fz_scale(scale, scale), fz_device_rgb(bg_ctx), 1);
    size_t nbytes = (size_t)pix->h * pix->stride;
    result = (unsigned char*)malloc(nbytes);
    if (result) { memcpy(result, pix->samples, nbytes); }
    *out_iw = pix->w; *out_ih = pix->h;
  }
  fz_catch(bg_ctx) { result = nullptr; }

  if (pix)  fz_drop_pixmap(bg_ctx, pix);
  if (page) fz_drop_page(bg_ctx, page);
  if (doc)  fz_drop_document(bg_ctx, doc);
  fz_drop_context(bg_ctx);
  return result;
}

// Create SDL_Texture from pre-scaled RGBA pixels on the main thread, then free pixels.
static SDL_Texture* pixels_to_texture(unsigned char* pixels, int iw, int ih) {
  if (!pixels) return nullptr;
  SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
      pixels, iw, ih, 32, iw * 4,
      0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
  SDL_Texture* tex = surf ? SDL_CreateTextureFromSurface(RENDERER, surf) : nullptr;
  SDL_FreeSurface(surf);
  free(pixels);
  return tex;
}

// ── Async cover loader ────────────────────────────────────────────────────────
// One worker thread decodes cover pixels off the main thread.
// Synchronization: two atomic flags (job_ready / result_ready) with ACQUIRE/RELEASE.

struct CoverLoader {
  // Job (written by main before RELEASE job_ready=1; read by worker after ACQUIRE)
  char job_path[512];
  int  job_tw, job_th, job_index;
  bool job_is_pdf;
  volatile int job_ready   = 0;

  // Result (written by worker before RELEASE result_ready=1; read by main after ACQUIRE)
  unsigned char* result_pixels = nullptr;
  int            result_iw     = 0;
  int            result_ih     = 0;
  int            result_index  = -1;
  volatile int   result_ready  = 0;

  volatile int should_exit = 0;
  volatile int stale       = 0;  // set by main to discard an in-flight result
  Thread       thread;
};

static void cover_loader_worker(void* arg) {
  CoverLoader* cl = static_cast<CoverLoader*>(arg);
  while (!__atomic_load_n(&cl->should_exit, __ATOMIC_ACQUIRE)) {
    if (!__atomic_load_n(&cl->job_ready, __ATOMIC_ACQUIRE)) {
      svcSleepThread(2000000LL); continue;  // 2 ms poll
    }
    string path   = cl->job_path;
    int    tw     = cl->job_tw, th = cl->job_th;
    int    idx    = cl->job_index;
    bool   is_pdf = cl->job_is_pdf;
    __atomic_store_n(&cl->job_ready, 0, __ATOMIC_RELEASE);

    int iw = 0, ih = 0;
    unsigned char* pixels = is_pdf
        ? extract_pdf_cover_pixels(path.c_str(), tw, th, &iw, &ih)
        : extract_cbz_cover_pixels(path.c_str(), tw, th, &iw, &ih);

    cl->result_pixels = pixels;
    cl->result_iw     = iw;
    cl->result_ih     = ih;
    cl->result_index  = idx;
    __atomic_store_n(&cl->result_ready, 1, __ATOMIC_RELEASE);
  }
}

// ── Main chooser ─────────────────────────────────────────────────────────────

void Menu_StartChoosing() {
  SDL_Texture* folder_textures[] = {
      adwaita_folder,    black_folder,       blue_grey_folder, blue_folder,
      breeze_folder,     brown_folder,       carmine_folder,   cyan_folder,
      dark_cyan_folder,  deep_orange_folder, green_folder,     grey_folder,
      indigo_folder,     magenta_folder,     nordic_folder,    orange_folder,
      pale_brown_folder, pale_orange_folder, pink_folder,      red_folder,
      teal_folder,       violet_folder,      white_folder,     yaru_folder,
      yellow_folder};
  SDL_Texture* book_textures[] = {
      adwaita_book,    black_book,       blue_grey_book, blue_book,
      breeze_book,     brown_book,       carmine_book,   cyan_book,
      dark_cyan_book,  deep_orange_book, green_book,     grey_book,
      indigo_book,     magenta_book,     nordic_book,    orange_book,
      pale_brown_book, pale_orange_book, pink_book,      red_book,
      teal_book,       violet_book,      white_book,     yaru_book,
      yellow_book};

  unsigned int chosenFolderColor = 0;
  unsigned int chosenBookColor   = 0;
  int scroll_option = 1;
  int zoom_option   = 1;

  string colors[] = {
      "Adwaita", "Black",      "Blue_Grey",   "Blue",      "Breeze",
      "Brown",   "Carmine",    "Cyan",        "Dark_Cyan", "Deep_Orange",
      "Green",   "Grey",       "Indigo",      "Magenta",   "Nordic",
      "Orange",  "Pale_Brown", "Pale_Orange", "Pink",      "Red",
      "Teal",    "Violet",     "White",       "Yaru",      "Yellow"};

  load_config(&chosenFolderColor, &chosenBookColor, &scroll_option,
              &zoom_option, &configDarkMode);

  SDL_Texture* folder_image = folder_textures[chosenFolderColor];
  SDL_Texture* book_image   = book_textures[chosenBookColor];

  float zoom_amount = 0.3f * zoom_option;
  int   scroll_speed = 3 * scroll_option;

  int chosen_index = 0;

  list<string> allowedExtentions = {".pdf", ".epub", ".cbz", ".cbr",
                                    ".cbt", ".cb7",  ".xps", "directory"};
  list<string> warnedExtentions  = {".epub", ".xps"};

  bool drawOption  = false;
  int  option_index   = 0;
  int  amountOfOptions = 4;

  string path = "/switch/WookReader";

  int windowX, windowY;
  SDL_GetWindowSize(WINDOW, &windowX, &windowY);
  int warningWidth  = 700;
  int warningHeight = 300;

  // ── Dynamic grid dimensions (ZL = zoom out / ZR = zoom in) ──────────────────
  int cols    = 5;
  int cell_w  = 1280 / cols;
  int thumb_w = cell_w - 2;  // 1px gap per side — covers almost touching
  int thumb_h = (int)(thumb_w * (3000.0f / 1988.0f));  // comic page portrait ratio
  int cell_h  = thumb_h;     // title overlaid on cover, no separate row below

  // Grid state
  int numFolders = 0;
  vector<SDL_Texture*> cover_textures;
  int scroll_y = 0;
  int cover_load_index = 0;  // next cover index to submit to background loader

  vector<fs::path> sorted_entries;
  int amountOfFiles = 0;
  bool isWarningOnScreen = false;

  // Notes overlay state
  bool drawNotesChooser = false;
  string notesChooserPath;   // absolute path of the comic whose note is shown
  string notesChooserText;   // cached note content

  // Cache for truncated display filenames: key = "fname|pixel_limit" → truncated string.
  unordered_map<string, string> trunc_cache;

  // Background cover loaders — two workers on cores 1 and 2
  static const int N_WORKERS = 2;
  CoverLoader cl[N_WORKERS];
  int cl_in_flight[N_WORKERS] = {-1, -1};

  // ── Helpers ────────────────────────────────────────────────────────────────
  // Keep the selected item visible in the viewport.
  auto update_scroll = [&]() {
    int top, bot;
    if (chosen_index < numFolders) {
      top = chosen_index * FOLDER_H;
      bot = top + FOLDER_H;
    } else {
      int bi  = chosen_index - numFolders;
      int row = bi / cols;
      top = numFolders * FOLDER_H + row * cell_h;
      bot = top + cell_h;
    }
    int usable = windowY - BOTTOM_H;
    if (top < scroll_y)          scroll_y = top;
    if (bot > scroll_y + usable) scroll_y = bot - usable;
    if (scroll_y < 0) scroll_y = 0;
  };

  // Recompute derived grid dimensions from cols.
  auto recalc_grid = [&]() {
    cell_w  = 1280 / cols;
    thumb_w = cell_w - 2;
    thumb_h = (int)(thumb_w * (3000.0f / 1988.0f));  // comic page portrait ratio
    cell_h  = thumb_h;
  };

  // Pick the largest label font that suits the current cell width.
  auto pick_label_font = [&]() -> TTF_Font* {
    if (cell_w >= 300) return ROBOTO_25;
    if (cell_w >= 190) return ROBOTO_20;
    return ROBOTO_15;
  };

  // Load (or reload) a directory: refresh sorted entries and cover thumbnails.
  auto enter_directory = [&](const string& dir) {
    // Cancel any queued/in-flight jobs instantly — no waiting.
    // Workers that are mid-decode will finish, but their results are marked stale
    // and discarded by the result-consumption loop below.
    for (int w = 0; w < N_WORKERS; w++) {
      __atomic_store_n(&cl[w].job_ready, 0, __ATOMIC_RELEASE);  // cancel queued job
      if (cl_in_flight[w] >= 0) {
        __atomic_store_n(&cl[w].stale, 1, __ATOMIC_RELEASE);    // discard when ready
        cl_in_flight[w] = -1;
      }
    }

    cover_textures.clear();  // cache owns textures, do NOT destroy them here
    trunc_cache.clear();
    scroll_y     = 0;
    chosen_index = 0;

    sorted_entries = get_sorted_entries(dir, allowedExtentions, &numFolders);
    amountOfFiles  = (int)sorted_entries.size();

    int numBooks = amountOfFiles - numFolders;
    cover_textures.resize(numBooks, nullptr);
    cover_load_index = 0;
  };

  enter_directory(path);

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  hidInitializeTouchScreen();

  // Start background cover loaders on cores 1 and 2
  int cover_cores[N_WORKERS] = {1, 2};
  for (int w = 0; w < N_WORKERS; w++) {
    threadCreate(&cl[w].thread, cover_loader_worker, &cl[w],
                 nullptr, 512 * 1024, 0x2C, cover_cores[w]);
    threadStart(&cl[w].thread);
  }

  float touch_prev_y0    = 0.0f;
  int   touch_prev_count = 0;
  float touch_start_x    = 0.0f, touch_start_y = 0.0f;
  bool  touch_dragging   = false;

  // ── Main loop ──────────────────────────────────────────────────────────────
  while (appletMainLoop()) {
    SDL_TextCache_NextFrame();
    SDL_PumpEvents();  // drain SDL's internal event queue to prevent HID interference
    SDL_Color textColor    = configDarkMode ? WHITE : BLACK;
    SDL_Color backColor    = configDarkMode ? BACK_BLACK : BACK_WHITE;
    SDL_Color selectorColor = configDarkMode ? SELECTOR_COLOUR_DARK
                                             : SELECTOR_COLOUR_LIGHT;

    SDL_ClearScreen(RENDERER, backColor);
    SDL_RenderClear(RENDERER);

    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);
    u64 kUp   = padGetButtonsUp(&pad);

    // ── Input ────────────────────────────────────────────────────────────────

    if (kUp & HidNpadButton_Plus) {
      if (drawOption) {
        // Closing options menu — save immediately
        save_config(chosenFolderColor, chosenBookColor, scroll_option,
                    zoom_option, configDarkMode);
      }
      drawOption = !drawOption;
    }

    if (kDown & HidNpadButton_B) {
      if (drawNotesChooser) {
        drawNotesChooser = false;
      } else if (isWarningOnScreen) {
        isWarningOnScreen = false;
      } else if (drawOption) {
        save_config(chosenFolderColor, chosenBookColor, scroll_option,
                    zoom_option, configDarkMode);
        drawOption = false;
      } else if (path == "/switch/WookReader") {
        save_config(chosenFolderColor, chosenBookColor, scroll_option,
                    zoom_option, configDarkMode);
        break;
      } else {
        // Navigate to parent directory
        string parent = path;
        while (!parent.empty() && parent.back() != '/') parent.pop_back();
        if (!parent.empty() && parent.back() == '/') parent.pop_back();
        if (parent.empty()) parent = "/";
        path = parent;
        enter_directory(path);
      }
    }

    if (kDown & HidNpadButton_A && !drawOption && !drawNotesChooser &&
        chosen_index < (int)sorted_entries.size()) {
      const fs::path& sel = sorted_entries[chosen_index];
      string filename = sel.filename().string();
      string extention;
      if (filename.find('.') != string::npos)
        extention = filename.substr(filename.find_last_of("."));
      else {
        std::error_code ec;
        extention = fs::is_directory(sel, ec) ? "directory" : "none";
      }

      if (contains(warnedExtentions, extention)) {
        if (isWarningOnScreen) {
          isWarningOnScreen = false;
          string book = sel.string();
          Menu_OpenBook((char*)book.c_str(), scroll_speed, zoom_amount);
        } else {
          isWarningOnScreen = true;
        }
      } else if (extention == "directory") {
        path = sel.string();
        enter_directory(path);
      } else {
        string book = sel.string();
        Menu_OpenBook((char*)book.c_str(), scroll_speed, zoom_amount);
      }
    }

    // ── Up / Down navigation ─────────────────────────────────────────────────
    if (kDown & (HidNpadButton_Up | HidNpadButton_StickRUp)) {
      if (drawOption) {
        option_index = (option_index > 0) ? option_index - 1 : amountOfOptions - 1;
      } else if (!isWarningOnScreen) {
        if (chosen_index < numFolders) {
          // Folder list: move up or wrap to last book
          if (chosen_index > 0)
            chosen_index--;
          else
            chosen_index = amountOfFiles - 1;
        } else {
          // Book grid: move up one row or go to last folder
          int bi  = chosen_index - numFolders;
          int row = bi / cols;
          if (row > 0)
            chosen_index -= cols;
          else
            chosen_index = (numFolders > 0) ? numFolders - 1 : amountOfFiles - 1;
        }
        update_scroll();
      }
    }

    if (kDown & (HidNpadButton_Down | HidNpadButton_StickRDown)) {
      if (drawOption) {
        option_index = (option_index == amountOfOptions - 1) ? 0 : option_index + 1;
      } else if (!isWarningOnScreen) {
        int numBooks = amountOfFiles - numFolders;
        if (chosen_index < numFolders) {
          // Folder list: move down (may cross into book grid)
          chosen_index++;
          if (chosen_index >= amountOfFiles) chosen_index = 0;
        } else {
          // Book grid: move down one row, clamp to last book, or wrap
          int bi = chosen_index - numFolders;
          if (bi + cols < numBooks)
            chosen_index += cols;
          else if (chosen_index < amountOfFiles - 1)
            chosen_index = amountOfFiles - 1;
          else
            chosen_index = 0;
        }
        update_scroll();
      }
    }

    // ── Left / Right navigation (grid columns + option menu) ─────────────────
    if (kDown & HidNpadButton_Right) {
      if (drawOption) {
        switch (option_index) {
          case 0:
            chosenFolderColor = (chosenFolderColor < COLORS_SIZE)
                                    ? chosenFolderColor + 1 : 0;
            folder_image = folder_textures[chosenFolderColor];
            break;
          case 1:
            chosenBookColor = (chosenBookColor < COLORS_SIZE)
                                  ? chosenBookColor + 1 : 0;
            book_image = book_textures[chosenBookColor];
            break;
          case 2:
            scroll_option = (scroll_option < 4) ? scroll_option + 1 : 1;
            scroll_speed  = SCROLL * scroll_option;
            break;
          case 3:
            zoom_option  = (zoom_option < 4) ? zoom_option + 1 : 1;
            zoom_amount  = ZOOM * zoom_option;
            break;
          default: break;
        }
      } else if (!isWarningOnScreen && chosen_index >= numFolders) {
        // Move right within the grid row
        int bi  = chosen_index - numFolders;
        int col = bi % cols;
        if (col < cols - 1 && chosen_index < amountOfFiles - 1) {
          chosen_index++;
          update_scroll();
        }
      }
    }

    if (kDown & HidNpadButton_Left) {
      if (drawOption) {
        switch (option_index) {
          case 0:
            chosenFolderColor = (chosenFolderColor > 0)
                                    ? chosenFolderColor - 1 : COLORS_SIZE;
            folder_image = folder_textures[chosenFolderColor];
            break;
          case 1:
            chosenBookColor = (chosenBookColor > 0)
                                  ? chosenBookColor - 1 : COLORS_SIZE;
            book_image = book_textures[chosenBookColor];
            break;
          case 2:
            scroll_option = (scroll_option > 1) ? scroll_option - 1 : 4;
            scroll_speed  = SCROLL * scroll_option;
            break;
          case 3:
            zoom_option = (zoom_option > 1) ? zoom_option - 1 : 4;
            zoom_amount = ZOOM * zoom_option;
            break;
          default: break;
        }
      } else if (!isWarningOnScreen && chosen_index >= numFolders) {
        // Move left within the grid row
        int bi  = chosen_index - numFolders;
        int col = bi % cols;
        if (col > 0) {
          chosen_index--;
          update_scroll();
        }
      }
    }

    if (kUp & HidNpadButton_Minus) {
      configDarkMode = !configDarkMode;
    }

    // ── Y: notes for selected book ────────────────────────────────────────────
    if (kDown & HidNpadButton_Y) {
      if (drawNotesChooser) {
        drawNotesChooser = false;
      } else if (!drawOption && !isWarningOnScreen &&
                 chosen_index >= numFolders &&
                 chosen_index < amountOfFiles) {
        notesChooserPath = sorted_entries[chosen_index].string();
        notesChooserText = chooser_note_read(notesChooserPath);
        drawNotesChooser = true;
      }
    }

    // ── Notes overlay input ───────────────────────────────────────────────────
    if (drawNotesChooser && kDown & HidNpadButton_A) {
      SwkbdConfig swkbd;
      swkbdCreate(&swkbd, 0);
      swkbdConfigSetType(&swkbd, SwkbdType_Normal);
      swkbdConfigSetHeaderText(&swkbd, "Edit note");
      swkbdConfigSetStringLenMax(&swkbd, 2000);
      swkbdConfigSetStringLenMin(&swkbd, 0);
      swkbdConfigSetBlurBackground(&swkbd, 1);
      swkbdConfigSetInitialText(&swkbd, notesChooserText.c_str());
      swkbdConfigSetInitialCursorPos(&swkbd, 1);
      char buf[2048] = {0};
      Result rc = swkbdShow(&swkbd, buf, sizeof(buf));
      swkbdClose(&swkbd);
      if (R_SUCCEEDED(rc)) {
        notesChooserText = buf;
        chooser_note_save(notesChooserPath, notesChooserText);
      }
    }
    // ── Grid zoom (ZR = fewer cols / ZL = more cols) ──────────────────────────
    if (!drawOption && !isWarningOnScreen) {
      if ((kDown & HidNpadButton_ZR && cols > 2) ||
          (kDown & HidNpadButton_ZL && cols < 8)) {
        if (kDown & HidNpadButton_ZR) cols--;
        else                          cols++;
        recalc_grid();
        trunc_cache.clear();  // cell_w changed, so pixel limits are now different
        // Repopulate covers from cache as placeholders at old size (rendered scaled).
        // Reset decode cursor so workers re-decode all covers at new thumb_w/th.
        int numBooks = amountOfFiles - numFolders;
        cover_textures.assign(numBooks, nullptr);
        for (int i = 0; i < numBooks; i++)
          cover_textures[i] = g_cover_cache.get(sorted_entries[numFolders + i].string());
        cover_load_index = 0;  // re-decode at new size in background
        update_scroll();
      }
    }

    // ── Touch input ───────────────────────────────────────────────────────────
    {
      HidTouchScreenState ts = {0};
      hidGetTouchScreenStates(&ts, 1);

      if (!drawOption && !isWarningOnScreen)
      {
        if (ts.count == 1)
        {
          float cx = (float)ts.touches[0].x;
          float cy = (float)ts.touches[0].y;

          if (touch_prev_count == 0)
          {
            touch_start_x  = cx;
            touch_start_y  = cy;
            touch_dragging = false;
          }
          else if (touch_prev_count == 1)
          {
            float dy    = cy - touch_prev_y0;
            float moved = sqrtf((cx - touch_start_x) * (cx - touch_start_x) +
                                (cy - touch_start_y) * (cy - touch_start_y));
            if (moved > 18.0f)
              touch_dragging = true;

            if (touch_dragging)
            {
              // Natural scroll: drag up → content goes up (scroll_y increases)
              scroll_y -= (int)roundf(dy);
              int numBooks = amountOfFiles - numFolders;
              int numRows  = (numBooks + cols - 1) / cols;
              int total_h  = numFolders * FOLDER_H + numRows * cell_h;
              int max_sy   = max(0, total_h - (windowY - BOTTOM_H));
              if (scroll_y < 0)      scroll_y = 0;
              if (scroll_y > max_sy) scroll_y = max_sy;
            }
          }

          touch_prev_y0 = cy;
        }
        else if (ts.count == 0 && touch_prev_count == 1 && !touch_dragging)
        {
          // Tap: ignore taps in the bottom bar
          if (touch_start_y < (float)(windowY - BOTTOM_H))
          {
            float content_ty = touch_start_y + scroll_y;
            int   tapped     = -1;

            int folder_idx = (int)(content_ty / FOLDER_H);
            if (folder_idx >= 0 && folder_idx < numFolders)
            {
              tapped = folder_idx;
            }
            else
            {
              int books_top = numFolders * FOLDER_H;
              if (content_ty >= books_top)
              {
                int rel_y = (int)(content_ty - books_top);
                int row   = rel_y / cell_h;
                int col   = (int)(touch_start_x / cell_w);
                int bi    = row * cols + col;
                if (bi >= 0 && bi < amountOfFiles - numFolders)
                  tapped = numFolders + bi;
              }
            }

            if (tapped >= 0 && tapped < (int)sorted_entries.size())
            {
              chosen_index = tapped;
              const fs::path& sel = sorted_entries[chosen_index];
              string filename = sel.filename().string();
              string extention;
              if (filename.find('.') != string::npos)
                extention = filename.substr(filename.find_last_of("."));
              else {
                std::error_code ec;
                extention = fs::is_directory(sel, ec) ? "directory" : "none";
              }

              if (contains(warnedExtentions, extention))
                isWarningOnScreen = true;
              else if (extention == "directory") {
                path = sel.string();
                enter_directory(path);
              } else {
                string book = sel.string();
                Menu_OpenBook((char*)book.c_str(), scroll_speed, zoom_amount);
              }
            }
          }
        }
      }

      touch_prev_count = ts.count;
    }

    // ── Async cover loading ───────────────────────────────────────────────────
    // Consume completed results from all workers (texture creation must be on main thread)
    for (int w = 0; w < N_WORKERS; w++) {
      if (!__atomic_load_n(&cl[w].result_ready, __ATOMIC_ACQUIRE)) continue;
      int idx = cl[w].result_index;
      if (__atomic_load_n(&cl[w].stale, __ATOMIC_ACQUIRE)) {
        // Result is from a previous directory — discard it
        free(cl[w].result_pixels); cl[w].result_pixels = nullptr;
        __atomic_store_n(&cl[w].result_ready, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&cl[w].stale,        0, __ATOMIC_RELEASE);
        cl_in_flight[w] = -1;
        continue;
      }
      SDL_Texture* tex = pixels_to_texture(cl[w].result_pixels, cl[w].result_iw, cl[w].result_ih);
      cl[w].result_pixels = nullptr;
      __atomic_store_n(&cl[w].result_ready, 0, __ATOMIC_RELEASE);
      cl_in_flight[w] = -1;
      if (idx >= 0 && idx < (int)cover_textures.size()) {
        string fpath = sorted_entries[numFolders + idx].string();
        if (tex) g_cover_cache.put(fpath, tex);
        cover_textures[idx] = tex;
      }
    }
    // Submit next uncached covers to any idle workers
    for (int w = 0; w < N_WORKERS; w++) {
      if (cl_in_flight[w] >= 0) continue;  // this worker is busy
      // Drain cache hits first — only accept if the cached texture is large
      // enough for the current thumb size (avoids blurry upscaling on zoom-in).
      while (cover_load_index < (int)cover_textures.size()) {
        SDL_Texture* cached = g_cover_cache.get(
            sorted_entries[numFolders + cover_load_index].string());
        if (!cached) break;
        int ctw, cth;
        SDL_QueryTexture(cached, nullptr, nullptr, &ctw, &cth);
        float s = (float)thumb_w / ctw;  // fill-width mode: only check width scale
        if (s > 1.2f) break;  // would upscale >20% — re-decode at current size
        cover_textures[cover_load_index] = cached;
        cover_load_index++;
      }
      if (cover_load_index >= (int)cover_textures.size()) break;
      // Submit next uncached to this worker
      int i = cover_load_index++;
      string fpath = sorted_entries[numFolders + i].string();
      string ext; size_t dot = fpath.find_last_of('.');
      if (dot != string::npos) ext = fpath.substr(dot);
      transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      bool is_pdf = (ext != ".cbz" && ext != ".cbr" && ext != ".cbt" && ext != ".cb7");
      strncpy(cl[w].job_path, fpath.c_str(), sizeof(cl[w].job_path) - 1);
      cl[w].job_path[sizeof(cl[w].job_path) - 1] = '\0';
      cl[w].job_tw = thumb_w; cl[w].job_th = thumb_h;
      cl[w].job_index = i; cl[w].job_is_pdf = is_pdf;
      cl_in_flight[w] = i;
      __atomic_store_n(&cl[w].job_ready, 1, __ATOMIC_RELEASE);
    }

    // ── Rendering ─────────────────────────────────────────────────────────────

    // Reset clip rect from previous frame before drawing bottom bar
    SDL_RenderSetClipRect(RENDERER, NULL);

    // Bottom bar (always visible, drawn first so content clips over it)
    {
      int exitWidth = 0;
      TTF_SizeText(ROBOTO_20, "Exit", &exitWidth, NULL);
      SDL_DrawButtonPrompt(RENDERER, button_b, ROBOTO_20, textColor, "Exit",
                           windowX - exitWidth - 50, windowY - 10, 35, 35, 5, 0);

      int themeWidth = 0;
      TTF_SizeText(ROBOTO_20, "Switch Theme", &themeWidth, NULL);
      SDL_DrawButtonPrompt(RENDERER, button_minus, ROBOTO_20, textColor,
                           "Switch Theme", windowX - themeWidth - 50,
                           windowY - 40, 35, 35, 5, 0);
      SDL_DrawButtonPrompt(RENDERER, button_plus, ROBOTO_20, textColor,
                           "Options Menu", windowX - themeWidth - 50,
                           windowY - 70, 35, 35, 5, 0);

      string display_path = path.size() > 18 ? path.substr(18) : "";
      SDL_DrawText(RENDERER, ROBOTO_25, 10, windowY - 40, textColor,
                   display_path.c_str());
    }

    // Clip content area to avoid overflowing into the bottom bar
    SDL_Rect clip = {0, 0, windowX, windowY - BOTTOM_H};
    SDL_RenderSetClipRect(RENDERER, &clip);

    // ── Folder list (top of content) ──────────────────────────────────────────
    for (int i = 0; i < numFolders; i++) {
      int row_y = i * FOLDER_H - scroll_y;
      if (row_y + FOLDER_H < 0 || row_y > windowY - BOTTOM_H) continue;

      if (chosen_index == i)
        SDL_DrawRect(RENDERER, 0, row_y, windowX, FOLDER_H, selectorColor);

      SDL_DrawImageScale(RENDERER, folder_image, 8, row_y + 4, 32, 32);
      SDL_DrawText(RENDERER, ROBOTO_25, 48, row_y + (FOLDER_H - 22) / 2,
                   textColor, sorted_entries[i].filename().c_str());
    }

    // ── Book / comic grid ─────────────────────────────────────────────────────
    int numBooks   = amountOfFiles - numFolders;
    int books_top  = numFolders * FOLDER_H;

    TTF_Font* lf  = pick_label_font();
    int lf_h      = TTF_FontHeight(lf);
    int strip_h   = lf_h + 8;  // 4px padding above and below text

    for (int bi = 0; bi < numBooks; bi++) {
      int row    = bi / cols;
      int col    = bi % cols;
      int cell_x = col * cell_w;
      int cell_y = books_top + row * cell_h - scroll_y;

      if (cell_y + cell_h < 0 || cell_y > windowY - BOTTOM_H) continue;

      // Cover thumbnail or fallback book icon
      SDL_Texture* cover =
          (bi < (int)cover_textures.size()) ? cover_textures[bi] : nullptr;
      int dx = cell_x, dy = cell_y, dw = cell_w, dh = cell_h;
      if (cover) {
        // Texture decoded at exact thumb_w × thumb_h — blit 1:1 (or minor scale from cache hit)
        dw = thumb_w;
        dh = thumb_h;
        dx = cell_x + (cell_w - thumb_w) / 2;
        dy = cell_y;
        SDL_DrawImageScale(RENDERER, cover, dx, dy, dw, dh);
      } else {
        SDL_DrawImageScale(RENDERER, book_image,
                           cell_x + (cell_w - 40) / 2,
                           cell_y + (thumb_h - 40) / 2, 40, 40);
      }

      // Title strip — semi-transparent dark bar overlaid on the cover bottom
      {
        string fname = sorted_entries[numFolders + bi].filename().string();
        size_t dot = fname.find_last_of('.');
        if (dot != string::npos) fname = fname.substr(0, dot);
        int limit = dw - 8;
        string trunc_key = fname + "|" + to_string(limit) + "|" + to_string(lf_h);
        auto it = trunc_cache.find(trunc_key);
        int label_w = 0;
        if (it != trunc_cache.end()) {
          fname = it->second;
          TTF_SizeText(lf, fname.c_str(), &label_w, nullptr);
        } else {
          TTF_SizeText(lf, fname.c_str(), &label_w, nullptr);
          if (label_w > limit) {
            while (!fname.empty()) {
              fname.pop_back();
              TTF_SizeText(lf, (fname + "..").c_str(), &label_w, nullptr);
              if (label_w <= limit) { fname += ".."; break; }
            }
          }
          trunc_cache[trunc_key] = fname;
        }
        int strip_y = dy + dh - strip_h;
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(RENDERER, 0, 0, 0, 170);
        SDL_Rect strip_rect = { dx, strip_y, dw, strip_h };
        SDL_RenderFillRect(RENDERER, &strip_rect);
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_NONE);
        SDL_DrawText(RENDERER, lf,
                     dx + (dw - label_w) / 2, strip_y + (strip_h - lf_h) / 2,
                     WHITE, fname.c_str());
      }

      // Selection highlight — 3px bright border drawn over cover + strip
      if (chosen_index == numFolders + bi) {
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(RENDERER, 255, 255, 255, 220);
        for (int t = 0; t < 3; t++) {
          SDL_Rect border = { cell_x + t, cell_y + t, cell_w - 2*t, cell_h - 2*t };
          SDL_RenderDrawRect(RENDERER, &border);
        }
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_NONE);
      }

      // Warning badge (top-right corner) for unsupported formats
      string entry_ext = sorted_entries[numFolders + bi].filename().extension().string();
      if (contains(warnedExtentions, entry_ext))
        SDL_DrawImageScale(RENDERER, warning, cell_x + cell_w - 28, cell_y + 4, 24, 24);

      // Note badge (top-left corner) — teal dot when a note exists
      if (chooser_note_exists(sorted_entries[numFolders + bi].string())) {
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(RENDERER, 0, 150, 136, 255);  // teal
        SDL_Rect nb = { cell_x + 4, cell_y + 4, 12, 12 };
        SDL_RenderFillRect(RENDERER, &nb);
      }
    }

    // ── Empty folder label ────────────────────────────────────────────────────
    if (amountOfFiles == 0 && ROBOTO_30) {
      const char* empty_msg = "This folder is empty";
      int ew = 0, eh = 0;
      TTF_SizeText(ROBOTO_30, empty_msg, &ew, &eh);
      SDL_DrawText(RENDERER, ROBOTO_30, (windowX - ew) / 2, (windowY - eh) / 2,
                   textColor, empty_msg);
    }

    SDL_RenderSetClipRect(RENDERER, nullptr);  // clear clip

    // ── Modal overlays ────────────────────────────────────────────────────────

    if (isWarningOnScreen) {
      if (!configDarkMode)
        SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));

      SDL_DrawRect(RENDERER, (windowX - warningWidth) / 2,
                   (windowY - warningHeight) / 2, warningWidth, warningHeight,
                   configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);
      SDL_DrawText(RENDERER, ROBOTO_30,
                   (windowX - warningWidth) / 2 + 15,
                   (windowY - warningHeight) / 2 + 15, textColor,
                   "This file is not yet fully supported, and may");
      SDL_DrawText(RENDERER, ROBOTO_30,
                   (windowX - warningWidth) / 2 + 15,
                   (windowY - warningHeight) / 2 + 50, textColor,
                   "cause a system, or app crash.");
      SDL_DrawText(RENDERER, ROBOTO_20,
                   (windowX - warningWidth) / 2 + warningWidth - 250,
                   (windowY - warningHeight) / 2 + warningHeight - 30,
                   textColor, "\"A\" - Read");
      SDL_DrawText(RENDERER, ROBOTO_20,
                   (windowX - warningWidth) / 2 + warningWidth - 125,
                   (windowY - warningHeight) / 2 + warningHeight - 30,
                   textColor, "\"B\" - Cancel.");
    }

    if (drawOption) {
      int helpWidth  = 680;
      int helpHeight = 395;

      if (!configDarkMode)
        SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));

      SDL_DrawRect(RENDERER, (windowX - helpWidth) / 2,
                   (windowY - helpHeight) / 2, helpWidth, helpHeight,
                   configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

      int optTextX = (windowX - helpWidth) / 2 + 20;
      int optTextY = (windowY - helpHeight) / 2 + 87;

      SDL_DrawRect(
          RENDERER, optTextX - 20, optTextY + (38 * option_index),
          helpWidth, 40,
          configDarkMode ? SELECTOR_COLOUR_DARK : SELECTOR_COLOUR_LIGHT);

      SDL_DrawText(RENDERER, ROBOTO_30, optTextX,
                   (windowY - helpHeight) / 2 + 10, textColor, "Option Menu");

      SDL_DrawText(RENDERER, ROBOTO_25, optTextX, optTextY, textColor,
                   "Folder Color: ");
      SDL_DrawText(RENDERER, ROBOTO_25, optTextX + 400, optTextY, textColor,
                   colors[chosenFolderColor].c_str());

      SDL_DrawText(RENDERER, ROBOTO_25, optTextX, optTextY + 38, textColor,
                   "Book Color: ");
      SDL_DrawText(RENDERER, ROBOTO_25, optTextX + 400, optTextY + 38,
                   textColor, colors[chosenBookColor].c_str());

      SDL_DrawText(RENDERER, ROBOTO_25, optTextX, optTextY + 38 * 2,
                   textColor, "Scroll Speed: ");
      SDL_DrawTextf(RENDERER, ROBOTO_25, optTextX + 400, optTextY + 38 * 2,
                    textColor, "%d", scroll_option);

      SDL_DrawText(RENDERER, ROBOTO_25, optTextX, optTextY + 38 * 3,
                   textColor, "Zoom Amount: ");
      SDL_DrawTextf(RENDERER, ROBOTO_25, optTextX + 400, optTextY + 38 * 3,
                    textColor, "%d", zoom_option);
    }

    // ── Notes overlay ─────────────────────────────────────────────────────────
    if (drawNotesChooser) {
      int noteWidth  = 800;
      int noteHeight = 500;

      if (!configDarkMode)
        SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));

      SDL_DrawRect(RENDERER, (windowX - noteWidth) / 2,
                   (windowY - noteHeight) / 2, noteWidth, noteHeight,
                   configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

      int nTextX = (windowX - noteWidth) / 2 + 20;
      int nTextY = (windowY - noteHeight) / 2 + 10;

      // Title: comic filename (no extension)
      string noteFname = notesChooserPath.substr(notesChooserPath.find_last_of("/\\") + 1);
      size_t dot = noteFname.find_last_of('.');
      if (dot != string::npos) noteFname = noteFname.substr(0, dot);
      SDL_DrawText(RENDERER, ROBOTO_25, nTextX, nTextY, textColor, noteFname.c_str());
      SDL_DrawText(RENDERER, ROBOTO_20, nTextX, nTextY + 30, textColor, "Notes:");

      // Hint at bottom
      int hintY = (windowY + noteHeight) / 2 - 35;
      if (ROBOTO_15)
        SDL_DrawText(RENDERER, ROBOTO_15, nTextX, hintY, textColor,
                     "A = Edit    B / Y = Close");

      // Note body
      int bodyY    = nTextY + 65;
      int bodyMaxH = hintY - bodyY - 10;
      if (notesChooserText.empty()) {
        if (ROBOTO_25)
          SDL_DrawText(RENDERER, ROBOTO_25, nTextX, bodyY,
                       DARK_GRAY, "No notes yet. Press A to add one.");
      } else if (ROBOTO_20) {
        SDL_DrawTextWrapped(RENDERER, ROBOTO_20, nTextX, bodyY,
                            noteWidth - 40, bodyMaxH, textColor,
                            notesChooserText.c_str());
      }
    }

    SDL_RenderPresent(RENDERER);
  }

  // Stop all background cover loader threads
  for (int w = 0; w < N_WORKERS; w++) {
    __atomic_store_n(&cl[w].should_exit, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&cl[w].job_ready, 0, __ATOMIC_RELEASE);  // cancel any queued job
  }
  for (int w = 0; w < N_WORKERS; w++) {
    threadWaitForExit(&cl[w].thread);
    threadClose(&cl[w].thread);
    if (cl[w].result_pixels) { free(cl[w].result_pixels); cl[w].result_pixels = nullptr; }
  }

  // Cleanup — cache owns all textures; clear it here before SDL shuts down
  cover_textures.clear();
  g_cover_cache.clear();

  if (optionConfig) {
    config_destroy(optionConfig);
    free(optionConfig);
    optionConfig = nullptr;
  }
}
