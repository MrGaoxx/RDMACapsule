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

#include "EventSelect.h"

#include <sys/select.h>
#include <unistd.h>

#include "common/common.h"
#include "common/context.h"

#undef dout_prefix
#define dout_prefix *_dout << "SelectDriver."

int SelectDriver::init(EventCenter *c, int nevent) {
    std::cout << "Select isn't suitable for production env, just avoid "
              << "compiling error or special purpose" << std::endl;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    max_fd = 0;
    return 0;
}

int SelectDriver::add_event(int fd, int cur_mask, int add_mask) {
    std::cout << __func__ << " add event to fd=" << fd << " mask=" << add_mask << std::endl;

    int mask = cur_mask | add_mask;
    if (mask & EVENT_READABLE) FD_SET(fd, &rfds);
    if (mask & EVENT_WRITABLE) FD_SET(fd, &wfds);
    if (fd > max_fd) max_fd = fd;

    return 0;
}

int SelectDriver::del_event(int fd, int cur_mask, int delmask) {
    std::cout << __func__ << " del event fd=" << fd << " cur mask=" << cur_mask << std::endl;

    if (delmask & EVENT_READABLE) FD_CLR(fd, &rfds);
    if (delmask & EVENT_WRITABLE) FD_CLR(fd, &wfds);
    return 0;
}

int SelectDriver::resize_events(int newsize) { return 0; }

int SelectDriver::event_wait(std::vector<FiredFileEvent> &fired_events, struct timeval *tvp) {
    int retval, numevents = 0;

    memcpy(&_rfds, &rfds, sizeof(fd_set));
    memcpy(&_wfds, &wfds, sizeof(fd_set));

    retval = select(max_fd + 1, &_rfds, &_wfds, NULL, tvp);
    if (retval > 0) {
        for (int j = 0; j <= max_fd; j++) {
            int mask = 0;
            struct FiredFileEvent fe;
            if (FD_ISSET(j, &_rfds)) mask |= EVENT_READABLE;
            if (FD_ISSET(j, &_wfds)) mask |= EVENT_WRITABLE;
            if (mask) {
                fe.fd = j;
                fe.mask = mask;
                fired_events.push_back(fe);
                numevents++;
            }
        }
    }
    return numevents;
}
