
#include "display.h"

unordered_map<int, display::platform_display_factory> display::s_factories;

display::display(platform p)
       : m_platform_dpy(s_factories[int(p)]())
{
}

vk_instance display::create_vk_instance(const std::vector<std::string> &extensions)
{
    return m_platform_dpy.m_interface->create_vk_instance(extensions);
}

window display::create_window(int width, int height)
{
    auto win = window(m_platform_dpy.m_interface->create_window(width, height));

    win.m_width = width;
    win.m_height = height;
    return win;
}

void display::run(const update_callback &update)
{
    m_platform_dpy.m_interface->run(update);
}

void display::register_platform(platform p, const platform_display_factory &factory)
{
    s_factories[(int)p] = factory;
}


window::window(platform_window w)
      : m_platform_window(std::move(w))
{
}

window::window(window &&w)
      : m_platform_window(std::move(w.m_platform_window))
{
}

void window::show()
{
    return m_platform_window.m_interface->show();
}

vk_surface window::create_vk_surface(const vk_instance &instance)
{
    return m_platform_window.m_interface->create_vk_surface(instance, *this);
}

