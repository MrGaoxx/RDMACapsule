#ifndef COMMON_H
#define COMMON_H

#include <atomic>
#include <iostream>
#define HAVE_MULTICAST
#ifdef HAVE_MULTICAST
#include "multicast/multicast.h"
#endif

#define kassert(x)                                        \
    if (!(x)) {                                           \
        std::cout << "assert failed " << #x << std::endl; \
        abort();                                          \
    }
std::string cpp_strerror(int err) {
    char buf[128];
    char* errmsg;

    if (err < 0) err = -err;
    std::ostringstream oss;

    if (errmsg = strerror_r(err, buf, sizeof(buf))) {
        errmsg = "Unknown error %d";
    }
    oss << "(" << err << ") " << errmsg;

    return oss.str();
}

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

#endif
