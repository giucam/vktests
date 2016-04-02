
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>

#include "vk.h"
#include "display.h"
#include "event_loop.h"

template<class T, class... Args>
struct Wrapper {
    template<void (T::*F)(Args...)>
    static void forward(void *data, Args... args) {
        (static_cast<T *>(data)->*F)(std::forward<Args>(args)...);
    }
};

template<class T, class... Args>
constexpr static auto createWrapper(void (T::*)(Args...)) -> Wrapper<T, Args...> {
    return Wrapper<T, Args...>();
}

#define wrapInterface(method) createWrapper(method).forward<method>

class wl_platform_display;
class wl_platform_window;

class pointer
{
public:
    explicit pointer(wl_pointer *p)
        : m_pointer(p)
        , m_window(nullptr)
    {
        static const wl_pointer_listener listener = {
            wrapInterface(&pointer::enter),
            wrapInterface(&pointer::leave),
            wrapInterface(&pointer::motion),
            wrapInterface(&pointer::button),
            wrapInterface(&pointer::axis),
        };
        wl_pointer_add_listener(p, &listener, this);
    }

    void enter(wl_pointer *, uint32_t serial, wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
    void leave(wl_pointer *, uint32_t serial, wl_surface *surface);
    void motion(wl_pointer *, uint32_t time, wl_fixed_t fx, wl_fixed_t fy);
    void button(wl_pointer *, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
    void axis(wl_pointer *, uint32_t time, uint32_t axis, wl_fixed_t value);

private:
    wl_pointer *m_pointer;
    wl_platform_window *m_window;
};

class seat
{
public:
    explicit seat(wl_seat *s)
        : m_seat(s)
    {
        static const wl_seat_listener listener = {
            wrapInterface(&seat::capabilities),
        };
        wl_seat_add_listener(s, &listener, this);
    }

    void capabilities(wl_seat *, uint32_t caps)
    {
        if (!m_pointer & caps & WL_SEAT_CAPABILITY_POINTER) {
            m_pointer = std::make_unique<pointer>(wl_seat_get_pointer(m_seat));
        }
    }

    constexpr static uint32_t supported_version = 1;

private:
    wl_seat *m_seat;
    std::unique_ptr<pointer> m_pointer;
};

class wl_platform_window
{
public:
    explicit wl_platform_window(wl_platform_display *dpy, int w, int h, window::handler hnd);
    wl_platform_window(const wl_platform_window &) = delete;
    wl_platform_window(wl_platform_window &&w);

    void show();
    vk_surface create_vk_surface(const vk_instance &instance, window &win);
    void update();
    void prepare_swap();

    window::handler &get_handler() { return m_winhnd; }

    static wl_platform_window *from_surface(wl_surface *surf) { return static_cast<wl_platform_window *>(wl_surface_get_user_data(surf)); }

private:
    void send_update(wl_callback *, uint32_t time);

    window::handler m_winhnd;
    wl_platform_display *m_display;
    wl_surface *m_surface;
    wl_shell_surface *m_shell_surface;
    bool m_update;
    wl_callback *m_frame_callback;
};

class wl_platform_display
{
public:
    explicit wl_platform_display()
        : m_compositor(nullptr)
        , m_shell(nullptr)
    {
    }

    void init()
    {
        m_display = wl_display_connect(nullptr);

        auto *reg = wl_display_get_registry(m_display);
        static const wl_registry_listener listener = {
            wrapInterface(&wl_platform_display::global),
            wrapInterface(&wl_platform_display::global_remove),
        };
        wl_registry_add_listener(reg, &listener, this);

        wl_display_roundtrip(m_display);
        if (!m_compositor) {
            throw platform_exception("No wl_compositor global available.");
        }
        if (!m_shell) {
            throw platform_exception("No wl_shell global available.");
        }

        fmt::print("display {}\n",(void*)m_display);

        m_event_loop.add_fd(wl_display_get_fd(m_display), event_loop::type::readable, [this](event_loop::type) { read_events(); });
    }

    vk_instance create_vk_instance(const std::vector<std::string> &extensions)
    {
        std::vector<std::string> exts = extensions;
        exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        exts.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);

        return vk_instance(std::vector<std::string>(), exts);
    }

    wl_platform_window create_window(int w, int h, window::handler hnd)
    {
        return wl_platform_window(this, w, h, std::move(hnd));
    }

    void read_events()
    {
        if (wl_display_prepare_read(m_display) != -1) {
            wl_display_read_events(m_display);
        }
    }

    void run()
    {

        m_run = true;
        while (m_run) {
            wl_display_flush(m_display);
            wl_display_dispatch_pending(m_display);

            m_event_loop.loop_once();
        }
    }

    void quit()
    {
        m_run = false;
    }

    void global(wl_registry *reg, uint32_t id, const char *interface, uint32_t version)
    {
        #define registry_bind(reg, type, ver) \
            reinterpret_cast<type *>(wl_registry_bind(reg, id, &type##_interface, std::min(version, (uint32_t)ver)));

        auto iface = stringview(interface);
        if (iface == "wl_compositor") {
            m_compositor = registry_bind(reg, wl_compositor, 1);
        } else if (iface == "wl_shell") {
            m_shell = registry_bind(reg, wl_shell, 1);
        } else if (iface == "wl_seat") {
            wl_seat *s = registry_bind(reg, wl_seat, seat::supported_version);
            new seat(s);
        }
    }

    void global_remove(wl_registry *reg, uint32_t id)
    {
    }

    void schedule(const std::function<void ()> &run)
    {
        m_event_loop.add_idle(run);
//         m_event_loop.add_timer(1, run);
//         m_runlist.push_back(run);
    }

private:
    wl_display *m_display;
    wl_compositor *m_compositor;
    wl_shell *m_shell;
    std::vector<std::function<void ()>> m_runlist;
    bool m_run;
    event_loop m_event_loop;

    friend class wl_platform_window;
};

REGISTER_PLATFORM(platform::wayland, wl_platform_display);



wl_platform_window::wl_platform_window(wl_platform_display *dpy, int, int, window::handler hnd)
                : m_winhnd(std::move(hnd))
                , m_display(dpy)
                , m_update(false)
                , m_frame_callback(nullptr)
{
    m_surface = wl_compositor_create_surface(dpy->m_compositor);
    wl_surface_add_listener(m_surface, nullptr, this);
    fmt::print("new wind {} {}\n",(void*)this,(void*)m_surface);
}

wl_platform_window::wl_platform_window(wl_platform_window &&w)
                  : m_winhnd(std::move(w.m_winhnd))
                  , m_display(w.m_display)
                  , m_surface(w.m_surface)
                  , m_shell_surface(w.m_shell_surface)
                  , m_update(w.m_update)
                  , m_frame_callback(w.m_frame_callback)
{
    wl_surface_set_user_data(m_surface, this);
    if (m_frame_callback) {
        wl_callback_set_user_data(m_frame_callback, this);
    }
}

void wl_platform_window::show()
{
    m_shell_surface = wl_shell_get_shell_surface(m_display->m_shell, m_surface);
    wl_shell_surface_set_toplevel(m_shell_surface);
}

vk_surface wl_platform_window::create_vk_surface(const vk_instance &instance, window &win)
{
    VkSurfaceKHR surface = 0;
    VkWaylandSurfaceCreateInfoKHR info = {
        VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, //type
        nullptr, //next
        0, //flags
        m_display->m_display, //display
        m_surface, //surface
    };
    VkResult res = vkCreateWaylandSurfaceKHR(instance.get_handle(), &info, nullptr, &surface);
    if (res != VK_SUCCESS) {
        throw platform_exception("Failed to create vulkan surface");
    }
    return vk_surface(instance, win, surface);
}

void wl_platform_window::update()
{
   if (!m_update) {
        m_update = true;
        if (!m_frame_callback) {
            m_display->schedule([this]() {
                m_update = false;
                m_winhnd.update(0);
            });
        }
   }
}

void wl_platform_window::prepare_swap()
{
    m_frame_callback = wl_surface_frame(m_surface);
    static const wl_callback_listener listener = {
        wrapInterface(&wl_platform_window::send_update),
    };
    wl_callback_add_listener(m_frame_callback, &listener, this);
}

void wl_platform_window::send_update(wl_callback *, uint32_t time)
{
    wl_callback_destroy(m_frame_callback);
    m_frame_callback = nullptr;
    if (m_update) {
        m_update = false;
        m_winhnd.update((double)time / 1000.);
    }
}



void pointer::enter(wl_pointer *, uint32_t serial, wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    m_window = wl_platform_window::from_surface(surface);
}

void pointer::leave(wl_pointer *, uint32_t serial, wl_surface *surface)
{
    m_window = nullptr;
}

void pointer::motion(wl_pointer *, uint32_t time, wl_fixed_t fx, wl_fixed_t fy)
{
    double x = wl_fixed_to_double(fx);
    double y = wl_fixed_to_double(fy);
    m_window->get_handler().mouse_motion(x, y);
}

void pointer::button(wl_pointer *, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    m_window->get_handler().mouse_button(state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void pointer::axis(wl_pointer *, uint32_t time, uint32_t axis, wl_fixed_t value)
{
}
