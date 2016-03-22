
#pragma once

#include <assert.h>
#include <string.h>

#include <exception>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>

#include "stringview.h"
#include "vk.h"

class display;
class window;
class platform_window;

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
    template<class T>
    platform_window(T t)
        : m_interface(std::make_unique<win<T>>(std::move(t)))
    {
    }

private:
    struct win_interface
    {
        ~win_interface() = default;
        virtual void show() = 0;
        virtual vk_surface create_vk_surface(const vk_instance &instance, const window &win) = 0;
    };

    template<class T>
    struct win : win_interface
    {
        win(T t) : data(std::move(t)) {}
        void show() override { data.show(); }
        vk_surface create_vk_surface(const vk_instance &instance, const window &win) override
        {
            return data.create_vk_surface(instance, win);
        }

        T data;
    };

    std::unique_ptr<win_interface> m_interface;
    friend class window;
    friend class display;
};

class display
{
public:
    class platform_display
    {
    public:
        template<class T>
        platform_display(T t)
            : m_interface(std::make_unique<dpy<T>>(std::move(t)))
        {}

    private:
        struct dpy_interface
        {
            virtual ~dpy_interface() = default;
            virtual vk_instance create_vk_instance(const std::vector<std::string> &extensions) = 0;
            virtual platform_window create_window(int width, int height) = 0;
        };

        template<class T>
        struct dpy : dpy_interface
        {
            dpy(T t) : data(std::move(t)) {}

            vk_instance create_vk_instance(const std::vector<std::string> &extensions) override
            {
                return data.create_vk_instance(extensions);
            }
            platform_window create_window(int width, int height) override
            {
                return data.create_window(width, height);
            }

            T data;
        };

        std::unique_ptr<dpy_interface> m_interface;

        friend class display;
    };

    using platform_display_factory = std::function<platform_display ()>;

    display(platform p);

    vk_instance create_vk_instance(const std::vector<std::string> &extensions);
    window create_window(int width, int height);

    static void register_platform(platform p, const platform_display_factory &factory);

private:
    platform_display m_platform_dpy;
    static unordered_map<int, platform_display_factory> s_factories;

    friend class window;
};

#define REGISTER_PLATFORM(platform, type) \
    __attribute__((constructor)) \
    static void register_platform() { \
        display::register_platform(platform, []() -> type { return type(); }); \
    }

class window
{
public:
    window(platform_window win);
    window(const window &) = delete;
    window(window &&w);

    int get_width() const { return m_width; }
    int get_height() const { return m_height; }

    void show();
    vk_surface create_vk_surface(const vk_instance &instance);

private:
    platform_window m_platform_window;
    int m_width;
    int m_height;

    friend class display;
};

