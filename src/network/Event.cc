// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "Event.h"

#include <fcntl.h>

#include <vector>

#include "EventEpoll.h"
#include "common/common.h"
#include "common/context.h"
#include "network/net_handler.h"

int pipe_cloexec(int pipefd[2], int flags) {
    if (pipe(pipefd) == -1) return -1;

    /*
     * The old-fashioned, race-condition prone way that we have to fall
     * back on if pipe2 does not exist.
     */
    if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) < 0) {
        goto fail;
    }

    if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) < 0) {
        goto fail;
    }

    return 0;
fail:
    int save_errno = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    return (errno = save_errno, -1);
}

class C_handle_notify : public EventCallback {
    EventCenter *center;
    Context *context;

   public:
    C_handle_notify(EventCenter *c, Context *cc) : center(c), context(cc) {}
    void do_request(uint64_t fd_or_id) override {
        char c[256];
        int r = 0;
        do {
            r = read(fd_or_id, c, sizeof(c));
            if (r < 0) {
                if (errno != EAGAIN) {
                    std::cout << typeid(this).name() << " : " << __func__ << " read notify pipe failed: " << cpp_strerror(errno) << std::endl;
                }
            }
        } while (r > 0);
    }
};

/**
 * Construct a Poller.
 *
 * \param center
 *      EventCenter object through which the poller will be invoked (defaults
 *      to the global #RAMCloud::center object).
 * \param pollerName
 *      Human readable name that can be printed out in debugging messages
 *      about the poller. The name of the superclass is probably sufficient
 *      for most cases.
 */
EventCenter::Poller::Poller(EventCenter *center, const std::string &name) : owner(center), poller_name(name), slot(owner->pollers.size()) {
    owner->pollers.push_back(this);
}

/**
 * Destroy a Poller.
 */
EventCenter::Poller::~Poller() {
    // Erase this Poller from the vector by overwriting it with the
    // poller that used to be the last one in the vector.
    //
    // Note: this approach is reentrant (it is safe to delete a
    // poller from a poller callback, which means that the poll
    // method is in the middle of scanning the list of all pollers;
    // the worst that will happen is that the poller that got moved
    // may not be invoked in the current scan).
    owner->pollers[slot] = owner->pollers.back();
    owner->pollers[slot]->slot = slot;
    owner->pollers.pop_back();
    slot = -1;
}

std::ostream &EventCenter::_event_prefix(std::ostream *_dout) {
    return std::cout << "Event(" << this << " nevent=" << nevent << " time_id=" << time_event_next_id << ").";
}

int EventCenter::init(int nevent, unsigned center_id, const std::string &type) {
    // can't init multi times
    kassert(this->nevent == 0);

    this->type = type;
    this->center_id = center_id;
    driver = new EpollDriver(context);

    if (!driver) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to create event driver " << std::endl;
        return -1;
    }

    int r = driver->init(this, nevent);
    if (r < 0) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to init event driver." << std::endl;
        return r;
    }

    file_events.resize(nevent);
    this->nevent = nevent;

    if (!driver->need_wakeup()) return 0;

    int fds[2];

    if (pipe_cloexec(fds, 0) < 0) {
        int e = errno;
        std::cout << typeid(this).name() << " : " << __func__ << " can't create notify pipe: " << cpp_strerror(e) << std::endl;
        return -e;
    }

    notify_receive_fd = fds[0];
    notify_send_fd = fds[1];

    r = Network::NetHandler::set_nonblock(notify_receive_fd);
    if (r < 0) {
        return r;
    }
    r = Network::NetHandler::set_nonblock(notify_send_fd);
    if (r < 0) {
        return r;
    }

    return r;
}

EventCenter::~EventCenter() {
    {
        std::lock_guard<std::mutex> l(external_lock);
        while (!external_events.empty()) {
            EventCallbackRef e = external_events.front();
            if (e) e->do_request(0);
            external_events.pop_front();
        }
    }
    time_events.clear();
    // assert(time_events.empty());

    if (notify_receive_fd >= 0) close(notify_receive_fd);
    if (notify_send_fd >= 0) close(notify_send_fd);

    delete driver;
    if (notify_handler) delete notify_handler;
}

void EventCenter::set_owner() {
    owner = pthread_self();
    std::cout << typeid(this).name() << " : " << __func__ << " center_id=" << center_id << " owner=" << owner << std::endl;
    if (!global_centers) {
        global_centers = &context->m_associateCenters;
        kassert(global_centers);
        global_centers->centers[center_id] = this;
        if (driver->need_wakeup()) {
            notify_handler = new C_handle_notify(this, context);
            create_file_event(notify_receive_fd, EVENT_READABLE, notify_handler);
            // kassert(r == 0);
        }
    }
}

int EventCenter::create_file_event(int fd, int mask, EventCallbackRef ctxt) {
    kassert(in_thread());
    int r = 0;
    if (fd >= nevent) {
        int new_size = nevent << 2;
        while (fd >= new_size) new_size <<= 2;
        std::cout << typeid(this).name() << " : " << __func__ << " event count exceed " << nevent << ", expand to " << new_size << std::endl;
        r = driver->resize_events(new_size);
        if (r < 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " event count is exceed." << std::endl;
            return -ERANGE;
        }
        file_events.resize(new_size);
        nevent = new_size;
    }

    EventCenter::FileEvent *event = _get_file_event(fd);
    // std::cout << typeid(this).name() << " : " << __func__ << " create event started fd=" << fd << " mask=" << mask << " original mask is " <<
    // event->mask << std::endl;
    if (event->mask == mask) return 0;

    r = driver->add_event(fd, event->mask, mask);
    if (r < 0) {
        // Actually we don't allow any failed error code, caller doesn't prepare to
        // handle error status. So now we need to assert failure here. In practice,
        // add_event shouldn't report error, otherwise it must be a innermost bug!
        std::cout << typeid(this).name() << " : " << __func__ << " add event failed, ret=" << r << " fd=" << fd << " mask=" << mask
                  << " original mask is " << event->mask << std::endl;
        abort();
        return r;
    }

    event->mask |= mask;
    if (mask & EVENT_READABLE) {
        event->read_cb = ctxt;
    }
    if (mask & EVENT_WRITABLE) {
        event->write_cb = ctxt;
    }
    // std::cout << typeid(this).name() << " : " << __func__ << " create event end fd=" << fd << " mask=" << mask << " current mask is " <<
    // event->mask<< std::endl;
    return 0;
}

void EventCenter::delete_file_event(int fd, int mask) {
    kassert(in_thread() && fd >= 0);
    if (fd >= nevent) {
        std::cout << typeid(this).name() << " : " << __func__ << " delete event fd=" << fd << " is equal or greater than nevent=" << nevent
                  << "mask=" << mask << std::endl;
        return;
    }
    EventCenter::FileEvent *event = _get_file_event(fd);
    // std::cout << typeid(this).name() << " : " << __func__ << " delete event started fd=" << fd << " mask=" << mask << " original mask is "
    //<< event->mask << std::endl;
    if (!event->mask) return;

    int r = driver->del_event(fd, event->mask, mask);
    if (r < 0) {
        // see create_file_event
        abort();
    }

    if (mask & EVENT_READABLE && event->read_cb) {
        event->read_cb = nullptr;
    }
    if (mask & EVENT_WRITABLE && event->write_cb) {
        event->write_cb = nullptr;
    }

    event->mask = event->mask & (~mask);
    // std::cout << typeid(this).name() << " : " << __func__ << " delete event end fd=" << fd << " mask=" << mask << " current mask is " <<
    // event->mask<< std::endl;
}

uint64_t EventCenter::create_time_event(uint64_t microseconds, EventCallbackRef ctxt) {
    kassert(in_thread());
    uint64_t id = time_event_next_id++;

    std::cout << typeid(this).name() << " : " << __func__ << " id=" << id << " trigger after " << microseconds << "us" << std::endl;
    EventCenter::TimeEvent event;
    clock_type::time_point expire = clock_type::now() + std::chrono::microseconds(microseconds);
    event.id = id;
    event.time_cb = ctxt;
    std::multimap<clock_type::time_point, TimeEvent>::value_type s_val(expire, event);
    auto it = time_events.insert(std::move(s_val));
    event_map[id] = it;

    return id;
}

void EventCenter::delete_time_event(uint64_t id) {
    kassert(in_thread());
    std::cout << typeid(this).name() << " : " << __func__ << " id=" << id << std::endl;
    if (id >= time_event_next_id || id == 0) return;

    auto it = event_map.find(id);
    if (it == event_map.end()) {
        std::cout << typeid(this).name() << " : " << __func__ << " id=" << id << " not found" << std::endl;
        return;
    }

    time_events.erase(it->second);
    event_map.erase(it);
}

void EventCenter::wakeup() {
    // No need to wake up since we never sleep
    if (!pollers.empty() || !driver->need_wakeup()) return;

    std::cout << typeid(this).name() << " : " << __func__ << std::endl;
    char buf = 'c';
    // wake up "event_wait"
    int n = write(notify_send_fd, &buf, sizeof(buf));
    if (n < 0) {
        if (errno != EAGAIN) {
            std::cout << typeid(this).name() << " : " << __func__ << " write notify pipe failed: " << cpp_strerror(errno) << std::endl;
            abort();
        }
    }
}

int EventCenter::process_time_events() {
    int processed = 0;
    clock_type::time_point now = clock_type::now();
    std::cout << typeid(this).name() << " : " << __func__ << " cur time is " << now << std::endl;

    while (!time_events.empty()) {
        auto it = time_events.begin();
        if (now >= it->first) {
            TimeEvent &e = it->second;
            EventCallbackRef cb = e.time_cb;
            uint64_t id = e.id;
            time_events.erase(it);
            event_map.erase(id);
            std::cout << typeid(this).name() << " : " << __func__ << " process time event: id=" << id << std::endl;
            processed++;
            cb->do_request(id);
        } else {
            break;
        }
    }

    return processed;
}

int EventCenter::process_events(unsigned timeout_microseconds, common::timespan *working_dur) {
    struct timeval tv;
    int numevents;
    bool trigger_time = false;
    auto now = clock_type::now();
    clock_type::time_point end_time = now + std::chrono::microseconds(timeout_microseconds);

    auto it = time_events.begin();
    if (it != time_events.end() && end_time >= it->first) {
        trigger_time = true;
        end_time = it->first;

        if (end_time > now) {
            timeout_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end_time - now).count();
        } else {
            timeout_microseconds = 0;
        }
    }

    bool blocking = pollers.empty() && !external_num_events.load();
    if (!blocking) timeout_microseconds = 0;
    tv.tv_sec = timeout_microseconds / 1000000;
    tv.tv_usec = timeout_microseconds % 1000000;

    // std::cout << typeid(this).name() << " : " << __func__ << " wait second " << tv.tv_sec << " usec " << tv.tv_usec << std::endl;
    std::vector<FiredFileEvent> fired_events;
    numevents = driver->event_wait(fired_events, &tv);
    auto working_start = common::mono_clock::now();
    for (int event_id = 0; event_id < numevents; event_id++) {
        int rfired = 0;
        FileEvent *event;
        EventCallbackRef cb;
        event = _get_file_event(fired_events[event_id].fd);

        /* note the event->mask & mask & ... code: maybe an already processed
         * event removed an element that fired and we still didn't
         * processed, so we check if the event is still valid. */
        if (event->mask & fired_events[event_id].mask & EVENT_READABLE) {
            rfired = 1;
            cb = event->read_cb;
            cb->do_request(fired_events[event_id].fd);
        }

        if (event->mask & fired_events[event_id].mask & EVENT_WRITABLE) {
            if (!rfired || event->read_cb != event->write_cb) {
                cb = event->write_cb;
                cb->do_request(fired_events[event_id].fd);
            }
        }

        // std::cout << typeid(this).name() << " : " << __func__ << " event_wq process is " << fired_events[event_id].fd << " mask is " <<
        // fired_events[event_id].mask << std::endl;
    }

    if (trigger_time) numevents += process_time_events();

    if (external_num_events.load()) {
        external_lock.lock();
        std::deque<EventCallbackRef> cur_process;
        cur_process.swap(external_events);
        external_num_events.store(0);
        external_lock.unlock();
        numevents += cur_process.size();
        while (!cur_process.empty()) {
            EventCallbackRef e = cur_process.front();
            // std::cout << typeid(this).name() << " : " << __func__ << " do " << e << std::endl;
            e->do_request(0);
            cur_process.pop_front();
        }
    }

    if (!numevents && !blocking) {
        for (uint32_t i = 0; i < pollers.size(); i++) numevents += pollers[i]->poll();
    }

    if (working_dur) *working_dur = common::mono_clock::now() - working_start;
    return numevents;
}

void EventCenter::dispatch_event_external(EventCallbackRef e) {
    uint64_t num = 0;
    {
        std::lock_guard lock{external_lock};
        if (external_num_events > 0 && *external_events.rbegin() == e) {
            return;
        }
        external_events.push_back(e);
        num = ++external_num_events;
    }
    if (num == 1 && !in_thread()) wakeup();

    // std::cout << typeid(this).name() << " : " << __func__ << " " << e << " pending " << num << std::endl;
}
