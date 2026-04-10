#include "CBZPageLayout.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <SDL2/SDL_image.h>

// stb_image: used for JPEG decoding because devkitPro libjpeg lacks progressive JPEG support
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NEON   // ARM NEON SIMD for faster JPEG IDCT + YCbCr conversion on Cortex-A57
#include "stb_image.h"

#include <turbojpeg.h>

extern "C"
{
#include <switch/kernel/mutex.h>
#include "common.h"
#include "config.h"
#include "SDL_helper.h"
}

extern void Log_Write(const std::string &msg);
extern void Log_Error(const std::string &msg);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_image_ext(const char *filename);

// Scan a RAR3 archive's file headers WITHOUT decompression.
// Jumps between blocks using PACK_SIZE fseek — works for solid and non-solid RAR3.
// Populates out_names with ALL image entry names. Returns true if RAR3 format detected.
static bool enumerate_rar3_headers(const char *path, std::vector<std::string> &out_names,
                                    volatile int *out_count = nullptr)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t magic[7];
    if (fread(magic, 1, 7, f) != 7 ||
        memcmp(magic, "\x52\x61\x72\x21\x1a\x07\x00", 7) != 0)
    {
        fclose(f);
        return false;  // not RAR3
    }

    long pos = 7;
    for (;;)
    {
        if (fseek(f, pos, SEEK_SET) != 0) break;

        uint8_t h[7];
        if (fread(h, 1, 7, f) != 7) break;

        const uint8_t  btype = h[2];
        const uint16_t flags = (uint16_t)(h[3] | (h[4] << 8));
        const uint16_t hsize = (uint16_t)(h[5] | (h[6] << 8));
        if (hsize < 7) break;
        if (btype == 0x7B) break;  // end-of-archive

        uint32_t add_size = 0;
        if (flags & 0x8000)
        {
            uint8_t s[4];
            if (fseek(f, pos + 7, SEEK_SET) != 0) break;
            if (fread(s, 1, 4, f) != 4) break;
            add_size = (uint32_t)s[0] | (uint32_t)s[1]<<8 |
                       (uint32_t)s[2]<<16 | (uint32_t)s[3]<<24;
        }

        if (btype == 0x74)  // file block
        {
            uint8_t ns[2];
            if (fseek(f, pos + 26, SEEK_SET) == 0 && fread(ns, 1, 2, f) == 2)
            {
                const uint16_t name_size = (uint16_t)(ns[0] | (ns[1] << 8));
                if (name_size > 0 && name_size < 512)
                {
                    const long name_off = pos + 32 + ((flags & 0x0100) ? 8 : 0);
                    char namebuf[513];
                    if (fseek(f, name_off, SEEK_SET) == 0 &&
                        (size_t)fread(namebuf, 1, name_size, f) == name_size)
                    {
                        namebuf[name_size] = '\0';
                        for (int i = 0; namebuf[i]; i++)
                            if (namebuf[i] == '\\') namebuf[i] = '/';
                        if (is_image_ext(namebuf))
                        {
                            out_names.push_back(namebuf);
                            if (out_count)
                                __atomic_store_n(out_count, (int)out_names.size(), __ATOMIC_RELEASE);
                        }
                    }
                }
            }
        }

        const long next = pos + (long)hsize + (long)add_size;
        if (next <= pos) break;
        pos = next;
    }
    fclose(f);
    return true;  // was RAR3 format (even if no images found)
}

static bool is_image_ext(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot)
        return false;
    char ext[16] = {};
    for (int i = 0; i < 15 && dot[i]; i++)
        ext[i] = (char)tolower((unsigned char)dot[i]);

    return strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0 ||
           strcmp(ext, ".png")  == 0 || strcmp(ext, ".webp") == 0 ||
           strcmp(ext, ".gif")  == 0 || strcmp(ext, ".tiff") == 0;
}

// ---------------------------------------------------------------------------
// Page-name disk cache
// ---------------------------------------------------------------------------
// Caches the sorted list of image filenames from an archive to disk so that
// re-opening the same comic skips the full archive scan (especially important
// for large solid RAR (CBR) archives where enumeration decompresses every page).
//
// Cache file: /switch/WookReader/.pagecache/<16-hex-digit key>.lst
// Cache key : FNV-1a hash of (path + file-size + mtime) — invalidates if the
//             file is replaced or modified.

static const char CACHE_DIR[] = "/switch/WookReader/.pagecache";

static uint64_t compute_cache_key(const char *path)
{
    struct stat st;
    uint64_t h = 14695981039346656037ULL;  // FNV-1a 64-bit offset basis
    if (stat(path, &st) == 0) {
        for (const char *p = path; *p; p++) {
            h ^= (uint8_t)*p;
            h *= 1099511628211ULL;
        }
        h ^= (uint64_t)(uint32_t)st.st_size;  h *= 1099511628211ULL;
        h ^= (uint64_t)(uint32_t)st.st_mtime; h *= 1099511628211ULL;
    }
    return h;
}

static bool load_page_cache(const char *path, std::vector<std::string> &names)
{
    uint64_t key = compute_cache_key(path);
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/%016llx.lst",
             CACHE_DIR, (unsigned long long)key);
    FILE *f = fopen(cache_path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;
        if (len > 0) names.emplace_back(line, len);
    }
    fclose(f);
    if (names.empty()) return false;
    Log_Write(std::string("page_cache: hit ") + path);
    return true;
}

static void save_page_cache(const char *path, const std::vector<std::string> &names)
{
    if (names.empty()) return;
    mkdir(CACHE_DIR, 0755);  // create dir if it doesn't exist (ignores EEXIST)
    uint64_t key = compute_cache_key(path);
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/%016llx.lst",
             CACHE_DIR, (unsigned long long)key);
    FILE *f = fopen(cache_path, "w");
    if (!f) { Log_Error(std::string("save_page_cache: cannot write ") + cache_path); return; }
    for (const auto &name : names)
        fprintf(f, "%s\n", name.c_str());
    fclose(f);
}

// ---------------------------------------------------------------------------
// LRU raw image byte cache
// ---------------------------------------------------------------------------
// Thread-safe cache of recently-extracted compressed image bytes.
// Eliminates redundant archive opens for back-navigation and for prefetch
// threads that would otherwise race to extract the same page independently.
// Capacity: 10 MB; evicts the least-recently-used entry when full.

struct RawCacheEntry {
    std::string key;          // archive_path + "|" + entry_name
    uint8_t    *data = nullptr;
    size_t      size = 0;
    uint64_t    seq  = 0;     // higher = more recently used
};

struct RawImageCache {
    Mutex                      mtx;          // zero-init = unlocked on Switch
    std::vector<RawCacheEntry> entries;
    size_t                     total_bytes = 0;
    uint64_t                   seq_counter = 0;
    static constexpr size_t    MAX_BYTES   = 10 * 1024 * 1024;  // 10 MB

    // Returns a malloc'd copy of cached data (caller must SDL_free). Returns false on miss.
    bool get(const std::string &key, void **out_data, size_t *out_size)
    {
        mutexLock(&mtx);
        for (auto &e : entries) {
            if (e.key == key) {
                void *copy = SDL_malloc(e.size);
                if (copy) {
                    memcpy(copy, e.data, e.size);
                    *out_data = copy;
                    *out_size = e.size;
                    e.seq = ++seq_counter;
                }
                mutexUnlock(&mtx);
                return copy != nullptr;
            }
        }
        mutexUnlock(&mtx);
        return false;
    }

    void put(const std::string &key, const void *data, size_t size)
    {
        if (size == 0 || size > MAX_BYTES) return;
        mutexLock(&mtx);
        // If already cached, just refresh the LRU sequence
        for (auto &e : entries) {
            if (e.key == key) { e.seq = ++seq_counter; mutexUnlock(&mtx); return; }
        }
        // Evict LRU entries until we have room
        while (total_bytes + size > MAX_BYTES && !entries.empty()) {
            auto oldest = entries.begin();
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->seq < oldest->seq) oldest = it;
            total_bytes -= oldest->size;
            SDL_free(oldest->data);
            entries.erase(oldest);
        }
        // Insert new entry
        RawCacheEntry e;
        e.key  = key;
        e.data = (uint8_t *)SDL_malloc(size);
        if (e.data) {
            memcpy(e.data, data, size);
            e.size = size;
            e.seq  = ++seq_counter;
            total_bytes += size;
            entries.push_back(std::move(e));
        }
        mutexUnlock(&mtx);
    }
};

static RawImageCache g_raw_cache;

// ---------------------------------------------------------------------------
// Open archive and enumerate image files into out_names (unsorted).
// Optional (first_name / first_data / first_size): when all three are non-null,
// the raw bytes of the FIRST image entry found are returned via SDL_malloc'd
// buffer (caller must SDL_free). This allows the caller to display the cover
// immediately while the rest of the scan continues in the background.
// Returns true on success.
static bool enumerate_images(const char *path, std::vector<std::string> &out_names,
                              std::string *first_name = nullptr,
                              void       **first_data = nullptr,
                              size_t      *first_size = nullptr,
                              volatile int *out_count = nullptr)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, path, 4194304) != ARCHIVE_OK)
    {
        Log_Error(std::string("enumerate_images: archive_read_open_filename failed: ") +
                  archive_error_string(a) + " path=" + path);
        archive_read_free(a);
        return false;
    }

    bool want_first     = (first_name && first_data && first_size);
    bool first_extracted = false;

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        if (archive_entry_filetype(entry) == AE_IFDIR)
        {
            archive_read_data_skip(a);
            continue;
        }

        const char *name = archive_entry_pathname(entry);
        if (name && is_image_ext(name))
        {
            out_names.push_back(name);
            if (out_count)
                __atomic_store_n(out_count, (int)out_names.size(), __ATOMIC_RELEASE);

            if (want_first && !first_extracted)
            {
                // Read this entry's raw bytes (no extra decompression cost for
                // solid RAR — we'd decompress it anyway during the skip).
                la_int64_t entry_size = archive_entry_size(entry);
                if (entry_size > 0)
                {
                    *first_data = SDL_malloc((size_t)entry_size);
                    if (*first_data)
                    {
                        uint8_t *dst      = (uint8_t *)*first_data;
                        size_t  remaining = (size_t)entry_size;
                        while (remaining > 0)
                        {
                            la_ssize_t n = archive_read_data(a, dst, remaining);
                            if (n <= 0) break;
                            dst       += n;
                            remaining -= (size_t)n;
                        }
                        *first_size = (size_t)entry_size - remaining;
                        *first_name = name;
                        first_extracted = true;
                        continue;  // data consumed; skip the data_skip below
                    }
                }
            }
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);
    return true;
}

// Open archive and extract image data for the entry whose pathname == target_name.
// Returns heap-allocated buffer in out_data / out_size, caller must SDL_free.
// Optional cancel_flag: if set to 1 by another thread, extraction aborts early.
static bool extract_image(const char *path, const std::string &target_name,
                          void **out_data, size_t *out_size,
                          volatile int *cancel_flag = nullptr)
{
    *out_data = nullptr;
    *out_size = 0;

    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, path, 1048576) != ARCHIVE_OK)
    {
        Log_Error(std::string("extract_image: open failed: ") + archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    bool found = false;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        // Check cancel flag each iteration — allows fast abort when scanning solid archives
        if (cancel_flag && __atomic_load_n(cancel_flag, __ATOMIC_ACQUIRE))
        {
            archive_read_free(a);
            return false;
        }

        if (archive_entry_filetype(entry) == AE_IFDIR)
        {
            archive_read_data_skip(a);
            continue;
        }

        const char *name = archive_entry_pathname(entry);

        if (name && target_name == name)
        {
            la_int64_t entry_size = archive_entry_size(entry);

            if (entry_size > 0)
            {
                // Known size: allocate once, read directly — no intermediate copies
                *out_size = (size_t)entry_size;
                *out_data = SDL_malloc(*out_size);
                if (*out_data)
                {
                    uint8_t *dst = (uint8_t *)*out_data;
                    size_t remaining = *out_size;
                    while (remaining > 0)
                    {
                        if (cancel_flag && __atomic_load_n(cancel_flag, __ATOMIC_ACQUIRE))
                        {
                            SDL_free(*out_data); *out_data = nullptr; *out_size = 0;
                            archive_read_free(a);
                            return false;
                        }
                        la_ssize_t n = archive_read_data(a, dst, remaining);
                        if (n <= 0) break;
                        dst += n;
                        remaining -= (size_t)n;
                    }
                    *out_size -= remaining;
                }
            }
            else
            {
                // Unknown size (some TAR/streaming formats): chunked fallback
                std::vector<uint8_t> buf;
                buf.reserve(8 * 1024 * 1024);  // pre-allocate 8 MB to avoid O(N log N) reallocations
                uint8_t chunk[65536];
                la_ssize_t n;
                while ((n = archive_read_data(a, chunk, sizeof(chunk))) > 0)
                {
                    if (cancel_flag && __atomic_load_n(cancel_flag, __ATOMIC_ACQUIRE))
                    {
                        archive_read_free(a);
                        return false;
                    }
                    buf.insert(buf.end(), chunk, chunk + n);
                }
                if (!buf.empty())
                {
                    *out_size = buf.size();
                    *out_data = SDL_malloc(*out_size);
                    if (*out_data) memcpy(*out_data, buf.data(), *out_size);
                }
            }

            if (!*out_data || *out_size == 0)
                Log_Error("extract_image: got 0 bytes for " + target_name);
            found = true;
            break;
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);

    if (!found && !(cancel_flag && __atomic_load_n(cancel_flag, __ATOMIC_ACQUIRE)))
        Log_Error("extract_image: entry not found: " + target_name + " in " + path);

    return found && *out_data != nullptr;
}

// Wrapper that checks/populates g_raw_cache before calling extract_image.
// Eliminates archive re-opens for recently-visited pages (back navigation,
// prefetch thread races, etc.). Caller must SDL_free the returned data.
static bool extract_image_cached(const char *archive_path,
                                 const std::string &target_name,
                                 void **out_data, size_t *out_size,
                                 volatile int *cancel_flag = nullptr)
{
    std::string key = std::string(archive_path) + "|" + target_name;
    if (g_raw_cache.get(key, out_data, out_size))
        return true;

    if (!extract_image(archive_path, target_name, out_data, out_size, cancel_flag))
        return false;

    if (*out_data && *out_size)
        g_raw_cache.put(key, *out_data, *out_size);

    return true;
}

// Decode JPEG using libjpeg-turbo (2-3x faster than stb_image, ARM NEON SIMD).
// Returns heap-allocated RGBA pixels (caller must free()), or nullptr on failure.
static unsigned char *decode_jpeg_turbo(const void *data, size_t dsize, int *out_w, int *out_h)
{
    tjhandle tj = tjInitDecompress();
    if (!tj) return nullptr;

    int w = 0, h = 0, subsamp = 0, cs = 0;
    if (tjDecompressHeader3(tj, (const unsigned char *)data, (unsigned long)dsize,
                            &w, &h, &subsamp, &cs) != 0)
    {
        Log_Error(std::string("decode_jpeg_turbo: header failed: ") + tjGetErrorStr2(tj));
        tjDestroy(tj);
        return nullptr;
    }

    unsigned char *pixels = (unsigned char *)malloc(w * h * 4);
    if (!pixels) { tjDestroy(tj); return nullptr; }

    if (tjDecompress2(tj, (const unsigned char *)data, (unsigned long)dsize,
                      pixels, w, 0, h, TJPF_RGBA, TJFLAG_FASTDCT) != 0)
    {
        Log_Error(std::string("decode_jpeg_turbo: decompress failed: ") + tjGetErrorStr2(tj));
        free(pixels);
        tjDestroy(tj);
        return nullptr;
    }

    tjDestroy(tj);
    *out_w = w;
    *out_h = h;
    return pixels;
}

// Decode image data to an SDL_Surface.
// For JPEG (FF D8): uses libjpeg-turbo; sets *out_pixels (caller must free()).
// For other formats: uses SDL2_image; *out_pixels is set to nullptr.
// Returns nullptr on failure.
static SDL_Surface *decode_to_surface(void *data, size_t dsize, unsigned char **out_pixels)
{
    *out_pixels = nullptr;

    bool is_jpeg = (dsize >= 2 &&
                    ((const uint8_t *)data)[0] == 0xFF &&
                    ((const uint8_t *)data)[1] == 0xD8);

    if (is_jpeg)
    {
        int w = 0, h = 0;
        unsigned char *pixels = decode_jpeg_turbo(data, dsize, &w, &h);
        if (!pixels)
        {
            // Fallback: stb_image handles progressive JPEG that turbo may reject
            int ch = 0;
            pixels = stbi_load_from_memory(
                (const stbi_uc *)data, (int)dsize, &w, &h, &ch, 4);
            if (!pixels) return nullptr;
        }

        // Masks for RGBA byte-order on little-endian (AArch64)
        SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
            pixels, w, h, 32, w * 4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
        if (!s)
        {
            Log_Error(std::string("decode_to_surface: SDL_CreateRGBSurfaceFrom failed: ") +
                      SDL_GetError());
            free(pixels);
            return nullptr;
        }
        *out_pixels = pixels;
        return s;
    }

    // Non-JPEG: use SDL2_image
    SDL_RWops *rw = SDL_RWFromMem(data, (int)dsize);
    if (!rw) return nullptr;
    SDL_Surface *s = IMG_Load_RW(rw, 1);
    if (!s)
        Log_Error(std::string("decode_to_surface: IMG_Load_RW failed: ") + IMG_GetError());
    return s;
}

// ---------------------------------------------------------------------------
// Early first-page display
// ---------------------------------------------------------------------------

// Decode _first_image_raw → SDL_Texture and set _valid = true so BookReader::draw()
// can render the cover immediately while the background enumeration scan continues.
// Called from the main thread once is_first_image_ready() returns true.
void CBZPageLayout::apply_first_image()
{
    if (!_first_image_raw || !_first_image_size) return;

    unsigned char *px   = nullptr;
    SDL_Surface   *surf = decode_to_surface(_first_image_raw, _first_image_size, &px);
    SDL_free(_first_image_raw);
    _first_image_raw  = nullptr;
    _first_image_size = 0;
    if (!surf) return;

    SDL_RenderGetViewport(RENDERER, &_viewport);
    _tex_w   = surf->w;
    _tex_h   = surf->h;
    _tex_r_w = 0;
    _tex     = SDL_CreateTextureFromSurface(RENDERER, surf);
    SDL_FreeSurface(surf);
    if (px) free(px);
    if (!_tex) return;

    // Zoom / centre — same logic as load_page_texture with reset_zoom = true.
    float ew    = (float)_tex_w;
    float eh    = (float)_tex_h;
    _min_zoom   = std::min((float)_viewport.w / ew, (float)_viewport.h / eh);
    _max_zoom   = std::max((float)_viewport.w / ew, (float)_viewport.h / eh) * 2.0f;
    _max_zoom   = std::max(_max_zoom, _min_zoom * 4.0f);
    _zoom       = _min_zoom;
    _cx         = _viewport.w / 2.0f;
    _cy         = eh * _zoom / 2.0f;
    _current_page = 0;
    _page_count   = 1;   // placeholder until finish_enumeration() knows the real count
    _valid        = true;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

// Background enumeration thread entry
void CBZPageLayout::enum_thread_entry(void *arg)
{
    static_cast<CBZPageLayout*>(arg)->do_enumerate();
}

void CBZPageLayout::do_enumerate()
{
    // Fast path: page names already cached on disk from a previous open.
    // Cache stores names in sorted order, so no sort needed.
    if (load_page_cache(_archive_path.c_str(), _page_names)) {
        __atomic_store_n(&_enum_count, (int)_page_names.size(), __ATOMIC_RELEASE);
        // Pre-extract page 0's raw bytes so the main thread can display the
        // cover immediately while we signal enum_done.
        if (!_page_names.empty()) {
            void *fd = nullptr; size_t fs = 0;
            if (extract_image_cached(_archive_path.c_str(), _page_names[0], &fd, &fs)) {
                _first_image_raw  = fd;
                _first_image_size = fs;
                _first_image_name = _page_names[0];
                __atomic_store_n(&_first_image_ready, 1, __ATOMIC_RELEASE);
            }
        }
        __atomic_store_n(&_enum_done, 1, __ATOMIC_RELEASE);
        return;
    }
    // RAR3 fast path: read file headers directly via fseek — zero decompression.
    // Completes in ~50ms even for 500MB solid-RAR archives.
    if (enumerate_rar3_headers(_archive_path.c_str(), _page_names, &_enum_count))
    {
        std::sort(_page_names.begin(), _page_names.end());
        __atomic_store_n(&_enum_count, (int)_page_names.size(), __ATOMIC_RELEASE);
        // Pre-extract cover so it displays immediately
        if (!_page_names.empty()) {
            void *fd = nullptr; size_t fs = 0;
            if (extract_image_cached(_archive_path.c_str(), _page_names[0], &fd, &fs)) {
                _first_image_raw  = fd;
                _first_image_size = fs;
                _first_image_name = _page_names[0];
                __atomic_store_n(&_first_image_ready, 1, __ATOMIC_RELEASE);
            }
        }
        save_page_cache(_archive_path.c_str(), _page_names);
        __atomic_store_n(&_enum_done, 1, __ATOMIC_RELEASE);
        return;
    }

    // Slow path: scan the archive via libarchive (needed for CBZ/CBT/CB7/RAR5).
    // Pass first-image output params so we can display the cover before the
    // scan finishes — no extra decompression cost; we save what we'd skip anyway.
    void *fd = nullptr; size_t fs = 0;
    enumerate_images(_archive_path.c_str(), _page_names,
                     &_first_image_name, &fd, &fs,
                     &_enum_count);
    if (fd) {
        _first_image_raw  = fd;
        _first_image_size = fs;
        __atomic_store_n(&_first_image_ready, 1, __ATOMIC_RELEASE);
    }
    std::sort(_page_names.begin(), _page_names.end());
    // Persist names for instant reopen next time.
    save_page_cache(_archive_path.c_str(), _page_names);
    __atomic_store_n(&_enum_done, 1, __ATOMIC_RELEASE);
}

void CBZPageLayout::finish_enumeration()
{
    if (!_enumerating) return;

    // Wait for the enumeration thread to finish (non-blocking: caller checks is_enumerating() first)
    threadWaitForExit(&_enum_thread);
    threadClose(&_enum_thread);
    _enumerating = false;

    if (_page_names.empty())
    {
        Log_Error(std::string("CBZPageLayout: no image files found in archive: ") + _archive_path);
        return;  // _valid stays false
    }

    _page_count = (int)_page_names.size();
    Log_Write("CBZPageLayout: opened OK, pages=" + std::to_string(_page_count) + " path=" + _archive_path);

    // If user navigated during enumeration, find their page in the now-sorted list.
    if (!_enum_current_name.empty())
    {
        auto it = std::lower_bound(_page_names.begin(), _page_names.end(), _enum_current_name);
        if (it != _page_names.end() && *it == _enum_current_name)
            _current_page = (int)(it - _page_names.begin());
        else
            _current_page = 0;  // fallback: page name disappeared (shouldn't happen)
        _enum_current_name.clear();
        // Restart prefetch with corrected indices
        int fwd  = (_current_page + 1 < _page_count) ? _current_page + 1 : -1;
        int bwd  = (_current_page - 1 >= 0)           ? _current_page - 1 : -1;
        int fwd2 = (_current_page + 2 < _page_count)  ? _current_page + 2 : -1;
        start_prefetch(fwd, bwd, fwd2);
    }
    else
    {
        SDL_RenderGetViewport(RENDERER, &_viewport);

        // If we already displayed the first image early, avoid reloading it.
        if (_valid && _start_page == 0 &&
            !_first_image_name.empty() && !_page_names.empty() &&
            _first_image_name == _page_names[0])
        {
            // Correct page already on screen — just unlock navigation.
            int fwd  = _page_count > 1 ? 1 : -1;
            int fwd2 = _page_count > 2 ? 2 : -1;
            start_prefetch(fwd, -1, fwd2);
        }
        else
        {
            FreeTextureIfNeeded(&_tex);
            FreeTextureIfNeeded(&_tex_r);
            int clamped = std::max(0, std::min(_start_page, _page_count - 1));
            load_page_texture(clamped, true);
            _valid = true;
        }
    }
}

CBZPageLayout::CBZPageLayout(const char *path, int start_page)
    : PageLayout(nullptr, 0)
{
    _archive_path = path;
    _start_page   = start_page;
    _page_names.reserve(8192);  // prevent reallocation during progressive nav

    SDL_RenderGetViewport(RENDERER, &_viewport);

    // Start enumeration on core 2 (core 3 is system-reserved on some Switch firmwares)
    if (threadCreate(&_enum_thread, enum_thread_entry, this, nullptr, 0x40000, 0x30, 2) == 0 &&
        threadStart(&_enum_thread) == 0)
    {
        _enumerating = true;
        // _valid stays false until finish_enumeration() is called from BookReader::draw()
        return;
    }

    // Fallback: synchronous enumeration if thread creation fails
    if (!enumerate_images(path, _page_names))
        return;

    if (_page_names.empty())
    {
        Log_Error(std::string("CBZPageLayout: no image files found in archive: ") + path);
        return;
    }

    std::sort(_page_names.begin(), _page_names.end());
    _page_count = (int)_page_names.size();

    Log_Write("CBZPageLayout: opened OK (sync), pages=" + std::to_string(_page_count) + " path=" + path);

    int clamped = std::max(0, std::min(start_page, _page_count - 1));
    load_page_texture(clamped, true);
    _valid = true;
}

CBZPageLayout::~CBZPageLayout()
{
    // If enumeration thread is still running, wait for it before destruction
    if (_enumerating)
    {
        threadWaitForExit(&_enum_thread);
        threadClose(&_enum_thread);
        _enumerating = false;
    }
    if (_first_image_raw) { SDL_free(_first_image_raw); _first_image_raw = nullptr; }
    cancel_prefetch();
    free_ready_textures();
    FreeTextureIfNeeded(&_tex);
    FreeTextureIfNeeded(&_tex_r);
}

// ---------------------------------------------------------------------------
// Internal rendering
// ---------------------------------------------------------------------------

// Helper: decode one archive page into an SDL_Texture, freeing all intermediate
// allocations immediately so we never hold more than one decoded image at a time.
static SDL_Texture *load_one_page_texture(const char *archive_path,
                                          const std::string &name,
                                          int *out_w, int *out_h)
{
    void *data = nullptr;
    size_t dsize = 0;
    if (!extract_image_cached(archive_path, name, &data, &dsize))
        return nullptr;

    unsigned char *pixels = nullptr;
    SDL_Surface *surf = decode_to_surface(data, dsize, &pixels);
    SDL_free(data);   // free compressed data immediately

    if (!surf)
        return nullptr;

    *out_w = surf->w;
    *out_h = surf->h;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(RENDERER, surf);
    SDL_FreeSurface(surf);
    if (pixels) free(pixels);  // free decoded pixel buffer

    if (!tex)
        Log_Error(std::string("load_one_page_texture: SDL_CreateTextureFromSurface failed: ") +
                  SDL_GetError());
    return tex;
}

void CBZPageLayout::load_page_texture(int page_num, bool reset_zoom)
{
    FreeTextureIfNeeded(&_tex);
    FreeTextureIfNeeded(&_tex_r);

    {
        int limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
        if (limit <= 0) return;
        _current_page = std::max(0, std::min(page_num, limit - 1));
    }

    int effective_count = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
    bool do_spread = (_spread_mode && _current_page + 1 < effective_count);

    int w = 0, h = 0;

    // Check all prefetch slots for a match
    {
        int match = -1;
        if (!do_spread)
        {
            for (int i = 0; i < N_PF; i++)
            {
                if (_pf_active[i] && _pf_page[i] == _current_page)
                {
                    match = i;
                    break;
                }
            }
        }

        if (match >= 0)
        {
            // Signal all OTHER slots to cancel immediately (non-blocking).
            // Cleanup is deferred to start_prefetch which runs after we display
            // the page — by then the losers have had extra time to abort.
            for (int i = 0; i < N_PF; i++)
                if (i != match && _pf_active[i])
                    __atomic_store_n(&_pf_cancel[i], 1, __ATOMIC_RELEASE);

            // Wait for the winning slot — instant if it already finished.
            threadWaitForExit(&_pf_thread[match]);
            threadClose(&_pf_thread[match]);
            _pf_active[match] = false;

            if (_pf_ok[match] && _pf_pixels[match])
            {
                SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
                    _pf_pixels[match], _pf_w[match], _pf_h[match], 32,
                    _pf_w[match] * 4,
                    0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
                if (s)
                {
                    w = _pf_w[match];
                    h = _pf_h[match];
                    _tex = SDL_CreateTextureFromSurface(RENDERER, s);
                    SDL_FreeSurface(s);
                }
            }
            if (_pf_pixels[match]) { free(_pf_pixels[match]); _pf_pixels[match] = nullptr; }
            // Loser slots still flagged _pf_active — start_prefetch will wait and clean them up.
        }
        else
        {
            // No hit: signal all slots to cancel now so they abort as fast as possible.
            // start_prefetch (called below) will wait for them to exit and clean up.
            for (int i = 0; i < N_PF; i++)
                if (_pf_active[i])
                    __atomic_store_n(&_pf_cancel[i], 1, __ATOMIC_RELEASE);
        }
    }

    if (!_tex)
    {
        _tex = load_one_page_texture(_archive_path.c_str(), _page_names[_current_page], &w, &h);
    }

    if (!_tex)
    {
        Log_Error("CBZPageLayout: failed to load page " + std::to_string(_current_page));
        return;
    }

    if (do_spread)
    {
        int rw = 0, rh = 0;
        _tex_r = load_one_page_texture(_archive_path.c_str(),
                                       _page_names[_current_page + 1], &rw, &rh);
        if (_tex_r)
        {
            _tex_r_w = rw;
            _tex_w   = w + rw;
            _tex_h   = std::max(h, rh);
        }
        else
        {
            Log_Error("CBZPageLayout: right page failed, showing single");
            _tex_r_w = 0;
            _tex_w   = w;
            _tex_h   = h;
        }
    }
    else
    {
        _tex_r_w = 0;
        _tex_w   = w;
        _tex_h   = h;
    }

    if (reset_zoom)
    {
        float eff_w = (_rotation == 90) ? (float)_tex_h : (float)_tex_w;
        float eff_h = (_rotation == 90) ? (float)_tex_w : (float)_tex_h;
        _min_zoom = std::min((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h);
        _max_zoom = std::max((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h) * 2.0f;
        _max_zoom = std::max(_max_zoom, _min_zoom * 4.0f);
        _zoom = _min_zoom;
        _cx   = _viewport.w / 2.0f;
        _cy   = eff_h * _zoom / 2.0f;
    }

    // Start async prefetch: N+1 (slot 0/core 1), N-1 (slot 1/core 2), N+2 (slot 2/core 3)
    if (!do_spread)
    {
        int pf_limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
        int fwd  = (_current_page + 1 < pf_limit) ? _current_page + 1 : -1;
        int bwd  = (_current_page - 1 >= 0)        ? _current_page - 1 : -1;
        int fwd2 = (_current_page + 2 < pf_limit)  ? _current_page + 2 : -1;
        start_prefetch(fwd, bwd, fwd2);
    }
}

// ---------------------------------------------------------------------------
// Async prefetch (two parallel threads on cores 1 and 2)
// ---------------------------------------------------------------------------

void CBZPageLayout::prefetch_entry_0(void *arg) { ((CBZPageLayout *)arg)->do_prefetch(0); }
void CBZPageLayout::prefetch_entry_1(void *arg) { ((CBZPageLayout *)arg)->do_prefetch(1); }
void CBZPageLayout::prefetch_entry_2(void *arg) { ((CBZPageLayout *)arg)->do_prefetch(2); }

void CBZPageLayout::do_prefetch(int slot)
{
    int page = _pf_page[slot];
    if (page < 0) return;

    if (__atomic_load_n(&_pf_cancel[slot], __ATOMIC_ACQUIRE))
        return;

    void *data = nullptr;
    size_t dsize = 0;
    if (!extract_image_cached(_archive_path.c_str(),
                              _pf_page_name[slot], &data, &dsize,
                              &_pf_cancel[slot]))
        return;

    if (__atomic_load_n(&_pf_cancel[slot], __ATOMIC_ACQUIRE))
    {
        SDL_free(data);
        return;
    }

    bool is_jpeg = (dsize >= 2 &&
                    ((const uint8_t *)data)[0] == 0xFF &&
                    ((const uint8_t *)data)[1] == 0xD8);

    unsigned char *pixels = nullptr;
    int w = 0, h = 0;

    if (is_jpeg)
    {
        pixels = decode_jpeg_turbo(data, dsize, &w, &h);
        if (!pixels)
        {
            int ch = 0;
            pixels = stbi_load_from_memory(
                (const stbi_uc *)data, (int)dsize, &w, &h, &ch, 4);
        }
    }
    else
    {
        int ch = 0;
        pixels = stbi_load_from_memory(
            (const stbi_uc *)data, (int)dsize, &w, &h, &ch, 4);
    }

    SDL_free(data);

    if (pixels)
    {
        _pf_pixels[slot] = pixels;
        _pf_w[slot]      = w;
        _pf_h[slot]      = h;
        __atomic_store_n(&_pf_ok[slot], true, __ATOMIC_RELEASE);
    }
}

void CBZPageLayout::cancel_one_slot(int i)
{
    if (!_pf_active[i]) return;
    __atomic_store_n(&_pf_cancel[i], 1, __ATOMIC_RELEASE);
    threadWaitForExit(&_pf_thread[i]);
    threadClose(&_pf_thread[i]);
    _pf_active[i] = false;
    if (_pf_pixels[i]) { free(_pf_pixels[i]); _pf_pixels[i] = nullptr; }
    _pf_page[i] = -1;
}

void CBZPageLayout::start_prefetch(int fwd_page, int bwd_page, int fwd2_page)
{
    typedef void (*ThreadFunc)(void *);
    static const ThreadFunc entries[N_PF] = {
        prefetch_entry_0, prefetch_entry_1, prefetch_entry_2
    };
    // Cores: slot 0→core 1, slot 1→core 2, slot 2→core 3
    static const int cores[N_PF] = {1, 2, 2};

    // Phase 1: signal all still-active slots to cancel (non-blocking).
    // Some may already be signaled from the hit/miss path above — that's fine.
    for (int i = 0; i < N_PF; i++)
        if (_pf_active[i])
            __atomic_store_n(&_pf_cancel[i], 1, __ATOMIC_RELEASE);

    // Phase 2: wait for all active slots to exit, then clean up.
    // By now the cancel signal has been set for the duration of texture upload
    // + zoom recalc, so most threads will have already exited.
    for (int i = 0; i < N_PF; i++)
    {
        if (!_pf_active[i]) continue;
        threadWaitForExit(&_pf_thread[i]);
        threadClose(&_pf_thread[i]);
        _pf_active[i] = false;
        if (_pf_pixels[i]) { free(_pf_pixels[i]); _pf_pixels[i] = nullptr; }
        _pf_ok[i]   = false;
        _pf_page[i] = -1;
    }

    // Phase 3: launch new prefetch threads.
    int pages[N_PF] = {fwd_page, bwd_page, fwd2_page};

    for (int i = 0; i < N_PF; i++)
    {
        _pf_page[i]   = pages[i];
        _pf_page_name[i] = (pages[i] >= 0) ? _page_names[pages[i]] : "";
        _pf_pixels[i] = nullptr;
        _pf_w[i]      = 0;
        _pf_h[i]      = 0;
        _pf_ok[i]     = false;
        __atomic_store_n(&_pf_cancel[i], 0, __ATOMIC_RELEASE);

        if (_pf_page[i] < 0) continue;
        // Skip slot 2: only 2 prefetch threads to avoid SD card I/O contention
        if (i == 2) continue;

        Result rc = threadCreate(&_pf_thread[i], entries[i], this,
                                 NULL, 0x80000, 0x30, cores[i]);
        if (R_SUCCEEDED(rc))
        {
            rc = threadStart(&_pf_thread[i]);
            if (R_SUCCEEDED(rc))
                _pf_active[i] = true;
            else
            {
                Log_Error("start_prefetch: threadStart failed slot=" + std::to_string(i));
                threadClose(&_pf_thread[i]);
            }
        }
        else
        {
            Log_Error("start_prefetch: threadCreate failed slot=" + std::to_string(i));
        }
    }
}

void CBZPageLayout::cancel_prefetch()
{
    // Signal all first (non-blocking), then wait for all.
    for (int i = 0; i < N_PF; i++)
        if (_pf_active[i])
            __atomic_store_n(&_pf_cancel[i], 1, __ATOMIC_RELEASE);
    for (int i = 0; i < N_PF; i++)
        cancel_one_slot(i);
    free_ready_textures();
}

void CBZPageLayout::clamp_center()
{
    float vw = ((_rotation == 90 && !_tex_r) ? (float)_tex_h : (float)_tex_w) * _zoom;
    float vh = ((_rotation == 90 && !_tex_r) ? (float)_tex_w : (float)_tex_h) * _zoom;
    {
        float cx_lo = std::min(vw / 2.0f, _viewport.w - vw / 2.0f);
        float cx_hi = std::max(vw / 2.0f, _viewport.w - vw / 2.0f);
        _cx = std::fmin(std::fmax(_cx, cx_lo), cx_hi);
    }
    _cy = std::fmin(std::fmax(_cy, _viewport.h - vh / 2.0f), vh / 2.0f);
}

void CBZPageLayout::apply_zoom(float v)
{
    v = std::fmin(std::fmax(v, _min_zoom), _max_zoom);
    if (v == _zoom)
        return;
    _zoom = v;
    clamp_center();
}

void CBZPageLayout::free_ready_textures()
{
    FreeTextureIfNeeded(&_ready_next);
    FreeTextureIfNeeded(&_ready_prev);
    _ready_next_pg = -1;
    _ready_prev_pg = -1;
    _ready_next_w  = 0;
    _ready_next_h  = 0;
    _ready_prev_w  = 0;
    _ready_prev_h  = 0;
}

void CBZPageLayout::poll_prefetch()
{
    // Skip in spread mode — prefetch doesn't handle page pairs
    if (_spread_mode) return;

    for (int i = 0; i < N_PF; i++)
    {
        if (!_pf_active[i]) continue;
        if (!__atomic_load_n(&_pf_ok[i], __ATOMIC_ACQUIRE)) continue;   // decode still running

        // Decode finished — thread is about to exit or already exited.
        threadWaitForExit(&_pf_thread[i]);   // instant (thread done)
        threadClose(&_pf_thread[i]);
        _pf_active[i] = false;

        int pg = _pf_page[i];
        unsigned char *pixels = _pf_pixels[i];
        int pw = _pf_w[i];
        int ph = _pf_h[i];
        _pf_pixels[i] = nullptr;
        _pf_page[i]   = -1;
        _pf_ok[i]     = false;

        if (!pixels) continue;

        // Determine which ready slot this page belongs to
        bool is_next = (pg == _current_page + 1);
        bool is_prev = (pg == _current_page - 1);

        // Skip if it doesn't match N+1 or N-1, or if we already have it
        if (is_next && _ready_next_pg == pg) { free(pixels); continue; }
        if (is_prev && _ready_prev_pg == pg) { free(pixels); continue; }
        if (!is_next && !is_prev)            { free(pixels); continue; }

        // Upload to GPU texture
        SDL_Surface *s = SDL_CreateRGBSurfaceFrom(
            pixels, pw, ph, 32, pw * 4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

        SDL_Texture *tex = nullptr;
        if (s)
        {
            tex = SDL_CreateTextureFromSurface(RENDERER, s);
            SDL_FreeSurface(s);
        }
        free(pixels);

        if (!tex) continue;

        if (is_next)
        {
            FreeTextureIfNeeded(&_ready_next);
            _ready_next    = tex;
            _ready_next_pg = pg;
            _ready_next_w  = pw;
            _ready_next_h  = ph;
        }
        else
        {
            FreeTextureIfNeeded(&_ready_prev);
            _ready_prev    = tex;
            _ready_prev_pg = pg;
            _ready_prev_w  = pw;
            _ready_prev_h  = ph;
        }
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void CBZPageLayout::previous_page(int n)
{
    if (!_valid) return;
    int step = _spread_mode ? 2 * n : n;
    int new_page = _current_page - step;
    if (new_page < 0)
        new_page = 0;
    if (new_page == _current_page)
        return;

    // Try instant swap from ready texture (single-page, step==1 only)
    if (!_spread_mode && step == 1 &&
        _ready_prev && _ready_prev_pg == new_page)
    {
        // Recycle old current texture as the new _ready_next (instant forward-nav)
        FreeTextureIfNeeded(&_ready_next);
        FreeTextureIfNeeded(&_tex_r);
        _ready_next    = _tex;
        _ready_next_pg = _current_page;
        _ready_next_w  = _tex_w;
        _ready_next_h  = _tex_h;

        _tex     = _ready_prev;
        _tex_w   = _ready_prev_w;
        _tex_h   = _ready_prev_h;
        _tex_r_w = 0;

        _ready_prev    = nullptr;
        _ready_prev_pg = -1;
        _ready_prev_w  = 0;
        _ready_prev_h  = 0;

        _current_page = new_page;

        // Reset zoom for the new page dimensions
        float eff_w = (_rotation == 90) ? (float)_tex_h : (float)_tex_w;
        float eff_h = (_rotation == 90) ? (float)_tex_w : (float)_tex_h;
        _min_zoom = std::min((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h);
        _max_zoom = std::max((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h) * 2.0f;
        _max_zoom = std::max(_max_zoom, _min_zoom * 4.0f);
        _zoom = _min_zoom;
        _cx   = _viewport.w / 2.0f;
        _cy   = eff_h * _zoom / 2.0f;

        // Launch new prefetch for N+1 and N-1
        int pf_limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
        int fwd  = (_current_page + 1 < pf_limit) ? _current_page + 1 : -1;
        int bwd  = (_current_page - 1 >= 0)        ? _current_page - 1 : -1;
        int fwd2 = (_current_page + 2 < pf_limit)  ? _current_page + 2 : -1;
        start_prefetch(fwd, bwd, fwd2);
    }
    else
    {
        // Fallback: blocking load
        free_ready_textures();
        load_page_texture(new_page, true);
    }

    if (_enumerating)
        _enum_current_name = _page_names[_current_page];
}

void CBZPageLayout::next_page(int n)
{
    if (!_valid) return;
    int step = _spread_mode ? 2 * n : n;
    int new_page = _current_page + step;
    int limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
    if (new_page >= limit)
        return;

    // Try instant swap from ready texture (single-page, step==1 only)
    if (!_spread_mode && step == 1 &&
        _ready_next && _ready_next_pg == new_page)
    {
        // Recycle old current texture as the new _ready_prev (instant back-nav)
        FreeTextureIfNeeded(&_ready_prev);
        FreeTextureIfNeeded(&_tex_r);
        _ready_prev    = _tex;
        _ready_prev_pg = _current_page;
        _ready_prev_w  = _tex_w;
        _ready_prev_h  = _tex_h;

        _tex     = _ready_next;
        _tex_w   = _ready_next_w;
        _tex_h   = _ready_next_h;
        _tex_r_w = 0;

        _ready_next    = nullptr;
        _ready_next_pg = -1;
        _ready_next_w  = 0;
        _ready_next_h  = 0;

        _current_page = new_page;

        // Reset zoom for the new page dimensions
        float eff_w = (_rotation == 90) ? (float)_tex_h : (float)_tex_w;
        float eff_h = (_rotation == 90) ? (float)_tex_w : (float)_tex_h;
        _min_zoom = std::min((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h);
        _max_zoom = std::max((float)_viewport.w / eff_w,
                             (float)_viewport.h / eff_h) * 2.0f;
        _max_zoom = std::max(_max_zoom, _min_zoom * 4.0f);
        _zoom = _min_zoom;
        _cx   = _viewport.w / 2.0f;
        _cy   = eff_h * _zoom / 2.0f;

        // Launch new prefetch for N+1 and N-1
        int pf_limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
        int fwd  = (_current_page + 1 < pf_limit) ? _current_page + 1 : -1;
        int bwd  = (_current_page - 1 >= 0)        ? _current_page - 1 : -1;
        int fwd2 = (_current_page + 2 < pf_limit)  ? _current_page + 2 : -1;
        start_prefetch(fwd, bwd, fwd2);
    }
    else
    {
        // Fallback: blocking load (spread mode, multi-step skip, or no ready texture)
        free_ready_textures();
        load_page_texture(new_page, true);
    }

    if (_enumerating)
        _enum_current_name = _page_names[_current_page];
}

void CBZPageLayout::goto_page(int num)
{
    if (!_valid) return;
    int limit = _enumerating ? __atomic_load_n(&_enum_count, __ATOMIC_ACQUIRE) : _page_count;
    if (limit <= 0) return;
    int target = std::min(std::max(0, num), limit - 1);
    if (target == _current_page)
        return;
    free_ready_textures();
    load_page_texture(target, true);
    if (_enumerating)
        _enum_current_name = _page_names[_current_page];
}

void CBZPageLayout::toggle_spread()
{
    // Y cycles: single(0°) → single(90°) → spread → single(0°)
    if (!_spread_mode && _rotation == 0)
    {
        _rotation = 90;
    }
    else if (!_spread_mode && _rotation == 90)
    {
        _rotation    = 0;
        _spread_mode = true;
    }
    else
    {
        _spread_mode = false;
    }
    free_ready_textures();
    load_page_texture(_current_page, true);
}

void CBZPageLayout::zoom_in(float zoom_amount)  { apply_zoom(_zoom + zoom_amount); }
void CBZPageLayout::zoom_out(float zoom_amount) { apply_zoom(_zoom - zoom_amount); }

void CBZPageLayout::zoom_at_point(float delta, float px, float py)
{
    float old_zoom = _zoom;
    float new_zoom = std::fmin(std::fmax(old_zoom + delta, _min_zoom), _max_zoom);
    if (new_zoom == old_zoom)
        return;
    float ratio = new_zoom / old_zoom;
    _cx = px + (_cx - px) * ratio;
    _cy = py + (_cy - py) * ratio;
    _zoom = new_zoom;
    clamp_center();
}

void CBZPageLayout::zoom_max()
{
    apply_zoom(_max_zoom);
    apply_zoom(_zoom - 0.6f);
    apply_zoom(_zoom + 0.6f);
}

void CBZPageLayout::move_up(int scroll_speed)
{
    float eff_h = ((_rotation == 90 && !_tex_r) ? (float)_tex_w : (float)_tex_h);
    float h = eff_h * _zoom;
    _cy = std::fmin(std::fmax(_cy + scroll_speed, _viewport.h - h / 2.0f), h / 2.0f);
}

void CBZPageLayout::move_down(int scroll_speed)
{
    float eff_h = ((_rotation == 90 && !_tex_r) ? (float)_tex_w : (float)_tex_h);
    float h = eff_h * _zoom;
    _cy = std::fmin(std::fmax(_cy - scroll_speed, _viewport.h - h / 2.0f), h / 2.0f);
}

void CBZPageLayout::move_left(int scroll_speed)
{
    float eff_w = ((_rotation == 90 && !_tex_r) ? (float)_tex_h : (float)_tex_w);
    float w  = eff_w * _zoom;
    float lo = std::min(w / 2.0f, _viewport.w - w / 2.0f);
    float hi = std::max(w / 2.0f, _viewport.w - w / 2.0f);
    _cx = std::fmin(std::fmax(_cx - scroll_speed, lo), hi);
}

void CBZPageLayout::move_right(int scroll_speed)
{
    float eff_w = ((_rotation == 90 && !_tex_r) ? (float)_tex_h : (float)_tex_w);
    float w  = eff_w * _zoom;
    float lo = std::min(w / 2.0f, _viewport.w - w / 2.0f);
    float hi = std::max(w / 2.0f, _viewport.w - w / 2.0f);
    _cx = std::fmin(std::fmax(_cx + scroll_speed, lo), hi);
}

void CBZPageLayout::reset()
{
    _cx = _viewport.w / 2.0f;
    _cy = _viewport.h / 2.0f;
    apply_zoom(_min_zoom);
}

void CBZPageLayout::draw_page()
{
    if (!_tex)
        return;

    if (_rotation == 90 && !_tex_r)
    {
        // Use the ORIGINAL texture dimensions for dstrect — SDL rotates the rect
        // itself, preserving aspect ratio. Swapping w/h here would stretch the texture.
        float vw = _tex_w * _zoom;
        float vh = _tex_h * _zoom;
        SDL_Rect dst = {(int)(_cx - vw / 2.0f), (int)(_cy - vh / 2.0f),
                        (int)vw, (int)vh};
        SDL_RenderCopyEx(RENDERER, _tex, NULL, &dst, 90.0, NULL, SDL_FLIP_NONE);
    }
    else if (_tex_r)
    {
        // Spread: render left and right textures side-by-side
        float total_w = _tex_w * _zoom;
        float total_h = _tex_h * _zoom;
        float left_x  = _cx - total_w / 2.0f;
        float top_y   = _cy - total_h / 2.0f;
        float left_w  = (_tex_w - _tex_r_w) * _zoom;

        int rx = (int)(left_x + left_w);  // right edge from float, no gap
        SDL_Rect dst_l = {(int)left_x, (int)top_y, rx - (int)left_x, (int)total_h};
        SDL_Rect dst_r = {rx,          (int)top_y, (int)(left_x + total_w) - rx, (int)total_h};
        SDL_RenderCopy(RENDERER, _tex,   NULL, &dst_l);
        SDL_RenderCopy(RENDERER, _tex_r, NULL, &dst_r);
    }
    else
    {
        float total_w = _tex_w * _zoom;
        float total_h = _tex_h * _zoom;
        SDL_Rect dst = {(int)(_cx - total_w / 2.0f), (int)(_cy - total_h / 2.0f),
                        (int)total_w, (int)total_h};
        SDL_RenderCopy(RENDERER, _tex, NULL, &dst);
    }
}

char *CBZPageLayout::info()
{
    if (_enumerating)
    {
        if (_tex_r)
            snprintf(_info_buf, sizeof(_info_buf), "%d-%d/?, %.0f%%",
                     _current_page + 1, _current_page + 2, _zoom * 100.0f);
        else
            snprintf(_info_buf, sizeof(_info_buf), "%d/?, %.0f%%",
                     _current_page + 1, _zoom * 100.0f);
    }
    else if (_tex_r)
        snprintf(_info_buf, sizeof(_info_buf), "%d-%d/%d, %.0f%%",
                 _current_page + 1, _current_page + 2, _page_count, _zoom * 100.0f);
    else
        snprintf(_info_buf, sizeof(_info_buf), "%d/%d, %.0f%%",
                 _current_page + 1, _page_count, _zoom * 100.0f);
    return _info_buf;
}
