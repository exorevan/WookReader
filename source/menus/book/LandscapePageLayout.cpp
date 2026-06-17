#include "LandscapePageLayout.hpp"
#include "common.h"
#include <algorithm>

LandscapePageLayout::LandscapePageLayout(fz_document *doc, int current_page) : PageLayout(doc, current_page)
{
    int w = viewport.w;
    viewport.w = viewport.h;
    viewport.h = w;

    reset();
}

void LandscapePageLayout::reset()
{
    page_center = fz_make_point(viewport.h / 2, viewport.w / 2);
    set_zoom(min_zoom);
};

void LandscapePageLayout::next_page(int n)
{
    render_page_to_texture(_current_page + n, false);
}

void LandscapePageLayout::draw_page()
{
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    SDL_Rect rect;
    rect.y = page_center.y - h / 2;
    rect.x = page_center.x - w / 2;
    rect.w = w;
    rect.h = h;

    SDL_RenderCopyEx(RENDERER, page_texture, NULL, &rect, 90, NULL, SDL_FLIP_NONE);
}

void LandscapePageLayout::move_page(float x, float y)
{
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    float cx_lo = std::min(h / 2.0f, (float)viewport.h - h / 2.0f);
    float cx_hi = std::max(h / 2.0f, (float)viewport.h - h / 2.0f);
    page_center.x = std::fmin(std::fmax(page_center.x + y, cx_lo), cx_hi);

    float cy_lo = std::min(w / 2.0f, (float)viewport.w - w / 2.0f);
    float cy_hi = std::max(w / 2.0f, (float)viewport.w - w / 2.0f);
    page_center.y = std::fmin(std::fmax(page_center.y + x, cy_lo), cy_hi);
}

void LandscapePageLayout::get_scroll_fractions(float &frac_x, float &frac_y) const
{
    float w_old = page_bounds.x1 * zoom;
    float h_old = page_bounds.y1 * zoom;

    frac_x = 0.5f;
    if (h_old > viewport.h) {
        frac_x = (page_center.x - viewport.h + h_old / 2.0f) / (h_old - viewport.h);
        frac_x = std::fmin(std::fmax(frac_x, 0.0f), 1.0f);
    }

    frac_y = 0.5f;
    if (w_old > viewport.w) {
        frac_y = (w_old / 2.0f - page_center.y) / (w_old - viewport.w);
        frac_y = std::fmin(std::fmax(frac_y, 0.0f), 1.0f);
    }
}

void LandscapePageLayout::apply_position(float old_zoom_factor, bool should_reset_zoom, bool keep_position, float frac_x, float frac_y)
{
    if (_has_saved_view)
    {
        zoom = std::fmin(std::fmax(_saved_zoom, min_zoom), max_zoom);
        page_center.x = _saved_cx;
        page_center.y = _saved_cy;
        move_page(0, 0); // Clamp to viewport limits
        _has_saved_view = false;
    }
    else if (should_reset_zoom)
    {
        zoom = min_zoom;
        page_center.y = viewport.w / 2.0f;
        float h_new = page_bounds.y1 * zoom;
        page_center.x = (h_new > viewport.h) ? (viewport.h - h_new / 2.0f) : (viewport.h / 2.0f);
    }
    else
    {
        zoom = std::fmin(std::fmax(old_zoom_factor * min_zoom, min_zoom), max_zoom);

        float w_new = page_bounds.x1 * zoom;
        float h_new = page_bounds.y1 * zoom;

        if (keep_position) {
            // Keep relative scroll position
            if (h_new > viewport.h) {
                page_center.x = (viewport.h - h_new / 2.0f) + frac_x * (h_new - viewport.h);
            } else {
                page_center.x = viewport.h / 2.0f;
            }
            if (w_new > viewport.w) {
                page_center.y = w_new / 2.0f - frac_y * (w_new - viewport.w);
            } else {
                page_center.y = viewport.w / 2.0f;
            }
        } else {
            // Reset position to right-aligned (top of page in rotated view) and Y-centered
            page_center.y = viewport.w / 2.0f;
            page_center.x = (h_new > viewport.h) ? (viewport.h - h_new / 2.0f) : (viewport.h / 2.0f);
        }
    }
}

void LandscapePageLayout::top_of_page()
{
    float h_new = page_bounds.y1 * zoom;
    page_center.y = viewport.w / 2.0f;
    page_center.x = (h_new > viewport.h) ? (viewport.h - h_new / 2.0f) : (viewport.h / 2.0f);
}
