
#pragma once

#include <vector>
#include <functional>
#include <memory>

#include "utils.h"

class event_loop
{
public:
    class fd_event;

    enum class type {
        none = 0,
        readable = 1,
        writeable = 2,
        error = 4,
    };

    using notify_func = std::function<void (type t)>;
    using timer_func = std::function<void ()>;
    using idle_func = std::function<void ()>;

    event_loop();
    event_loop(const event_loop &) = delete;
    event_loop(event_loop &&);
    ~event_loop();

    void loop_once();

    void add_timer(int msecs, const timer_func &notify);
    fd_event *add_fd(int fd, type t, const notify_func &notify);
    void add_idle(const idle_func &notify);

    void remove_fd(fd_event *e);

private:
    int m_fd;
    std::vector<idle_func> m_idles;
};

FLAGS(event_loop::type)
