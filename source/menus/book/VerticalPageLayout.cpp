#include "VerticalPageLayout.hpp"
#include <algorithm>

float VerticalPageLayout::get_min_zoom_for_bounds(const fz_rect &bounds) const
{
    if (bounds.x1 <= 0.0f)
        return 1.0f;
    return (float)viewport.w / bounds.x1;
}

float VerticalPageLayout::get_max_zoom_for_bounds(const fz_rect &bounds, float min_z) const
{
    return std::max(min_z, min_z * 4.0f);
}

VerticalPageLayout::VerticalPageLayout(fz_document *doc, int current_page)
    : PageLayout(doc, current_page)
{
    reset();
}

void VerticalPageLayout::reset()
{
    PageLayout::reset();
    top_of_page();
}
