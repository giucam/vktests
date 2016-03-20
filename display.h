
#pragma once

#include <assert.h>
#include <string.h>

#include <exception>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

#include <xcb/xcb.h>

#include "stringview.h"

class display;
class window;
class platform_window;
class platform_display;

class vk_surface;
class vk_instance;
class vk_physical_device;

using std::string;
using std::weak_ptr;
using std::unique_ptr;
using std::make_unique;
using std::make_shared;
using std::function;
using std::unordered_map;

class platform_exception : public std::exception
{
public:
    explicit platform_exception(stringview err)
        : m_err(err.to_string())
    {
    }

    const char *what() const throw()
    {
        return m_err.data();
    }

private:
    string m_err;
};

enum class platform {
    xcb,
    wayland,
};

class platform_window
{
public:
    explicit platform_window(const std::weak_ptr<window> &win)
        : m_window(win)
    {
    }

    virtual void show() = 0;
    virtual std::shared_ptr<vk_surface> create_vk_surface(const std::weak_ptr<vk_instance> &instance) = 0;

protected:
    std::weak_ptr<window> m_window;
};

class platform_display
{
public:
    explicit platform_display(display *dpy)
        : m_dpy(dpy)
    {
    }

    virtual std::shared_ptr<vk_instance> create_vk_instance(const std::vector<std::string> &extensions) = 0;
    virtual unique_ptr<platform_window> create_window(const std::weak_ptr<window> &win) = 0;

protected:
    display *m_dpy;
};


class display : public std::enable_shared_from_this<display>
{
public:
    typedef function<unique_ptr<platform_display> (display *)> platform_display_factory;

    static std::shared_ptr<display> create(platform p);

    std::shared_ptr<vk_instance> create_vk_instance(const std::vector<std::string> &extensions);
    std::shared_ptr<window> create_window(int width, int height);

    static bool register_platform(platform p, const platform_display_factory &factory);

private:
    display() {}

    unique_ptr<platform_display> m_platform_dpy;

    static unordered_map<int, platform_display_factory> s_factories;

    friend class window;
};

class window : public std::enable_shared_from_this<window>
{
public:
    weak_ptr<display> get_display() const { return m_dpy; }

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }

    void show() { return m_platformWindow->show(); }
    std::shared_ptr<vk_surface> create_vk_surface(const std::weak_ptr<vk_instance> &instance)
    {
        return m_platformWindow->create_vk_surface(instance);
    }

private:
    window() {}

    weak_ptr<display> m_dpy;
    unique_ptr<platform_window> m_platformWindow;
    int m_width;
    int m_height;

    friend class display;
};

