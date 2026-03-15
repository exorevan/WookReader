#ifndef WOOK_READER_PAGE_LAYOUT_HPP
#define WOOK_READER_PAGE_LAYOUT_HPP

#include <mupdf/pdf.h>
#include <SDL2/SDL.h>
#include <switch.h>
#include <string>

extern fz_context *ctx;

static inline void FreeTextureIfNeeded(SDL_Texture **texture)
{
    if (texture && *texture)
    {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
    }
}

// Background rasterization state for MuPDF layouts.
// The thread uses a cloned context + independently-opened document,
// so no locking between main thread and background thread is needed.
struct BgRender {
    Thread        thread;
    fz_context   *bg_ctx  = nullptr;  // fz_clone_context(ctx)
    fz_document  *bg_doc  = nullptr;  // fz_open_document(bg_ctx, path)
    fz_pixmap    *result  = nullptr;  // written by bg thread, read by main after join
    volatile int  cancel  = 0;        // atomic: 1 = abort
    volatile int  done    = 0;        // atomic: 1 = thread finished
    bool          active  = false;
    int           target_page = -1;
    float         target_zoom = 1.0f;
    bool          dark_mode   = false;
};

class PageLayout
{
public:
    PageLayout(fz_document *doc, int current_page);
    virtual ~PageLayout();

    int current_page()
    {
        return _current_page;
    }

    // Call from BookReader after constructing layout to enable background rasterization.
    void set_bg_path(const char *path);

    // Call from BookReader::draw() each frame (for MuPDF layouts only).
    // Checks if background render finished and swaps in the new texture.
    // Also polls prefetch slots.
    void poll_bg_render();

    virtual void previous_page(int n);
    virtual void next_page(int n);
    virtual void goto_page(int num);    // 0-indexed absolute jump
    virtual void zoom_in(float zoom_amount);
    virtual void zoom_out(float zoom_amount);
    virtual void zoom_max();
    virtual void move_up(int scroll_speed);
    virtual void move_down(int scroll_speed);
    virtual void move_left(int scroll_speed);
    virtual void move_right(int scroll_speed);
    virtual void reset();
    virtual void draw_page();
    virtual char *info();

protected:
    virtual void render_page_to_texture(int num, bool reset_zoom);
    virtual void set_zoom(float value);
    virtual void move_page(float x, float y);
    virtual void top_of_page();

    fz_document *doc = NULL;
    pdf_document *pdf = NULL;
    const int pages_count = 0;

    int _current_page = 0;
    fz_rect page_bounds = fz_empty_rect;
    fz_point page_center = fz_make_point(0, 0);
    float min_zoom = 1, max_zoom = 1, zoom = 1;

    SDL_Rect viewport;
    SDL_Texture *page_texture = NULL;

private:
    std::string _bg_path;
    BgRender    _bg;       // current-page render (core 3)
    BgRender    _pf_fwd;   // prefetch N+1        (core 1)
    BgRender    _pf_bwd;   // prefetch N-1        (core 2)

    // Ready (decoded) prefetch textures
    SDL_Texture *_pf_fwd_tex  = nullptr;
    SDL_Texture *_pf_bwd_tex  = nullptr;
    int          _pf_fwd_page = -1;
    int          _pf_bwd_page = -1;
    float        _pf_fwd_zoom = -1.0f;
    float        _pf_bwd_zoom = -1.0f;

    // Current-page render
    void start_bg_render(int page, float z);
    void cancel_bg_render();
    static void bg_thread_entry(void *arg);

    // Shared render implementation (used by all three slots)
    void do_render_slot(BgRender& slot);

    // Per-slot thread entry points
    static void pf_fwd_thread_entry(void *arg);
    static void pf_bwd_thread_entry(void *arg);

    // Prefetch slot helpers
    void start_pf_slot(BgRender& slot, int page, float z, void (*entry)(void*), int core);
    void cancel_pf_slot(BgRender& slot, SDL_Texture **tex, int *page_ref, float *zoom_ref);
    void poll_pf_slot(BgRender& slot, SDL_Texture **tex, int *page_ref, float *zoom_ref);
    void start_prefetch();

    // Context/document init helper
    bool init_slot_ctx(BgRender& slot, const char *path);
    void free_slot_ctx(BgRender& slot);
};

#endif
