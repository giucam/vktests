
#include "display.h"

unordered_map<int, display::platform_display_factory> display::s_factories;

display::display(platform p)
       : m_platform_dpy(s_factories[int(p)]())
{
    m_platform_dpy.m_interface->init();
}

vk_instance display::create_vk_instance(const std::vector<std::string> &extensions)
{
    return m_platform_dpy.m_interface->create_vk_instance(extensions);
}

void display::run()
{
    m_platform_dpy.m_interface->run();
}

void display::quit()
{
    m_platform_dpy.m_interface->quit();
}

void display::register_platform(platform p, const platform_display_factory &factory)
{
    s_factories[(int)p] = factory;
}



window::window(const display &dpy, int width, int height, handler hnd)
      : m_platform_window(dpy.m_platform_dpy.m_interface->create_window(width, height, std::move(hnd)))
      , m_width(width)
      , m_height(height)
{
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

void window::update()
{
    m_platform_window.m_interface->update();
}

void window::prepare_swap()
{
    m_platform_window.m_interface->prepare_swap();
}

