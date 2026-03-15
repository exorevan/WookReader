#include "VerticalPageLayout.hpp"
#include <algorithm>

// Fit the page so its width exactly fills the viewport width.
// After this call, a second render_page_to_texture(_current_page, false) is needed
// to produce the texture at the new zoom level.
void VerticalPageLayout::apply_fit_to_width()
{
    if (page_bounds.x1 <= 0)
        return;

    float fit_width = (float)viewport.w / page_bounds.x1;
    min_zoom = fit_width;
    max_zoom = std::fmax(fit_width, max_zoom); // never shrink max_zoom
    zoom = fit_width;
}

VerticalPageLayout::VerticalPageLayout(fz_document *doc, int current_page)
    : PageLayout(doc, current_page)
{
    // PageLayout constructor already rendered the page at standard fit-to-screen zoom.
    // Now re-render at fit-to-width zoom.
    apply_fit_to_width();
    render_page_to_texture(_current_page, false);
    top_of_page();
}

void VerticalPageLayout::reset()
{
    apply_fit_to_width();
    render_page_to_texture(_current_page, false);
    top_of_page();
}

void VerticalPageLayout::next_page(int n)
{
    // Load new page and reset bounds/zoom (reset_zoom=true).
    render_page_to_texture(_current_page + n, true);
    // Override zoom to fit-to-width and re-render.
    apply_fit_to_width();
    render_page_to_texture(_current_page, false);
    top_of_page();
}
