#ifndef WOOK_READER_VERTICAL_PAGE_LAYOUT_HPP
#define WOOK_READER_VERTICAL_PAGE_LAYOUT_HPP

#include "PageLayout.hpp"

// Vertical (comic) reading mode: page fits to screen width, right-stick navigates pages.
class VerticalPageLayout : public PageLayout
{
public:
    VerticalPageLayout(fz_document *doc, int current_page);

    void reset();

    float get_min_zoom_for_bounds(const fz_rect &bounds) const override;
    float get_max_zoom_for_bounds(const fz_rect &bounds, float min_z) const override;
};

#endif
