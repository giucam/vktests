
#include "display.h"

unordered_map<int, display::platform_display_factory> display::s_factories;

std::shared_ptr<display> display::create(platform p)
{
    class actual_dpy : public display {};
    auto dpy = std::make_shared<actual_dpy>();
    dpy->m_platform_dpy = s_factories[(int)p](dpy.get());
    return dpy;
}

std::shared_ptr<window> display::create_window(int width, int height)
{
    class actual_win : public window {};
    auto win = std::make_shared<actual_win>();

    win->m_width = width;
    win->m_height = height;
    win->m_dpy = shared_from_this();
    win->m_platformWindow = m_platform_dpy->create_window(win);
    return win;
}

bool display::register_platform(platform p, const platform_display_factory &factory)
{
    s_factories[(int)p] = factory;
    return true;
}

