#ifndef COMMON_H
#define COMMON_H

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>
//#define HAVE_MULTICAST
#ifdef HAVE_MULTICAST
#include "multicast/multicast.h"
#endif

#define kassert(x)                                        \
    if (!(x)) {                                           \
        std::cout << "assert failed " << #x << std::endl; \
        abort();                                          \
    }

inline std::string cpp_strerror(int err) {
    char buf[128];

    char* errmsg = "Unknown error %d";

    if (err < 0) err = -err;
    std::ostringstream oss;

    errmsg = strerror_r(err, buf, sizeof(buf));
    oss << "(" << err << ") " << errmsg;

    return oss.str();
}

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace common {

class spinlock;

inline void spin_lock(std::atomic_flag& lock);
inline void spin_unlock(std::atomic_flag& lock);
inline void spin_lock(common::spinlock& lock);
inline void spin_unlock(common::spinlock& lock);

/* A pre-packaged spinlock type modelling BasicLockable: */
class spinlock final {
    std::atomic_flag af = ATOMIC_FLAG_INIT;

   public:
    void lock() { common::spin_lock(af); }

    void unlock() noexcept { common::spin_unlock(af); }
};

// Free functions:
inline void spin_lock(std::atomic_flag& lock) {
    while (lock.test_and_set(std::memory_order_acquire))
        ;
}

inline void spin_unlock(std::atomic_flag& lock) { lock.clear(std::memory_order_release); }

inline void spin_lock(std::atomic_flag* lock) { spin_lock(*lock); }

inline void spin_unlock(std::atomic_flag* lock) { spin_unlock(*lock); }

inline void spin_lock(common::spinlock& lock) { lock.lock(); }

inline void spin_unlock(common::spinlock& lock) { lock.unlock(); }

inline void spin_lock(common::spinlock* lock) { spin_lock(*lock); }

inline void spin_unlock(common::spinlock* lock) { spin_unlock(*lock); }

}  // namespace common

#if defined(HAVE_PTHREAD_SET_NAME_NP)
/* Fix a small name diff and return 0 */
#define pthread_setname(thread, name)      \
    ({                                     \
        pthread_set_name_np(thread, name); \
        0;                                 \
    })
#else
/* compiler warning free success noop */
#define pthread_setname(thread, name) \
    ({                                \
        int __i = 0;                  \
        __i;                          \
    })
#endif

template <typename F>
struct scope_guard {
    F f;
    scope_guard() = delete;
    scope_guard(const scope_guard&) = delete;
    scope_guard(scope_guard&&) = default;
    scope_guard& operator=(const scope_guard&) = delete;
    scope_guard& operator=(scope_guard&&) = default;
    scope_guard(const F& f) : f(f) {}
    scope_guard(F&& f) : f(std::move(f)) {}
    template <typename... Args>
    scope_guard(std::in_place_t, Args&&... args) : f(std::forward<Args>(args)...) {}
    ~scope_guard() {
        std::move(f)();  // Support at-most-once functions
    }
};

template <typename F>
scope_guard<F> make_scope_guard(F&& f) {
    return scope_guard<F>(std::forward<F>(f));
}

template <typename F, typename... Args>
scope_guard<F> make_scope_guard(std::in_place_type_t<F>, Args&&... args) {
    return {std::in_place, std::forward<Args>(args)...};
}

#endif
