#ifndef WOOK_READER_VERTICAL_PAGE_LAYOUT_HPP
#define WOOK_READER_VERTICAL_PAGE_LAYOUT_HPP

#include "PageLayout.hpp"

// Vertical (comic) reading mode: page fits to screen width, right-stick navigates pages.
class VerticalPageLayout : public PageLayout
{
public:
    VerticalPageLayout(fz_document *doc, int current_page);

    void reset();
    void next_page(int n);

private:
    // Override min/max/zoom so the page always fills the viewport width.
    void apply_fit_to_width();
};

#endif
