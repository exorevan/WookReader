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
    void move_page(float x, float y) override;
    void get_scroll_fractions(float &frac_x, float &frac_y) const override;
    void apply_position(float old_zoom_factor, bool should_reset_zoom, bool keep_position, float frac_x, float frac_y) override;
    void top_of_page() override;
};

#endif
