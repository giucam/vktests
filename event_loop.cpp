
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "event_loop.h"
#include "format.h"
#include "display.h"

struct event_loop::fd_event {
    fd_event(int f, const event_loop::notify_func &n) : fd(f), notify(n) {}
    int fd;
    event_loop::notify_func notify;
    epoll_event event;
};

event_loop::event_loop()
          : m_fd(epoll_create1(0))
{
    if (m_fd < 0) {
        throw platform_exception(fmt::format("Failed to create epoll fd: {}\n", strerror(errno)));
    }
}

event_loop::event_loop(event_loop &&e)
          : m_fd(e.m_fd)
          , m_idles(std::move(e.m_idles))
{
    e.m_fd = -1;
}

event_loop::~event_loop()
{
    close(m_fd);
}

void event_loop::loop_once()
{
    std::vector<idle_func> idles;
    idles.swap(m_idles);
    for (const auto &f: idles) {
        f();
    }

    epoll_event events[32];
    int num_events = epoll_wait(m_fd, events, 32, m_idles.empty() ? -1 : 0);

    if (num_events < 0) {
        throw platform_exception(fmt::format("epoll_wait failed: {}\n", strerror(errno)));
    }

    for (int i = 0; i < num_events; ++i) {
        fd_event *e = static_cast<fd_event *>(events[i].data.ptr);
        auto t = type::none;
        if (events[i].events & EPOLLIN) {
            t |= type::readable;
        }
        if (events[i].events & EPOLLOUT) {
            t |= type::writeable;
        }
        if (events[i].events & EPOLLHUP) {
            t |= type::error;
        }
        e->notify(t);
    }
}

void event_loop::add_timer(int msecs, const timer_func &notify)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    const struct itimerspec its = {
        .it_interval = {
            1000000, 0,
        },
        .it_value = {
            .tv_sec = msecs / 1000,
            .tv_nsec = (msecs % 1000) ,
        },
    };
    int ret = timerfd_settime(fd, 0, &its, nullptr);
    if (ret < 0) {
        throw platform_exception(fmt::format("timerfd_settime failed: {}\n", strerror(errno)));
    }

    class timer
    {
    public:
        timer() {}
        timer(const timer &) = delete;
        event_loop *loop;
        fd_event *event;
        int fd;
        timer_func notify;

        void operator()(type) const
        {
            uint64_t expirations;
            read(fd, &expirations, sizeof(expirations));
            notify();
            loop->remove_fd(event);
            close(fd);
        }
    };

    auto tim = new timer;
    tim->loop = this;
    tim->fd = fd;
    tim->notify = notify;
    tim->event = add_fd(fd, type::readable, [tim](type t) { (*tim)(t); });
}

event_loop::fd_event *event_loop::add_fd(int fd, type t, const notify_func &notify)
{
    auto event = new fd_event(fd, notify);

    event->event.events = 0;
    if (t & type::readable) {
        event->event.events |= EPOLLIN;
    }
    if (t & type::writeable) {
        event->event.events |= EPOLLOUT;
    }
    event->event.data.fd = fd;
    event->event.data.ptr = event;
    int ret = epoll_ctl(m_fd, EPOLL_CTL_ADD, fd, &event->event);
    if (ret < 0) {
        throw platform_exception(fmt::format("epoll_ctl failed: {}\n", strerror(errno)));
    }
    return event;
}

void event_loop::add_idle(const idle_func &notify)
{
    m_idles.push_back(notify);
}

void event_loop::remove_fd(fd_event *fd)
{
    int ret = epoll_ctl(m_fd, EPOLL_CTL_DEL, fd->fd, nullptr);
    if (ret < 0) {
        throw platform_exception(fmt::format("epoll_ctl failed: {}\n", strerror(errno)));
    }
    delete fd;
}
