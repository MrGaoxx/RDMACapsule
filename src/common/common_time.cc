// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.	See file COPYING.
 *
 */

// For ceph_timespec
#include "common_time.h"

#include <chrono>
#include <map>

double Cycles::cycles_per_sec = 0.0;
timeval Cycles::now_tv;
timespec Cycles::now_ts;

namespace common {
using std::chrono::nanoseconds;
using std::chrono::seconds;

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::seconds;

template <typename Clock, typename std::enable_if<Clock::is_steady>::type*>
std::ostream& operator<<(std::ostream& m, const std::chrono::time_point<Clock>& t) {
    return m << std::fixed << std::chrono::duration<double>(t.time_since_epoch()).count() << 's';
}

template <typename Clock, typename std::enable_if<!Clock::is_steady>::type*>
std::ostream& operator<<(std::ostream& m, const std::chrono::time_point<Clock>& t) {
    m.setf(std::ios::right);
    char oldfill = m.fill();
    m.fill('0');
    // localtime.  this looks like an absolute time.
    //  conform to http://en.wikipedia.org/wiki/ISO_8601
    struct tm bdt;
    time_t tt = Clock::to_time_t(t);
    localtime_r(&tt, &bdt);
    char tz[32] = {0};
    strftime(tz, sizeof(tz), "%z", &bdt);
    m << std::setw(4) << (bdt.tm_year + 1900)  // 2007 -> '07'
      << '-' << std::setw(2) << (bdt.tm_mon + 1) << '-' << std::setw(2) << bdt.tm_mday << 'T' << std::setw(2) << bdt.tm_hour << ':' << std::setw(2)
      << bdt.tm_min << ':' << std::setw(2) << bdt.tm_sec << "." << std::setw(6)
      << duration_cast<microseconds>(t.time_since_epoch() % seconds(1)).count() << tz;
    m.fill(oldfill);
    m.unsetf(std::ios::right);
    return m;
}

template std::ostream& operator<< <mono_clock>(std::ostream& m, const mono_time& t);
template std::ostream& operator<< <real_clock>(std::ostream& m, const real_time& t);
template std::ostream& operator<< <coarse_mono_clock>(std::ostream& m, const coarse_mono_time& t);
template std::ostream& operator<< <coarse_real_clock>(std::ostream& m, const coarse_real_time& t);

std::string timespan_str(timespan t) {
    // FIXME: somebody pretty please make a version of this function
    // that isn't as lame as this one!
    uint64_t nsec = std::chrono::nanoseconds(t).count();
    std::ostringstream ss;
    if (nsec < 2'000'000'000) {
        ss << ((float)nsec / 1'000'000'000) << "s";
        return ss.str();
    }
    uint64_t sec = nsec / 1'000'000'000;
    if (sec < 120) {
        ss << sec << "s";
        return ss.str();
    }
    uint64_t min = sec / 60;
    if (min < 120) {
        ss << min << "m";
        return ss.str();
    }
    uint64_t hr = min / 60;
    if (hr < 48) {
        ss << hr << "h";
        return ss.str();
    }
    uint64_t day = hr / 24;
    if (day < 14) {
        ss << day << "d";
        return ss.str();
    }
    uint64_t wk = day / 7;
    if (wk < 12) {
        ss << wk << "w";
        return ss.str();
    }
    uint64_t mn = day / 30;
    if (mn < 24) {
        ss << mn << "M";
        return ss.str();
    }
    uint64_t yr = day / 365;
    ss << yr << "y";
    return ss.str();
}

std::string exact_timespan_str(timespan t) {
    uint64_t nsec = std::chrono::nanoseconds(t).count();
    uint64_t sec = nsec / 1'000'000'000;
    nsec %= 1'000'000'000;
    uint64_t yr = sec / (60 * 60 * 24 * 365);
    std::ostringstream ss;
    if (yr) {
        ss << yr << "y";
        sec -= yr * (60 * 60 * 24 * 365);
    }
    uint64_t mn = sec / (60 * 60 * 24 * 30);
    if (mn >= 3) {
        ss << mn << "mo";
        sec -= mn * (60 * 60 * 24 * 30);
    }
    uint64_t wk = sec / (60 * 60 * 24 * 7);
    if (wk >= 2) {
        ss << wk << "w";
        sec -= wk * (60 * 60 * 24 * 7);
    }
    uint64_t day = sec / (60 * 60 * 24);
    if (day >= 2) {
        ss << day << "d";
        sec -= day * (60 * 60 * 24);
    }
    uint64_t hr = sec / (60 * 60);
    if (hr >= 2) {
        ss << hr << "h";
        sec -= hr * (60 * 60);
    }
    uint64_t min = sec / 60;
    if (min >= 2) {
        ss << min << "m";
        sec -= min * 60;
    }
    if (sec || nsec) {
        if (nsec) {
            ss << (((float)nsec / 1'000'000'000) + sec) << "s";
        } else {
            ss << sec << "s";
        }
    }
    return ss.str();
}

}  // namespace common

namespace std {
template <typename Rep, typename Period>
ostream& operator<<(ostream& m, const chrono::duration<Rep, Period>& t) {
    if constexpr (chrono::treat_as_floating_point_v<Rep> || Period::den > 1) {
        using seconds_t = chrono::duration<float>;
        m << "{:.9}" << chrono::duration_cast<seconds_t>(t);
    } else {
        m << "{}" << t;
    }
    return m;
}

template ostream& operator<< <common::timespan::rep, common::timespan::period>(ostream&, const ::common::timespan&);

template ostream& operator<< <common::signedspan::rep, common::signedspan::period>(ostream&, const ::common::signedspan&);

template ostream& operator<< <chrono::seconds::rep, chrono::seconds::period>(ostream&, const chrono::seconds&);

template ostream& operator<< <chrono::milliseconds::rep, chrono::milliseconds::period>(ostream&, const chrono::milliseconds&);

}  // namespace std
