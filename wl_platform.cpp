
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>

#include "vk.h"
#include "display.h"

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

class wl_platform_window
{
public:
    explicit wl_platform_window(wl_platform_display *dpy, int w, int h);

    void show();
    vk_surface create_vk_surface(const vk_instance &instance, const window &win);

private:
    wl_platform_display *m_display;
    wl_surface *m_surface;
    wl_shell_surface *m_shell_surface;
};

class wl_platform_display
{
public:
    explicit wl_platform_display()
        : m_compositor(nullptr)
        , m_shell(nullptr)
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
    }

    vk_instance create_vk_instance(const std::vector<std::string> &extensions)
    {
        std::vector<std::string> exts = extensions;
        exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        exts.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);

        return vk_instance(std::vector<std::string>(), exts);
    }

    wl_platform_window create_window(int w, int h)
    {
        return wl_platform_window(this, w, h);
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
        }
    }

    void global_remove(wl_registry *reg, uint32_t id)
    {
    }

private:
    wl_display *m_display;
    wl_compositor *m_compositor;
    wl_shell *m_shell;

    friend class wl_platform_window;
};

REGISTER_PLATFORM(platform::wayland, wl_platform_display);



wl_platform_window::wl_platform_window(wl_platform_display *dpy, int, int)
                : m_display(dpy)
{
    m_surface = wl_compositor_create_surface(dpy->m_compositor);
}

void wl_platform_window::show()
{
    m_shell_surface = wl_shell_get_shell_surface(m_display->m_shell, m_surface);
    wl_shell_surface_set_toplevel(m_shell_surface);
}

vk_surface wl_platform_window::create_vk_surface(const vk_instance &instance, const window &win)
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
    return vk_surface(win, surface);
}
