#pragma once

#include "PageLayout.hpp"
extern "C" {
#include <switch/types.h>
#include <switch/result.h>
#include <switch/kernel/thread.h>
}
#include <archive.h>
#include <vector>
#include <string>

class CBZPageLayout : public PageLayout
{
public:
    // Opens the comic archive at path and loads page start_page.
    // Supports ZIP (.cbz), RAR3/RAR5 (.cbr), TAR (.cbt), 7z (.cb7), etc.
    CBZPageLayout(const char *path, int start_page);
    ~CBZPageLayout();

    void previous_page(int n) override;
    void next_page(int n) override;
    void goto_page(int num) override;
    void zoom_in(float zoom_amount) override;
    void zoom_out(float zoom_amount) override;
    void zoom_max() override;
    void move_up(int scroll_speed) override;
    void move_down(int scroll_speed) override;
    void move_left(int scroll_speed) override;
    void move_right(int scroll_speed) override;
    void reset() override;
    void draw_page() override;
    char *info() override;

    bool is_valid()        const { return _valid; }
    bool is_enumerating()  const { return _enumerating && !__atomic_load_n(&_enum_done, __ATOMIC_ACQUIRE); }
    bool is_enumerating_internal() const { return _enumerating; }  // true until finish_enumeration() called
    int  page_count()      const { return _page_count; }

    // Called once per frame from BookReader::draw() while is_enumerating().
    // When enumeration finishes, joins the thread and loads the first page.
    void finish_enumeration();

    bool is_first_image_ready() const { return __atomic_load_n(&_first_image_ready, __ATOMIC_ACQUIRE); }
    void apply_first_image();  // decode _first_image_raw → SDL_Texture, sets _valid = true

    void toggle_spread();
    void zoom_at_point(float delta, float px, float py);
    void poll_prefetch();  // call once per frame to upload finished prefetch pixels to GPU

private:
    // Load image at page index from the archive, build SDL_Texture, optionally reset zoom.
    void load_page_texture(int page_num, bool reset_zoom);
    void clamp_center();
    void apply_zoom(float new_zoom);
    void free_ready_textures();

    std::string              _archive_path;          // path to the archive file
    std::vector<std::string> _page_names;            // sorted list of image filenames inside archive
    int                      _page_count = 0;
    bool                     _valid      = false;

    // Background enumeration thread
    Thread         _enum_thread;
    bool           _enumerating  = false;
    volatile int   _enum_done    = 0;   // atomic: 1 when thread wrote _page_names
    volatile int   _enum_count   = 0;   // atomic: pages discovered so far (release after push_back)
    std::string    _enum_current_name;  // page name user is viewing during enum (for sort reconciliation)
    int            _start_page   = 0;   // deferred until finish_enumeration()

    static void enum_thread_entry(void *arg);
    void        do_enumerate();

    // Early first-page display: enum thread signals as soon as first image data is known,
    // allowing the main thread to show it while the full scan continues in the background.
    volatile int   _first_image_ready = 0;   // atomic: set by enum thread
    void          *_first_image_raw   = nullptr;
    size_t         _first_image_size  = 0;
    std::string    _first_image_name;         // archive-order entry name

    SDL_Rect     _viewport  = {0, 0, 1280, 720};
    SDL_Texture *_tex       = nullptr;   // left (or single) page texture
    SDL_Texture *_tex_r     = nullptr;   // right page texture (spread mode only)
    int          _tex_w     = 1;         // combined width in spread, single width otherwise
    int          _tex_h     = 1;         // max(left_h, right_h) in spread, single height otherwise
    int          _tex_r_w   = 1;         // right page pixel width (for split rendering)
    bool         _spread_mode = false;   // show two pages side-by-side
    int          _rotation    = 0;       // 0 or 90 degrees (clockwise)

    float _zoom     = 1.0f;
    float _min_zoom = 0.5f;
    float _max_zoom = 4.0f;
    float _cx       = 640.0f;
    float _cy       = 360.0f;

    // Triple-buffer: pre-uploaded textures for instant page swap
    SDL_Texture *_ready_next    = nullptr;
    SDL_Texture *_ready_prev    = nullptr;
    int          _ready_next_pg = -1;   // page index held by _ready_next, -1 = empty
    int          _ready_prev_pg = -1;
    int          _ready_next_w  = 0;
    int          _ready_next_h  = 0;
    int          _ready_prev_w  = 0;
    int          _ready_prev_h  = 0;

    // Async prefetch: three parallel threads decode N+1, N-1, and N+2 on separate cores
    static constexpr int N_PF = 3;
    void start_prefetch(int fwd_page, int bwd_page, int fwd2_page);
    void cancel_prefetch();
    void cancel_one_slot(int i);
    static void prefetch_entry_0(void *arg);
    static void prefetch_entry_1(void *arg);
    static void prefetch_entry_2(void *arg);
    void do_prefetch(int slot);

    // Slots: [0]=N+1 on core 1, [1]=N-1 on core 2, [2]=N+2 on core 3 (when not enumerating)
    Thread          _pf_thread[N_PF];
    bool            _pf_active[N_PF]   = {};
    int             _pf_page[N_PF]     = {-1, -1, -1};
    std::string     _pf_page_name[N_PF]; // local copy — safe from sort races
    unsigned char  *_pf_pixels[N_PF]   = {};
    int             _pf_w[N_PF]        = {};
    int             _pf_h[N_PF]        = {};
    volatile int    _pf_cancel[N_PF]   = {};
    volatile int    _pf_ok[N_PF]       = {};

    char _info_buf[128];
};
