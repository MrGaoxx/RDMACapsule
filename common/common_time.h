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

#ifndef COMMON_TIME_H
#define COMMON_TIME_H
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

#include "common/buffer.h"
#include "common/formatter.h"
struct timespec;

namespace common {
// Currently we use a 64-bit count of nanoseconds.

// We could, if we wished, use a struct holding a uint64_t count
// of seconds and a uint32_t count of nanoseconds.

// At least this way we can change it to something else if we
// want.
using rep = uint64_t;
// Like the above but signed.
using signed_rep = int64_t;

// duration is the concrete time representation for our code in the
// case that we are only interested in durations between now and the
// future. Using it means we don't have to have EVERY function that
// deals with a duration be a template. We can do so for user-facing
// APIs, however.
using timespan = std::chrono::duration<rep, std::nano>;

// Similar to the above but for durations that can specify
// differences between now and a time point in the past.
using signedspan = std::chrono::duration<signed_rep, std::nano>;

template <typename Duration>
struct timeval to_timeval(Duration d) {
    struct timeval tv;
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(d);
    tv.tv_sec = sec.count();
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(d - sec);
    tv.tv_usec = usec.count();
    return tv;
}

// We define our own clocks so we can have our choice of all time
// sources supported by the operating system. With the standard
// library the resolution and cost are unspecified. (For example,
// the libc++ system_clock class gives only microsecond
// resolution.)

// One potential issue is that we should accept system_clock
// timepoints in user-facing APIs alongside (or instead of)
// common::real_clock times.

// High-resolution real-time clock
class real_clock {
   public:
    typedef timespan duration;
    typedef duration::rep rep;
    typedef duration::period period;
    // The second template parameter defaults to the clock's duration
    // type.
    typedef std::chrono::time_point<real_clock> time_point;
    static constexpr const bool is_steady = false;

    static time_point now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return from_timespec(ts);
    }

    static bool is_zero(const time_point& t) { return (t == time_point::min()); }

    static time_point zero() { return time_point::min(); }

    // Allow conversion to/from any clock with the same interface as
    // std::chrono::system_clock)
    template <typename Clock, typename Duration>
    static time_point to_system_time_point(const std::chrono::time_point<Clock, Duration>& t) {
        return time_point(seconds(Clock::to_time_t(t)) + std::chrono::duration_cast<duration>(t.time_since_epoch() % std::chrono::seconds(1)));
    }
    template <typename Clock, typename Duration>
    static std::chrono::time_point<Clock, Duration> to_system_time_point(const time_point& t) {
        return (Clock::from_time_t(to_time_t(t)) + std::chrono::duration_cast<Duration>(t.time_since_epoch() % std::chrono::seconds(1)));
    }

    static time_t to_time_t(const time_point& t) noexcept { return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count(); }
    static time_point from_time_t(const time_t& t) noexcept { return time_point(std::chrono::seconds(t)); }

    static void to_timespec(const time_point& t, struct timespec& ts) {
        ts.tv_sec = to_time_t(t);
        ts.tv_nsec = (t.time_since_epoch() % std::chrono::seconds(1)).count();
    }
    static struct timespec to_timespec(const time_point& t) {
        struct timespec ts;
        to_timespec(t, ts);
        return ts;
    }
    static time_point from_timespec(const struct timespec& ts) {
        return time_point(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
    }

    static void to_timespec(const time_point& t, struct timespec& ts);
    static struct timespec to_timespec(const time_point& t);
    static time_point from_timespec(const struct timespec& ts);

    static void to_timeval(const time_point& t, struct timeval& tv) {
        tv.tv_sec = to_time_t(t);
        tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch() % std::chrono::seconds(1)).count();
    }
    static struct timeval to_timeval(const time_point& t) {
        struct timeval tv;
        to_timeval(t, tv);
        return tv;
    }
    static time_point from_timeval(const struct timeval& tv) {
        return time_point(std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec));
    }

    static double to_double(const time_point& t) { return std::chrono::duration<double>(t.time_since_epoch()).count(); }
    static time_point from_double(const double d) { return time_point(std::chrono::duration_cast<duration>(std::chrono::duration<double>(d))); }
};

// Low-resolution but preusmably faster real-time clock
class coarse_real_clock {
   public:
    typedef timespan duration;
    typedef duration::rep rep;
    typedef duration::period period;
    // The second template parameter defaults to the clock's duration
    // type.
    typedef std::chrono::time_point<coarse_real_clock> time_point;
    static constexpr const bool is_steady = false;

    static time_point now() noexcept {
        struct timespec ts;
#if defined(CLOCK_REALTIME_COARSE)
        // Linux systems have _COARSE clocks.
        clock_gettime(CLOCK_REALTIME_COARSE, &ts);
#elif defined(CLOCK_REALTIME_FAST)
        // BSD systems have _FAST clocks.
        clock_gettime(CLOCK_REALTIME_FAST, &ts);
#else
        // And if we find neither, you may wish to consult your system's
        // documentation.
#warning Falling back to CLOCK_REALTIME, may be slow.
        clock_gettime(CLOCK_REALTIME, &ts);
#endif
        return from_timespec(ts);
    }

    static bool is_zero(const time_point& t) { return (t == time_point::min()); }

    static time_point zero() { return time_point::min(); }

    static time_t to_time_t(const time_point& t) noexcept { return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count(); }
    static time_point from_time_t(const time_t t) noexcept { return time_point(std::chrono::seconds(t)); }

    static void to_timespec(const time_point& t, struct timespec& ts) {
        ts.tv_sec = to_time_t(t);
        ts.tv_nsec = (t.time_since_epoch() % std::chrono::seconds(1)).count();
    }
    static struct timespec to_timespec(const time_point& t) {
        struct timespec ts;
        to_timespec(t, ts);
        return ts;
    }
    static time_point from_timespec(const struct timespec& ts) {
        return time_point(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
    }

    static void to_timespec(const time_point& t, struct timespec& ts);
    static struct timespec to_timespec(const time_point& t);
    static time_point from_timespec(const struct timespec& ts);

    static void to_timeval(const time_point& t, struct timeval& tv) {
        tv.tv_sec = to_time_t(t);
        tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch() % std::chrono::seconds(1)).count();
    }
    static struct timeval to_timeval(const time_point& t) {
        struct timeval tv;
        to_timeval(t, tv);
        return tv;
    }
    static time_point from_timeval(const struct timeval& tv) {
        return time_point(std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec));
    }

    static double to_double(const time_point& t) { return std::chrono::duration<double>(t.time_since_epoch()).count(); }
    static time_point from_double(const double d) { return time_point(std::chrono::duration_cast<duration>(std::chrono::duration<double>(d))); }
};

// High-resolution monotonic clock
class mono_clock {
   public:
    typedef timespan duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<mono_clock> time_point;
    static constexpr const bool is_steady = true;

    static time_point now() noexcept {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return time_point(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
    }

    static bool is_zero(const time_point& t) { return (t == time_point::min()); }

    static time_point zero() { return time_point::min(); }
};

// Low-resolution but, I would hope or there's no point, faster
// monotonic clock
class coarse_mono_clock {
   public:
    typedef timespan duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef std::chrono::time_point<coarse_mono_clock> time_point;
    static constexpr const bool is_steady = true;

    static time_point now() noexcept {
        struct timespec ts;
#if defined(CLOCK_MONOTONIC_COARSE)
        // Linux systems have _COARSE clocks.
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
#elif defined(CLOCK_MONOTONIC_FAST)
        // BSD systems have _FAST clocks.
        clock_gettime(CLOCK_MONOTONIC_FAST, &ts);
#else
        // And if we find neither, you may wish to consult your system's
        // documentation.
#warning Falling back to CLOCK_MONOTONIC, may be slow.
        clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
        return time_point(std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec));
    }

    static bool is_zero(const time_point& t) { return (t == time_point::min()); }

    static time_point zero() { return time_point::min(); }
};

namespace time_detail {
// So that our subtractions produce negative spans rather than
// arithmetic underflow.
template <typename Rep1, typename Period1, typename Rep2, typename Period2>
inline auto difference(std::chrono::duration<Rep1, Period1> minuend, std::chrono::duration<Rep2, Period2> subtrahend) ->
    typename std::common_type<std::chrono::duration<typename std::make_signed<Rep1>::type, Period1>,
                              std::chrono::duration<typename std::make_signed<Rep2>::type, Period2>>::type {
    // Foo.
    using srep = typename std::common_type<std::chrono::duration<typename std::make_signed<Rep1>::type, Period1>,
                                           std::chrono::duration<typename std::make_signed<Rep2>::type, Period2>>::type;
    return srep(srep(minuend).count() - srep(subtrahend).count());
}

template <typename Clock, typename Duration1, typename Duration2>
inline auto difference(typename std::chrono::time_point<Clock, Duration1> minuend, typename std::chrono::time_point<Clock, Duration2> subtrahend) ->
    typename std::common_type<std::chrono::duration<typename std::make_signed<typename Duration1::rep>::type, typename Duration1::period>,
                              std::chrono::duration<typename std::make_signed<typename Duration2::rep>::type, typename Duration2::period>>::type {
    return difference(minuend.time_since_epoch(), subtrahend.time_since_epoch());
}
}  // namespace time_detail

// Please note that the coarse clocks are disjoint. You cannot
// subtract a real_clock timepoint from a coarse_real_clock
// timepoint as, from C++'s perspective, they are disjoint types.

// This is not necessarily bad. If I sample a mono_clock and then a
// coarse_mono_clock, the coarse_mono_clock's time could potentially
// be previous to the mono_clock's time (just due to differing
// resolution) which would be Incorrect.

// This is not horrible, though, since you can use an idiom like
// mono_clock::timepoint(coarsepoint.time_since_epoch()) to unwrap
// and rewrap if you know what you're doing.

// Actual wall-clock times
typedef real_clock::time_point real_time;
typedef coarse_real_clock::time_point coarse_real_time;

// Monotonic times should never be serialized or communicated
// between machines, since they are incomparable. Thus we also don't
// make any provision for converting between
// std::chrono::steady_clock time and common::mono_clock time.
typedef mono_clock::time_point mono_time;
typedef coarse_mono_clock::time_point coarse_mono_time;

template <typename Rep1, typename Ratio1, typename Rep2, typename Ratio2>
auto floor(const std::chrono::duration<Rep1, Ratio1>& duration, const std::chrono::duration<Rep2, Ratio2>& precision) ->
    typename std::common_type<std::chrono::duration<Rep1, Ratio1>, std::chrono::duration<Rep2, Ratio2>>::type {
    return duration - (duration % precision);
}

template <typename Rep1, typename Ratio1, typename Rep2, typename Ratio2>
auto ceil(const std::chrono::duration<Rep1, Ratio1>& duration, const std::chrono::duration<Rep2, Ratio2>& precision) ->
    typename std::common_type<std::chrono::duration<Rep1, Ratio1>, std::chrono::duration<Rep2, Ratio2>>::type {
    auto tmod = duration % precision;
    return duration - tmod + (tmod > tmod.zero() ? 1 : 0) * precision;
}

template <typename Clock, typename Duration, typename Rep, typename Ratio>
auto floor(const std::chrono::time_point<Clock, Duration>& timepoint, const std::chrono::duration<Rep, Ratio>& precision)
    -> std::chrono::time_point<Clock, typename std::common_type<Duration, std::chrono::duration<Rep, Ratio>>::type> {
    return std::chrono::time_point<Clock, typename std::common_type<Duration, std::chrono::duration<Rep, Ratio>>::type>(
        floor(timepoint.time_since_epoch(), precision));
}
template <typename Clock, typename Duration, typename Rep, typename Ratio>
auto ceil(const std::chrono::time_point<Clock, Duration>& timepoint, const std::chrono::duration<Rep, Ratio>& precision)
    -> std::chrono::time_point<Clock, typename std::common_type<Duration, std::chrono::duration<Rep, Ratio>>::type> {
    return std::chrono::time_point<Clock, typename std::common_type<Duration, std::chrono::duration<Rep, Ratio>>::type>(
        ceil(timepoint.time_since_epoch(), precision));
}

inline timespan make_timespan(const double d) { return std::chrono::duration_cast<timespan>(std::chrono::duration<double>(d)); }
inline std::optional<timespan> maybe_timespan(const double d) { return d ? std::make_optional(make_timespan(d)) : std::nullopt; }

template <typename Clock, typename std::enable_if<!Clock::is_steady>::type* = nullptr>
std::ostream& operator<<(std::ostream& m, const std::chrono::time_point<Clock>& t);
template <typename Clock, typename std::enable_if<Clock::is_steady>::type* = nullptr>
std::ostream& operator<<(std::ostream& m, const std::chrono::time_point<Clock>& t);

// The way std::chrono handles the return type of subtraction is not
// wonderful. The difference of two unsigned types SHOULD be signed.

inline signedspan operator-(real_time minuend, real_time subtrahend) { return time_detail::difference(minuend, subtrahend); }

inline signedspan operator-(coarse_real_time minuend, coarse_real_time subtrahend) { return time_detail::difference(minuend, subtrahend); }

inline signedspan operator-(mono_time minuend, mono_time subtrahend) { return time_detail::difference(minuend, subtrahend); }

inline signedspan operator-(coarse_mono_time minuend, coarse_mono_time subtrahend) { return time_detail::difference(minuend, subtrahend); }

// We could add specializations of time_point - duration and
// time_point + duration to assert on overflow, but I don't think we
// should.
inline timespan abs(signedspan z) { return z > signedspan::zero() ? std::chrono::duration_cast<timespan>(z) : timespan(-z.count()); }
inline timespan to_timespan(signedspan z) {
    if (z < signedspan::zero()) {
        // common_assert(z >= signedspan::zero());
        //  There is a kernel bug that seems to be triggering this assert.  We've
        //  seen it in:
        //    centos 8.1: 4.18.0-147.el8.x86_64
        //    debian 10.3: 4.19.0-8-amd64
        //    debian 10.1: 4.19.67-2+deb10u1
        //    ubuntu 18.04
        //  see bugs:
        //    https://tracker.common.com/issues/43365
        //    https://tracker.common.com/issues/44078
        z = signedspan::zero();
    }
    return std::chrono::duration_cast<timespan>(z);
}

std::string timespan_str(timespan t);
std::string exact_timespan_str(timespan t);

// detects presence of Clock::to_timespec() and from_timespec()
template <typename Clock, typename = std::void_t<>>
struct converts_to_timespec : std::false_type {};

template <typename Clock>
struct converts_to_timespec<Clock, std::void_t<decltype(Clock::from_timespec(Clock::to_timespec(std::declval<typename Clock::time_point>())))>>
    : std::true_type {};

template <typename Clock>
constexpr bool converts_to_timespec_v = converts_to_timespec<Clock>::value;

template <typename Rep, typename T>
static Rep to_seconds(T t) {
    return std::chrono::duration_cast<std::chrono::duration<Rep>>(t).count();
}

template <typename Rep, typename T>
static Rep to_microseconds(T t) {
    return std::chrono::duration_cast<std::chrono::duration<Rep, std::micro>>(t).count();
}

}  // namespace common

namespace std {
template <typename Rep, typename Period>
ostream& operator<<(ostream& m, const chrono::duration<Rep, Period>& t);
}

inline uint32_t cap_to_u32_max(uint64_t t) { return std::min(t, (uint64_t)std::numeric_limits<uint32_t>::max()); }

class utime_t {
   public:
    struct {
        uint32_t tv_sec, tv_nsec;
    } tv;

   public:
    bool is_zero() const { return (tv.tv_sec == 0) && (tv.tv_nsec == 0); }

    void normalize() {
        if (tv.tv_nsec > 1000000000ul) {
            tv.tv_sec = cap_to_u32_max(tv.tv_sec + tv.tv_nsec / (1000000000ul));
            tv.tv_nsec %= 1000000000ul;
        }
    }

    // cons
    utime_t() {
        tv.tv_sec = 0;
        tv.tv_nsec = 0;
    }
    utime_t(time_t s, int n) {
        tv.tv_sec = s;
        tv.tv_nsec = n;
        normalize();
    }
    utime_t(const struct timespec& v) { decode_timeval(&v); }
    utime_t(const struct timespec v) {
        // NOTE: this is used by common_clock_now() so should be kept
        // as thin as possible.
        tv.tv_sec = v.tv_sec;
        tv.tv_nsec = v.tv_nsec;
    }
    // conversion from common::real_time/coarse_real_time
    template <typename Clock, typename std::enable_if_t<common::converts_to_timespec_v<Clock>>* = nullptr>
    explicit utime_t(const std::chrono::time_point<Clock>& t) : utime_t(Clock::to_timespec(t)) {}  // forward to timespec ctor

    template <class Rep, class Period>
    explicit utime_t(const std::chrono::duration<Rep, Period>& dur) {
        using common_t = std::common_type_t<Rep, int>;
        tv.tv_sec = std::max<common_t>(std::chrono::duration_cast<std::chrono::seconds>(dur).count(), 0);
        tv.tv_nsec = std::max<common_t>((std::chrono::duration_cast<std::chrono::nanoseconds>(dur) % std::chrono::seconds(1)).count(), 0);
    }
#if defined(WITH_SEASTAR)
    explicit utime_t(const seastar::lowres_system_clock::time_point& t) {
        tv.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
        tv.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch() % std::chrono::seconds(1)).count();
    }
    explicit operator seastar::lowres_system_clock::time_point() const noexcept {
        using clock_t = seastar::lowres_system_clock;
        return clock_t::time_point{
            std::chrono::duration_cast<clock_t::duration>(std::chrono::seconds{tv.tv_sec} + std::chrono::nanoseconds{tv.tv_nsec})};
    }
#endif

    utime_t(const struct timeval& v) { set_from_timeval(&v); }
    utime_t(const struct timeval* v) { set_from_timeval(v); }
    void to_timespec(struct timespec* ts) const {
        ts->tv_sec = tv.tv_sec;
        ts->tv_nsec = tv.tv_nsec;
    }
    void set_from_double(double d) {
        tv.tv_sec = (uint32_t)trunc(d);
        tv.tv_nsec = (uint32_t)((d - (double)tv.tv_sec) * 1000000000.0);
    }

    common::real_time to_real_time() const {
        timespec ts;
        encode_timeval(&ts);
        return common::real_clock::from_timespec(ts);
    }

    // accessors
    time_t sec() const { return tv.tv_sec; }
    long usec() const { return tv.tv_nsec / 1000; }
    int nsec() const { return tv.tv_nsec; }

    // ref accessors/modifiers
    uint32_t& sec_ref() { return tv.tv_sec; }
    uint32_t& nsec_ref() { return tv.tv_nsec; }

    uint64_t to_nsec() const { return (uint64_t)tv.tv_nsec + (uint64_t)tv.tv_sec * 1000000000ull; }
    uint64_t to_msec() const { return (uint64_t)tv.tv_nsec / 1000000ull + (uint64_t)tv.tv_sec * 1000ull; }

    void copy_to_timeval(struct timeval* v) const {
        v->tv_sec = tv.tv_sec;
        v->tv_usec = tv.tv_nsec / 1000;
    }
    void set_from_timeval(const struct timeval* v) {
        tv.tv_sec = v->tv_sec;
        tv.tv_nsec = v->tv_usec * 1000;
    }
    void padding_check() { static_assert(sizeof(utime_t) == sizeof(tv.tv_sec) + sizeof(tv.tv_nsec), "utime_t have padding"); }

    void dump(common::Formatter* f) const;
    static void generate_test_instances(std::list<utime_t*>& o);

    void encode_timeval(struct timespec* t) const {
        t->tv_sec = tv.tv_sec;
        t->tv_nsec = tv.tv_nsec;
    }
    void decode_timeval(const struct timespec* t) {
        tv.tv_sec = t->tv_sec;
        tv.tv_nsec = t->tv_nsec;
    }

    utime_t round_to_minute() {
        struct tm bdt;
        time_t tt = sec();
        localtime_r(&tt, &bdt);
        bdt.tm_sec = 0;
        tt = mktime(&bdt);
        return utime_t(tt, 0);
    }

    utime_t round_to_hour() {
        struct tm bdt;
        time_t tt = sec();
        localtime_r(&tt, &bdt);
        bdt.tm_sec = 0;
        bdt.tm_min = 0;
        tt = mktime(&bdt);
        return utime_t(tt, 0);
    }

    utime_t round_to_day() {
        struct tm bdt;
        time_t tt = sec();
        localtime_r(&tt, &bdt);
        bdt.tm_sec = 0;
        bdt.tm_min = 0;
        bdt.tm_hour = 0;
        tt = mktime(&bdt);
        return utime_t(tt, 0);
    }

    // cast to double
    operator double() const { return (double)sec() + ((double)nsec() / 1000000000.0f); }
    operator timespec() const {
        timespec ts;
        ts.tv_sec = sec();
        ts.tv_nsec = nsec();
        return ts;
    }

    void sleep() const {
        struct timespec ts;
        to_timespec(&ts);
        nanosleep(&ts, NULL);
    }

    // output
    std::ostream& gmtime(std::ostream& out, bool legacy_form = false) const {
        out.setf(std::ios::right);
        char oldfill = out.fill();
        out.fill('0');
        if (sec() < ((time_t)(60 * 60 * 24 * 365 * 10))) {
            // raw seconds.  this looks like a relative time.
            out << (long)sec() << "." << std::setw(6) << usec();
        } else {
            // this looks like an absolute time.
            //  conform to http://en.wikipedia.org/wiki/ISO_8601
            struct tm bdt;
            time_t tt = sec();
            gmtime_r(&tt, &bdt);
            out << std::setw(4) << (bdt.tm_year + 1900)  // 2007 -> '07'
                << '-' << std::setw(2) << (bdt.tm_mon + 1) << '-' << std::setw(2) << bdt.tm_mday;
            if (legacy_form) {
                out << ' ';
            } else {
                out << 'T';
            }
            out << std::setw(2) << bdt.tm_hour << ':' << std::setw(2) << bdt.tm_min << ':' << std::setw(2) << bdt.tm_sec;
            out << "." << std::setw(6) << usec();
            out << "Z";
        }
        out.fill(oldfill);
        out.unsetf(std::ios::right);
        return out;
    }

    // output
    std::ostream& gmtime_nsec(std::ostream& out) const {
        out.setf(std::ios::right);
        char oldfill = out.fill();
        out.fill('0');
        if (sec() < ((time_t)(60 * 60 * 24 * 365 * 10))) {
            // raw seconds.  this looks like a relative time.
            out << (long)sec() << "." << std::setw(6) << usec();
        } else {
            // this looks like an absolute time.
            //  conform to http://en.wikipedia.org/wiki/ISO_8601
            struct tm bdt;
            time_t tt = sec();
            gmtime_r(&tt, &bdt);
            out << std::setw(4) << (bdt.tm_year + 1900)  // 2007 -> '07'
                << '-' << std::setw(2) << (bdt.tm_mon + 1) << '-' << std::setw(2) << bdt.tm_mday << 'T' << std::setw(2) << bdt.tm_hour << ':'
                << std::setw(2) << bdt.tm_min << ':' << std::setw(2) << bdt.tm_sec;
            out << "." << std::setw(9) << nsec();
            out << "Z";
        }
        out.fill(oldfill);
        out.unsetf(std::ios::right);
        return out;
    }

    // output
    std::ostream& asctime(std::ostream& out) const {
        out.setf(std::ios::right);
        char oldfill = out.fill();
        out.fill('0');
        if (sec() < ((time_t)(60 * 60 * 24 * 365 * 10))) {
            // raw seconds.  this looks like a relative time.
            out << (long)sec() << "." << std::setw(6) << usec();
        } else {
            // this looks like an absolute time.
            struct tm bdt;
            time_t tt = sec();
            gmtime_r(&tt, &bdt);

            char buf[128];
            asctime_r(&bdt, buf);
            int len = strlen(buf);
            if (buf[len - 1] == '\n') buf[len - 1] = '\0';
            out << buf;
        }
        out.fill(oldfill);
        out.unsetf(std::ios::right);
        return out;
    }

    std::ostream& localtime(std::ostream& out, bool legacy_form = false) const {
        out.setf(std::ios::right);
        char oldfill = out.fill();
        out.fill('0');
        if (sec() < ((time_t)(60 * 60 * 24 * 365 * 10))) {
            // raw seconds.  this looks like a relative time.
            out << (long)sec() << "." << std::setw(6) << usec();
        } else {
            // this looks like an absolute time.
            //  conform to http://en.wikipedia.org/wiki/ISO_8601
            struct tm bdt;
            time_t tt = sec();
            localtime_r(&tt, &bdt);
            out << std::setw(4) << (bdt.tm_year + 1900)  // 2007 -> '07'
                << '-' << std::setw(2) << (bdt.tm_mon + 1) << '-' << std::setw(2) << bdt.tm_mday;
            if (legacy_form) {
                out << ' ';
            } else {
                out << 'T';
            }
            out << std::setw(2) << bdt.tm_hour << ':' << std::setw(2) << bdt.tm_min << ':' << std::setw(2) << bdt.tm_sec;
            out << "." << std::setw(6) << usec();
            if (!legacy_form) {
                char buf[32] = {0};
                strftime(buf, sizeof(buf), "%z", &bdt);
                out << buf;
            }
        }
        out.fill(oldfill);
        out.unsetf(std::ios::right);
        return out;
    }
};

// arithmetic operators
inline utime_t operator+(const utime_t& l, const utime_t& r) {
    uint64_t sec = (uint64_t)l.sec() + r.sec();
    return utime_t(cap_to_u32_max(sec), l.nsec() + r.nsec());
}
inline utime_t& operator+=(utime_t& l, const utime_t& r) {
    l.sec_ref() = cap_to_u32_max((uint64_t)l.sec() + r.sec());
    l.nsec_ref() += r.nsec();
    l.normalize();
    return l;
}
inline utime_t& operator+=(utime_t& l, double f) {
    double fs = trunc(f);
    double ns = (f - fs) * 1000000000.0;
    l.sec_ref() = cap_to_u32_max(l.sec() + (uint64_t)fs);
    l.nsec_ref() += (long)ns;
    l.normalize();
    return l;
}

inline utime_t operator-(const utime_t& l, const utime_t& r) {
    return utime_t(l.sec() - r.sec() - (l.nsec() < r.nsec() ? 1 : 0), l.nsec() - r.nsec() + (l.nsec() < r.nsec() ? 1000000000 : 0));
}
inline utime_t& operator-=(utime_t& l, const utime_t& r) {
    l.sec_ref() -= r.sec();
    if (l.nsec() >= r.nsec())
        l.nsec_ref() -= r.nsec();
    else {
        l.nsec_ref() += 1000000000L - r.nsec();
        l.sec_ref()--;
    }
    return l;
}
inline utime_t& operator-=(utime_t& l, double f) {
    double fs = trunc(f);
    double ns = (f - fs) * 1000000000.0;
    l.sec_ref() -= (long)fs;
    long nsl = (long)ns;
    if (nsl) {
        l.sec_ref()--;
        l.nsec_ref() = 1000000000L + l.nsec_ref() - nsl;
    }
    l.normalize();
    return l;
}

class Cycles {
   public:
    static void init();

    /**
     * Return the current value of the fine-grain CPU cycle counter
     * (accessed via the RDTSC instruction).
     */
    static __inline __attribute__((always_inline)) uint64_t rdtsc() {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (((uint64_t)hi << 32) | lo);
    }
    static double per_second();
    static double to_seconds(uint64_t cycles, double cycles_per_sec = 0);
    static uint64_t from_seconds(double seconds, double cycles_per_sec = 0);
    static uint64_t to_microseconds(uint64_t cycles, double cycles_per_sec = 0);
    static uint64_t to_nanoseconds(uint64_t cycles, double cycles_per_sec = 0);
    static uint64_t from_nanoseconds(uint64_t ns, double cycles_per_sec = 0);
    static void sleep(uint64_t us);

   private:
    Cycles();

    /// Conversion factor between cycles and the seconds; computed by
    /// Cycles::init.
    static double cycles_per_sec;

    /**
     * Returns the conversion factor between cycles in seconds, using
     * a mock value for testing when appropriate.
     */
    static __inline __attribute__((always_inline)) double get_cycles_per_sec() { return cycles_per_sec; }
};

double Cycles::cycles_per_sec = 0;

/**
 * Perform once-only overall initialization for the Cycles class, such
 * as calibrating the clock frequency.  This method must be called
 * before using the Cycles module.
 *
 * It is not initialized by default because the timing loops cause
 * general process startup times to balloon
 * (http://tracker.ceph.com/issues/15225).
 */
inline void Cycles::init() {
    if (cycles_per_sec != 0) return;

    // Skip initialization if rtdsc is not implemented
    if (rdtsc() == 0) return;

    // Compute the frequency of the fine-grained CPU timer: to do this,
    // take parallel time readings using both rdtsc and gettimeofday.
    // After 10ms have elapsed, take the ratio between these readings.

    struct timeval start_time, stop_time;
    uint64_t micros;
    double old_cycles;

    // There is one tricky aspect, which is that we could get interrupted
    // between calling gettimeofday and reading the cycle counter, in which
    // case we won't have corresponding readings.  To handle this (unlikely)
    // case, compute the overall result repeatedly, and wait until we get
    // two successive calculations that are within 0.1% of each other.
    old_cycles = 0;
    while (1) {
        if (gettimeofday(&start_time, NULL) != 0) {
            abort();
        }
        uint64_t start_cycles = rdtsc();
        while (1) {
            if (gettimeofday(&stop_time, NULL) != 0) {
                abort();
            }
            uint64_t stop_cycles = rdtsc();
            micros = (stop_time.tv_usec - start_time.tv_usec) + (stop_time.tv_sec - start_time.tv_sec) * 1000000;
            if (micros > 10000) {
                cycles_per_sec = static_cast<double>(stop_cycles - start_cycles);
                cycles_per_sec = 1000000.0 * cycles_per_sec / static_cast<double>(micros);
                break;
            }
        }
        double delta = cycles_per_sec / 1000.0;
        if ((old_cycles > (cycles_per_sec - delta)) && (old_cycles < (cycles_per_sec + delta))) {
            return;
        }
        old_cycles = cycles_per_sec;
    }
}

/**
 * Return the number of CPU cycles per second.
 */
inline double Cycles::per_second() { return get_cycles_per_sec(); }

/**
 * Given an elapsed time measured in cycles, return a floating-point number
 * giving the corresponding time in seconds.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cycles_per_sec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in seconds corresponding to cycles.
 */
double Cycles::to_seconds(uint64_t cycles, double cycles_per_sec) {
    if (cycles_per_sec == 0) cycles_per_sec = get_cycles_per_sec();
    return static_cast<double>(cycles) / cycles_per_sec;
}

/**
 * Given a time in seconds, return the number of cycles that it
 * corresponds to.
 * \param seconds
 *      Time in seconds.
 * \param cycles_per_sec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The approximate number of cycles corresponding to #seconds.
 */
inline uint64_t Cycles::from_seconds(double seconds, double cycles_per_sec) {
    if (cycles_per_sec == 0) cycles_per_sec = get_cycles_per_sec();
    return (uint64_t)(seconds * cycles_per_sec + 0.5);
}

/**
 * Given an elapsed time measured in cycles, return an integer
 * giving the corresponding time in microseconds. Note: to_seconds()
 * is faster than this method.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cycles_per_sec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in microseconds corresponding to cycles (rounded).
 */
inline uint64_t Cycles::to_microseconds(uint64_t cycles, double cycles_per_sec) { return to_nanoseconds(cycles, cycles_per_sec) / 1000; }

/**
 * Given an elapsed time measured in cycles, return an integer
 * giving the corresponding time in nanoseconds. Note: to_seconds()
 * is faster than this method.
 * \param cycles
 *      Difference between the results of two calls to rdtsc.
 * \param cycles_per_sec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The time in nanoseconds corresponding to cycles (rounded).
 */
inline uint64_t Cycles::to_nanoseconds(uint64_t cycles, double cycles_per_sec) {
    if (cycles_per_sec == 0) cycles_per_sec = get_cycles_per_sec();
    return (uint64_t)(1e09 * static_cast<double>(cycles) / cycles_per_sec + 0.5);
}

/**
 * Given a number of nanoseconds, return an approximate number of
 * cycles for an equivalent time length.
 * \param ns
 *      Number of nanoseconds.
 * \param cycles_per_sec
 *      Optional parameter to specify the frequency of the counter that #cycles
 *      was taken from. Useful when converting a remote machine's tick counter
 *      to seconds. The default value of 0 will use the local processor's
 *      computed counter frequency.
 * \return
 *      The approximate number of cycles for the same time length.
 */
inline uint64_t Cycles::from_nanoseconds(uint64_t ns, double cycles_per_sec) {
    if (cycles_per_sec == 0) cycles_per_sec = get_cycles_per_sec();
    return (uint64_t)(static_cast<double>(ns) * cycles_per_sec / 1e09 + 0.5);
}

/**
 * Busy wait for a given number of microseconds.
 * Callers should use this method in most reasonable cases as opposed to
 * usleep for accurate measurements. Calling usleep may put the the processor
 * in a low power mode/sleep state which reduces the clock frequency.
 * So, each time the process/thread wakes up from usleep, it takes some time
 * to ramp up to maximum frequency. Thus meausrements often incur higher
 * latencies.
 * \param us
 *      Number of microseconds.
 */
inline void Cycles::sleep(uint64_t us) {
    uint64_t stop = Cycles::rdtsc() + Cycles::from_nanoseconds(1000 * us);
    while (Cycles::rdtsc() < stop)
        ;
}
#endif  // COMMON_TIME_H