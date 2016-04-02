
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>

#include "vk.h"
#include "display.h"
#include "event_loop.h"

class xcb_platform_display;

class xcb_platform_window
{
public:
    explicit xcb_platform_window(xcb_platform_display *dpy, int w, int h, window::handler hnd);
    xcb_platform_window(const xcb_platform_window &) = delete;
    xcb_platform_window(xcb_platform_window &&);

    void show();
    vk_surface create_vk_surface(const vk_instance &instance, const window &win);
    void update();

    void press_event(xcb_button_press_event_t *e);
    void release_event(xcb_button_release_event_t *e);

private:
    xcb_platform_display *m_display;
    xcb_window_t m_xcb_window;
    xcb_visualid_t m_root_visual;
    window::handler m_winhnd;
    bool m_update;
};

class xcb_platform_display
{
public:
    xcb_platform_display()
    {
    }

    void init()
    {
        m_connection = xcb_connect(0, 0);
        if (xcb_connection_has_error(m_connection)) {
            throw platform_exception("Failed to connect to the X display");
        }

        int fd = xcb_get_file_descriptor(m_connection);
        m_event_loop.add_fd(fd, event_loop::type::readable, [this](event_loop::type) { dispatch(); });
    }

    vk_instance create_vk_instance(const std::vector<std::string> &extensions)
    {
        std::vector<std::string> exts = extensions;
        exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        exts.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

        return vk_instance(std::vector<std::string>(), exts);
    }

    xcb_platform_window create_window(int w, int h, window::handler hnd)
    {
        return xcb_platform_window(this, w, h, std::move(hnd));
    }

    void dispatch()
    {
        int connection_error = xcb_connection_has_error(m_connection);
        if (connection_error) {
            throw platform_exception(fmt::format("The X11 connection broke (error {}). Did the X11 server die?", connection_error));
        }

        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(m_connection))) {
            fmt::print("event {}\n",(int)event->response_type);
            switch (event->response_type & ~0x80) {
            case XCB_BUTTON_PRESS: {
                xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
                window(press->event)->press_event(press);
                break;
            }
            case XCB_BUTTON_RELEASE: {
                xcb_button_release_event_t *release = (xcb_button_release_event_t *)event;
                window(release->event)->release_event(release);
                break;
            }
            default:
                break;
            }

            free(event);
        }


        xcb_flush(m_connection);
    }

    void run()
    {
        m_run = true;
        while (m_run) {
            dispatch();

            m_event_loop.loop_once();
        }
    }

    void quit()
    {
        m_run = false;
    }

    xcb_platform_window *window(xcb_window_t id)
    {
        auto it = m_windows.find(id);
        if (it == m_windows.end()) {
            throw platform_exception(fmt::format("No window found with the given Xid: {}\n", id));
        }
        return it->second;
    }

private:
    xcb_connection_t *m_connection;
    bool m_run;
    event_loop m_event_loop;
    std::unordered_map<xcb_window_t, xcb_platform_window *> m_windows;

    friend class xcb_platform_window;
};

REGISTER_PLATFORM(platform::xcb, xcb_platform_display);




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


xcb_platform_window::xcb_platform_window(xcb_platform_display *dpy, int width, int height, window::handler hnd)
                   : m_display(dpy)
                   , m_winhnd(std::move(hnd))
                   , m_update(false)
{
//     static const char title[] = "Vulkan Test";

    m_xcb_window = xcb_generate_id(m_display->m_connection);

   uint32_t window_values[] = {
      XCB_EVENT_MASK_EXPOSURE |
      XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_KEY_PRESS |
      XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
   };

   xcb_screen_iterator_t iter = xcb_setup_roots_iterator(xcb_get_setup(m_display->m_connection));

   xcb_create_window(m_display->m_connection,
                     XCB_COPY_FROM_PARENT,
                     m_xcb_window,
                     iter.data->root,
                     0, 0,
                     width,
                     height,
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

    m_display->m_windows[m_xcb_window] = this;
}

xcb_platform_window::xcb_platform_window(xcb_platform_window &&w)
                    : m_display(w.m_display)
                    , m_xcb_window(w.m_xcb_window)
                    , m_root_visual(w.m_root_visual)
                    , m_winhnd(std::move(w.m_winhnd))
                    , m_update(w.m_update)
{
    m_display->m_windows[m_xcb_window] = this;
}

void xcb_platform_window::show()
{
   xcb_map_window(m_display->m_connection, m_xcb_window);

   xcb_flush(m_display->m_connection);
}

void xcb_platform_window::update()
{
    if (!m_update) {
       m_update = true;
       m_display->m_event_loop.add_idle([this]() {
           m_update = false;
           m_winhnd.update();
       });
   }
}

vk_surface xcb_platform_window::create_vk_surface(const vk_instance &instance, const window &window)
{
    VkSurfaceKHR surface = 0;
    VkXcbSurfaceCreateInfoKHR info = {
        VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR, nullptr, 0,
        m_display->m_connection,
        m_xcb_window,
    };
    VkResult res = vkCreateXcbSurfaceKHR(instance.get_handle(), &info, nullptr, &surface);
    if (res != VK_SUCCESS) {
        throw platform_exception("Failed to create vulkan surface");
    }
    return vk_surface(instance, window, surface);
}

void xcb_platform_window::press_event(xcb_button_press_event_t *e)
{
   m_winhnd.mouse_button(true);
}

void xcb_platform_window::release_event(xcb_button_release_event_t *e)
{
   m_winhnd.mouse_button(false);
}
