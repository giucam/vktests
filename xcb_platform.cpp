
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>

#include "vk.h"
#include "display.h"

class xcb_platform_display;

class xcb_platform_window : public platform_window
{
public:
    explicit xcb_platform_window(xcb_platform_display *dpy, const std::weak_ptr<window> &win);

    void show() override;
    std::shared_ptr<vk_surface> create_vk_surface(const weak_ptr<vk_instance> &instance, vk_device *dev) override;

private:
    xcb_platform_display *m_display;
    xcb_window_t m_xcb_window;
    xcb_visualid_t m_root_visual;
};

class xcb_platform_display : public platform_display
{
public:
    explicit xcb_platform_display(display *dpy)
        : platform_display(dpy)
    {
        m_connection = xcb_connect(0, 0);
        if (xcb_connection_has_error(m_connection)) {
            throw platform_exception("Failed to connect to the X display");
        }
    }

    std::shared_ptr<vk_instance> create_vk_instance(const std::vector<std::string> &extensions) override
    {
        std::vector<std::string> exts = extensions;
        exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        exts.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

        return std::make_shared<vk_instance>(std::vector<std::string>(), exts);
    }

    unique_ptr<platform_window> create_window(const std::weak_ptr<window> &win) override
    {
        return make_unique<xcb_platform_window>(this, win);
    }

private:
    xcb_connection_t *m_connection;

    friend class xcb_platform_window;
};

static bool a = display::register_platform(platform::xcb, [](display *dpy) -> unique_ptr<platform_display> {
    return make_unique<xcb_platform_display>(dpy);
});



static xcb_atom_t
get_atom(struct xcb_connection_t *conn, const char *name)
{
   xcb_intern_atom_cookie_t cookie;
   xcb_intern_atom_reply_t *reply;
   xcb_atom_t atom;

   cookie = xcb_intern_atom(conn, 0, strlen(name), name);
   reply = xcb_intern_atom_reply(conn, cookie, NULL);
   if (reply)
      atom = reply->atom;
   else
      atom = XCB_NONE;

   free(reply);
   return atom;
}


xcb_platform_window::xcb_platform_window(xcb_platform_display *dpy, const std::weak_ptr<window> &win)
                   : platform_window(win)
                   , m_display(dpy)
{
//     static const char title[] = "Vulkan Test";

    m_xcb_window = xcb_generate_id(m_display->m_connection);

   uint32_t window_values[] = {
      XCB_EVENT_MASK_EXPOSURE |
      XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_KEY_PRESS
   };

   xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(m_display->m_connection));

   xcb_create_window(m_display->m_connection,
                     XCB_COPY_FROM_PARENT,
                     m_xcb_window,
                     iter.data->root,
                     0, 0,
                     m_window.lock()->get_width(),
                     m_window.lock()->get_height(),
                     0,
                     XCB_WINDOW_CLASS_INPUT_OUTPUT,
                     iter.data->root_visual,
                     XCB_CW_EVENT_MASK, window_values);
    m_root_visual = iter.data->root_visual;

    auto atom_wm_protocols = get_atom(m_display->m_connection, "WM_PROTOCOLS");
    auto atom_wm_delete_window = get_atom(m_display->m_connection, "WM_DELETE_WINDOW");
    xcb_change_property(m_display->m_connection,
                        XCB_PROP_MODE_REPLACE,
                        m_xcb_window,
                        atom_wm_protocols,
                        XCB_ATOM_ATOM,
                        32,
                        1, &atom_wm_delete_window);
//
//    xcb_change_property(vc->xcb.conn,
//                        XCB_PROP_MODE_REPLACE,
//                        vc->xcb.window,
//                        get_atom(vc->xcb.conn, "_NET_WM_NAME"),
//                        get_atom(vc->xcb.conn, "UTF8_STRING"),
//                        8, // sizeof(char),
//                        strlen(title), title);
//
}

void xcb_platform_window::show()
{
   xcb_map_window(m_display->m_connection, m_xcb_window);

   xcb_flush(m_display->m_connection);
}

std::shared_ptr<vk_surface> xcb_platform_window::create_vk_surface(const std::weak_ptr<vk_instance> &instance, vk_device *device)
{
    if (!vkGetPhysicalDeviceXcbPresentationSupportKHR(device->get_physical_device()->get_handle(), 0, m_display->m_connection, m_root_visual)) {
        throw platform_exception("Presentation is not supported for this surface.");
    }

    VkSurfaceKHR surface = 0;
    VkXcbSurfaceCreateInfoKHR info = {
        VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, nullptr, 0,
        m_display->m_connection,
        m_xcb_window,
    };
    VkResult res = vkCreateXcbSurfaceKHR(instance.lock()->get_handle(), &info, nullptr, &surface);
    if (res != VK_SUCCESS) {
        throw platform_exception("Failed to create vulkan surface");
    }
    return make_shared<vk_surface>(m_window.lock(), surface);
}
