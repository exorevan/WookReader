#include "PageLayout.hpp"
#include <algorithm>
#include <iostream>
#include <cmath>

extern "C"
{
#include "common.h"
#include "config.h"
#include "SDL_helper.h"
}

PageLayout::PageLayout(fz_document *doc, int current_page) : doc(doc), pdf(ctx ? pdf_specifics(ctx, doc) : nullptr), pages_count(ctx && doc ? fz_count_pages(ctx, doc) : 0)
{
    if (!ctx || !doc)
        return;
    fz_try(ctx)
    {
        _current_page = std::min(std::max(0, current_page), pages_count - 1);

        SDL_RenderGetViewport(RENDERER, &viewport);
        render_page_to_texture(_current_page, false);
    }
    fz_catch(ctx)
    {
        std::cout << "Failed to render page " << _current_page << std::endl;
    }
}

PageLayout::~PageLayout()
{
    cancel_bg_render();

    // Cancel and free prefetch slots before freeing their contexts
    if (_pf_fwd.bg_ctx) {
        cancel_pf_slot(_pf_fwd, &_pf_fwd_tex, &_pf_fwd_page, &_pf_fwd_zoom);
        free_slot_ctx(_pf_fwd);
    }
    if (_pf_bwd.bg_ctx) {
        cancel_pf_slot(_pf_bwd, &_pf_bwd_tex, &_pf_bwd_page, &_pf_bwd_zoom);
        free_slot_ctx(_pf_bwd);
    }

    FreeTextureIfNeeded(&page_texture);
    if (_bg.result)  { fz_drop_pixmap(_bg.bg_ctx, _bg.result); _bg.result = nullptr; }
    if (_bg.bg_doc)  { fz_drop_document(_bg.bg_ctx, _bg.bg_doc); _bg.bg_doc = nullptr; }
    if (_bg.bg_ctx)  { fz_drop_context(_bg.bg_ctx); _bg.bg_ctx = nullptr; }
}

// ── Context/document helpers ────────────────────────────────────────────────

bool PageLayout::init_slot_ctx(BgRender& slot, const char *path)
{
    slot.bg_ctx = fz_clone_context(ctx);
    if (!slot.bg_ctx) return false;
    fz_try(slot.bg_ctx)
    {
        slot.bg_doc = fz_open_document(slot.bg_ctx, path);
    }
    fz_catch(slot.bg_ctx)
    {
        fz_drop_context(slot.bg_ctx);
        slot.bg_ctx = nullptr;
        return false;
    }
    return true;
}

void PageLayout::free_slot_ctx(BgRender& slot)
{
    if (slot.bg_doc) { fz_drop_document(slot.bg_ctx, slot.bg_doc); slot.bg_doc = nullptr; }
    if (slot.bg_ctx) { fz_drop_context(slot.bg_ctx); slot.bg_ctx = nullptr; }
}

void PageLayout::set_bg_path(const char *path)
{
    if (!path || !ctx) return;
    _bg_path = path;

    init_slot_ctx(_bg,     path);
    init_slot_ctx(_pf_fwd, path);
    init_slot_ctx(_pf_bwd, path);

    // Kick off prefetch for N+1 (and N-1 if not on first page) immediately.
    start_prefetch();
}

void PageLayout::previous_page(int n)
{
    render_page_to_texture(_current_page - n, false);
}

void PageLayout::next_page(int n)
{
    render_page_to_texture(_current_page + n, false);
    top_of_page();
}

void PageLayout::goto_page(int num)
{
    render_page_to_texture(num, false);  // clamps 0..pages_count-1 internally
    top_of_page();
}

void PageLayout::zoom_in(float zoom_amount)
{
    set_zoom(zoom + zoom_amount);
};

void PageLayout::zoom_out(float zoom_amount)
{
    set_zoom(zoom - zoom_amount);
};

void PageLayout::move_up(int scroll_speed)
{
    move_page(0, scroll_speed);
};

void PageLayout::move_down(int scroll_speed)
{
    move_page(0, -scroll_speed);
};

void PageLayout::move_left(int scroll_speed)
{
    move_page(-scroll_speed, 0);
};

void PageLayout::move_right(int scroll_speed)
{
    move_page(scroll_speed, 0);
};

void PageLayout::reset()
{
    page_center = fz_make_point(viewport.w / 2, viewport.h / 2);
    set_zoom(min_zoom);
};

void PageLayout::draw_page()
{
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    SDL_Rect rect;
    rect.x = page_center.x - w / 2;
    rect.y = page_center.y - h / 2;
    rect.w = w;
    rect.h = h;

    SDL_RenderCopy(RENDERER, page_texture, NULL, &rect);
}

char *PageLayout::info()
{
    static char title[128];
    sprintf(title, "%i/%i, %.2f%%", _current_page + 1, pages_count, zoom * 100);
    return title;
}

// ── Shared render implementation ─────────────────────────────────────────────

void PageLayout::do_render_slot(BgRender& slot)
{
    fz_page    *page = nullptr;
    fz_pixmap  *pix  = nullptr;

    fz_try(slot.bg_ctx)
    {
        if (__atomic_load_n(&slot.cancel, __ATOMIC_ACQUIRE))
            fz_throw(slot.bg_ctx, FZ_ERROR_GENERIC, "cancelled");

        page = fz_load_page(slot.bg_ctx, slot.bg_doc, slot.target_page);

        if (__atomic_load_n(&slot.cancel, __ATOMIC_ACQUIRE))
            fz_throw(slot.bg_ctx, FZ_ERROR_GENERIC, "cancelled");

        pix = fz_new_pixmap_from_page_contents(
            slot.bg_ctx, page,
            fz_scale(slot.target_zoom, slot.target_zoom),
            fz_device_rgb(slot.bg_ctx), 0);

        if (slot.dark_mode)
            fz_invert_pixmap(slot.bg_ctx, pix);

        fz_drop_page(slot.bg_ctx, page); page = nullptr;
    }
    fz_catch(slot.bg_ctx)
    {
        if (page) { fz_drop_page(slot.bg_ctx, page); page = nullptr; }
        if (pix)  { fz_drop_pixmap(slot.bg_ctx, pix); pix = nullptr; }
    }

    if (!__atomic_load_n(&slot.cancel, __ATOMIC_ACQUIRE))
        slot.result = pix;
    else if (pix)
        fz_drop_pixmap(slot.bg_ctx, pix);

    __atomic_store_n(&slot.done, 1, __ATOMIC_RELEASE);
}

// Thread entry points
void PageLayout::bg_thread_entry(void *arg)
{
    static_cast<PageLayout*>(arg)->do_render_slot(static_cast<PageLayout*>(arg)->_bg);
}

void PageLayout::pf_fwd_thread_entry(void *arg)
{
    static_cast<PageLayout*>(arg)->do_render_slot(static_cast<PageLayout*>(arg)->_pf_fwd);
}

void PageLayout::pf_bwd_thread_entry(void *arg)
{
    static_cast<PageLayout*>(arg)->do_render_slot(static_cast<PageLayout*>(arg)->_pf_bwd);
}

// ── Current-page render ──────────────────────────────────────────────────────

void PageLayout::start_bg_render(int page, float z)
{
    if (!_bg.bg_ctx || !_bg.bg_doc) return;

    cancel_bg_render();

    _bg.target_page = page;
    _bg.target_zoom = z;
    _bg.dark_mode   = (configDarkMode != 0);
    __atomic_store_n(&_bg.cancel, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&_bg.done,   0, __ATOMIC_RELEASE);
    _bg.result = nullptr;

    if (threadCreate(&_bg.thread, bg_thread_entry, this, nullptr, 0x80000, 0x30, 2) != 0)
        return;
    if (threadStart(&_bg.thread) != 0)
    {
        threadClose(&_bg.thread);
        return;
    }
    _bg.active = true;
}

void PageLayout::cancel_bg_render()
{
    if (!_bg.active) return;
    __atomic_store_n(&_bg.cancel, 1, __ATOMIC_RELEASE);
    threadWaitForExit(&_bg.thread);
    threadClose(&_bg.thread);
    _bg.active = false;
    if (_bg.result)
    {
        fz_drop_pixmap(_bg.bg_ctx, _bg.result);
        _bg.result = nullptr;
    }
}

void PageLayout::poll_bg_render()
{
    // Poll current-page render
    if (_bg.active && __atomic_load_n(&_bg.done, __ATOMIC_ACQUIRE))
    {
        threadWaitForExit(&_bg.thread);
        threadClose(&_bg.thread);
        _bg.active = false;

        if (_bg.result)
        {
            SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
                _bg.result->samples,
                _bg.result->w, _bg.result->h,
                _bg.result->n * 8,
                _bg.result->w * _bg.result->n,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0);

            SDL_Texture *new_tex = surf ? SDL_CreateTextureFromSurface(RENDERER, surf) : nullptr;
            SDL_FreeSurface(surf);

            fz_drop_pixmap(_bg.bg_ctx, _bg.result);
            _bg.result = nullptr;

            if (new_tex)
            {
                FreeTextureIfNeeded(&page_texture);
                page_texture = new_tex;
            }
        }

        // Current page is now displayed — kick off prefetch for neighbors
        start_prefetch();
    }

    // Poll prefetch slots every frame
    poll_pf_slot(_pf_fwd, &_pf_fwd_tex, &_pf_fwd_page, &_pf_fwd_zoom);
    poll_pf_slot(_pf_bwd, &_pf_bwd_tex, &_pf_bwd_page, &_pf_bwd_zoom);
}

// ── Prefetch helpers ─────────────────────────────────────────────────────────

void PageLayout::start_pf_slot(BgRender& slot, int page, float z, void (*entry)(void*), int core)
{
    slot.target_page = page;
    slot.target_zoom = z;
    slot.dark_mode   = (configDarkMode != 0);
    __atomic_store_n(&slot.cancel, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&slot.done,   0, __ATOMIC_RELEASE);
    slot.result = nullptr;

    // Priority 0x31 = slightly below current-page render (0x30)
    if (threadCreate(&slot.thread, entry, this, nullptr, 0x80000, 0x31, core) != 0)
        return;
    if (threadStart(&slot.thread) != 0)
    {
        threadClose(&slot.thread);
        return;
    }
    slot.active = true;
}

void PageLayout::cancel_pf_slot(BgRender& slot, SDL_Texture **tex, int *page_ref, float *zoom_ref)
{
    if (slot.active)
    {
        __atomic_store_n(&slot.cancel, 1, __ATOMIC_RELEASE);
        threadWaitForExit(&slot.thread);
        threadClose(&slot.thread);
        slot.active = false;
        if (slot.result) { fz_drop_pixmap(slot.bg_ctx, slot.result); slot.result = nullptr; }
    }
    FreeTextureIfNeeded(tex);
    *page_ref = -1;
    *zoom_ref = -1.0f;
}

void PageLayout::poll_pf_slot(BgRender& slot, SDL_Texture **tex, int *page_ref, float *zoom_ref)
{
    if (!slot.active) return;
    if (!__atomic_load_n(&slot.done, __ATOMIC_ACQUIRE)) return;

    threadWaitForExit(&slot.thread);
    threadClose(&slot.thread);
    slot.active = false;

    if (slot.result)
    {
        FreeTextureIfNeeded(tex);  // discard any previous ready texture for this slot
        SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
            slot.result->samples,
            slot.result->w, slot.result->h,
            slot.result->n * 8,
            slot.result->w * slot.result->n,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0);

        *tex = surf ? SDL_CreateTextureFromSurface(RENDERER, surf) : nullptr;
        SDL_FreeSurface(surf);

        fz_drop_pixmap(slot.bg_ctx, slot.result);
        slot.result = nullptr;

        *page_ref = slot.target_page;
        *zoom_ref = slot.target_zoom;
    }
}

void PageLayout::start_prefetch()
{
    if (!_pf_fwd.bg_ctx || !_pf_bwd.bg_ctx) return;

    int target_fwd = _current_page + 1;
    int target_bwd = _current_page - 1;

    // Forward: skip if already ready or already rendering the right page at the right zoom
    bool fwd_ready   = (_pf_fwd_tex && _pf_fwd_page == target_fwd && fabsf(_pf_fwd_zoom - zoom) < 0.001f);
    bool fwd_running = (_pf_fwd.active && _pf_fwd.target_page == target_fwd && fabsf(_pf_fwd.target_zoom - zoom) < 0.001f);
    if (!fwd_ready && !fwd_running)
    {
        cancel_pf_slot(_pf_fwd, &_pf_fwd_tex, &_pf_fwd_page, &_pf_fwd_zoom);
        if (target_fwd < pages_count)
            start_pf_slot(_pf_fwd, target_fwd, zoom, pf_fwd_thread_entry, 1);
    }

    // Backward: same logic
    bool bwd_ready   = (_pf_bwd_tex && _pf_bwd_page == target_bwd && fabsf(_pf_bwd_zoom - zoom) < 0.001f);
    bool bwd_running = (_pf_bwd.active && _pf_bwd.target_page == target_bwd && fabsf(_pf_bwd.target_zoom - zoom) < 0.001f);
    if (!bwd_ready && !bwd_running)
    {
        cancel_pf_slot(_pf_bwd, &_pf_bwd_tex, &_pf_bwd_page, &_pf_bwd_zoom);
        if (target_bwd >= 0)
            start_pf_slot(_pf_bwd, target_bwd, zoom, pf_bwd_thread_entry, 2);
    }
}

// ── Page bounds helper (fast, no rasterization) ─────────────────────────────

static bool get_page_bounds(fz_context *c, fz_document *d, int num, fz_rect *out_bounds)
{
    bool ok = false;
    fz_page *page = nullptr;
    fz_try(c)
    {
        page = fz_load_page(c, d, num);
        *out_bounds = fz_bound_page(c, page);
        ok = true;
    }
    fz_catch(c) {}
    if (page) fz_drop_page(c, page);
    return ok;
}

void PageLayout::render_page_to_texture(int num, bool reset_zoom)
{
    _current_page = std::min(std::max(0, num), pages_count - 1);

    // Fast sync path: load bounds only (no rasterization) to update zoom/center.
    fz_rect bounds = fz_empty_rect;
    if (!get_page_bounds(ctx, doc, _current_page, &bounds))
    {
        std::cout << "Failed to load page bounds " << _current_page << std::endl;
        return;
    }

    if (bounds.x1 != page_bounds.x1 || bounds.y1 != page_bounds.y1 || reset_zoom)
    {
        page_bounds = bounds;
        page_center = fz_make_point(viewport.w / 2, viewport.h / 2);
        min_zoom = fmin(viewport.w / bounds.x1, viewport.h / bounds.y1);
        max_zoom = fmax(viewport.w / bounds.x1, viewport.h / bounds.y1);
        zoom = min_zoom;
    }

    // Check prefetch cache — instant texture swap with no rendering
    if (!reset_zoom)
    {
        if (_pf_fwd_tex && _pf_fwd_page == _current_page && fabsf(_pf_fwd_zoom - zoom) < 0.001f)
        {
            cancel_bg_render();
            FreeTextureIfNeeded(&page_texture);
            page_texture = _pf_fwd_tex;
            _pf_fwd_tex  = nullptr;
            _pf_fwd_page = -1;
            start_prefetch();
            return;
        }
        if (_pf_bwd_tex && _pf_bwd_page == _current_page && fabsf(_pf_bwd_zoom - zoom) < 0.001f)
        {
            cancel_bg_render();
            FreeTextureIfNeeded(&page_texture);
            page_texture = _pf_bwd_tex;
            _pf_bwd_tex  = nullptr;
            _pf_bwd_page = -1;
            start_prefetch();
            return;
        }
    }

    // No cache hit — use async background rasterization if available.
    if (_bg.bg_ctx && _bg.bg_doc)
    {
        start_bg_render(_current_page, zoom);
        return;
    }

    // Synchronous fallback (used for first page load before set_bg_path() is called).
    FreeTextureIfNeeded(&page_texture);

    fz_page   *page = nullptr;
    fz_pixmap *pix  = nullptr;
    fz_try(ctx)
    {
        page = fz_load_page(ctx, doc, _current_page);
        pix  = fz_new_pixmap_from_page_contents(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
        if (configDarkMode)
            fz_invert_pixmap(ctx, pix);

        SDL_Surface *image = SDL_CreateRGBSurfaceFrom(pix->samples, pix->w, pix->h, pix->n * 8, pix->w * pix->n, 0x000000FF, 0x0000FF00, 0x00FF0000, 0);
        page_texture = SDL_CreateTextureFromSurface(RENDERER, image);
        SDL_FreeSurface(image);
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx)
    {
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
        std::cout << "Failed to render page " << _current_page << std::endl;
    }
}

void PageLayout::set_zoom(float value)
{
    value = fmin(fmax(min_zoom, value), max_zoom);

    if (value == zoom)
        return;

    zoom = value;

    // Prefetch cache is at the old zoom — invalidate it
    cancel_pf_slot(_pf_fwd, &_pf_fwd_tex, &_pf_fwd_page, &_pf_fwd_zoom);
    cancel_pf_slot(_pf_bwd, &_pf_bwd_tex, &_pf_bwd_page, &_pf_bwd_zoom);

    if (_bg.bg_ctx && _bg.bg_doc)
    {
        start_bg_render(_current_page, zoom);
        // start_prefetch() will be called once the current-page render completes
    }
    else
    {
        FreeTextureIfNeeded(&page_texture);
        fz_page   *page = nullptr;
        fz_pixmap *pix  = nullptr;
        fz_try(ctx)
        {
            page = fz_load_page(ctx, doc, _current_page);
            pix  = fz_new_pixmap_from_page_contents(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
            if (configDarkMode) fz_invert_pixmap(ctx, pix);
            SDL_Surface *img = SDL_CreateRGBSurfaceFrom(pix->samples, pix->w, pix->h, pix->n * 8, pix->w * pix->n, 0x000000FF, 0x0000FF00, 0x00FF0000, 0);
            page_texture = SDL_CreateTextureFromSurface(RENDERER, img);
            SDL_FreeSurface(img);
            fz_drop_pixmap(ctx, pix);
            fz_drop_page(ctx, page);
        }
        fz_catch(ctx)
        {
            fz_drop_pixmap(ctx, pix);
            fz_drop_page(ctx, page);
        }
    }
    move_page(0, 0);
}

void PageLayout::move_page(float x, float y)
{
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    page_center.x = fmin(fmax(page_center.x + x, w / 2), viewport.w - w / 2);
    page_center.y = fmin(fmax(page_center.y + y, viewport.h - h / 2), h / 2);
}

void PageLayout::top_of_page()
{
    float h = page_bounds.y1 * zoom;
    page_center = fz_make_point(viewport.w / 2, h / 2);
}

void PageLayout::zoom_max()
{
    zoom = max_zoom;
    set_zoom(zoom);
    /*Although this should fix this for now figure out why this blurs the image*/
    zoom_out(0.6);
    zoom_in(0.6);
}
