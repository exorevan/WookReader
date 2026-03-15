#ifndef WOOK_READER_BOOK_READER_HPP
#define WOOK_READER_BOOK_READER_HPP

#include <mupdf/pdf.h>
#include <string>
#include "PageLayout.hpp"
#include "CBZPageLayout.hpp"
#include <switch.h>
struct SDL_Texture;

typedef enum
{
    BookPageLayoutPortrait,
    BookPageLayoutLandscape,
    BookPageLayoutVertical
} BookPageLayout;

class BookReader
{
public:
    BookReader(const char *path, int *result);
    ~BookReader();

    bool permStatusBar = false;

    void previous_page(int n);
    void next_page(int n);
    void goto_page(int page_1indexed);
    void zoom_in(float zoom_amount);
    void zoom_out(float zoom_amount);
    void zoom_at_point(float delta, float px, float py);
    void move_page_up(int scroll_speed);
    void move_page_down(int scroll_speed);
    void move_page_left(int scroll_speed);
    void move_page_right(int scroll_speed);
    void reset_page();
    void zoom_max();
    void switch_page_layout();
    void draw(bool drawHelp);

    BookPageLayout currentPageLayout()
    {
        return _currentPageLayout;
    }

private:
    void show_status_bar();
    void switch_current_page_layout(BookPageLayout bookPageLayout, int current_page);

    fz_document *doc = NULL;
    int status_bar_visible_counter = 0;

    BookPageLayout _currentPageLayout = BookPageLayoutPortrait;
    PageLayout *layout = nullptr;

    std::string book_name;
    std::string book_path; // needed to reconstruct CBZPageLayout on layout switch
    bool _is_cbz = false;
};

#endif
