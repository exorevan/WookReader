#ifndef WOOK_READER_LANDSCAPE_PAGE_LAYOUT_HPP
#define WOOK_READER_LANDSCAPE_PAGE_LAYOUT_HPP

#include "PageLayout.hpp"

class LandscapePageLayout : public PageLayout
{
public:
    LandscapePageLayout(fz_document *doc, int current_page);

    void reset();
    void draw_page();
    virtual void next_page(int n);

protected:
    void move_page(float x, float y);
};

#endif
