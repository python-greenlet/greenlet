#ifndef GREENLET_THREAD_SUPPORT_HPP
#define GREENLET_THREAD_SUPPORT_HPP

/**
 * Defines various utility functions to help greenlet integrate well
 * with threads. When possible, we use portable C++ 11 threading; when
 * not possible, we will use platform specific APIs if needed and
 * available. (Currently, this is only for Python 2.7 on Windows.)
 */

// Allow setting this to 0 on the command line so that we
// can test these code paths on compilers that otherwise support
// standard threads.
#ifndef G_USE_STANDARD_THREADING
#if __cplusplus >= 201103
// Cool. We should have standard support
#    define G_USE_STANDARD_THREADING 1
#elif defined(_MSC_VER)
// MSVC doesn't use a modern version of __cplusplus automatically, you
// have to opt-in to update it with /Zc:__cplusplus, but that's not
// available on our old version of visual studio for Python 2.7
#    if _MSC_VER <= 1500
// Python 2.7 on Windows. Use the Python thread state and native Win32 APIs.
#        define G_USE_STANDARD_THREADING 0
#    else
// Assume we have a compiler that supports it. The Appveyor compilers
// we use all do have standard support
#        define G_USE_STANDARD_THREADING 1
#    endif
#elif defined(__GNUC__) || defined(__clang__)
// All tested versions either do, or can, support what we need
#    define G_USE_STANDARD_THREADING 1
#else
#    define G_USE_STANDARD_THREADING 0
#endif
#endif /* G_USE_STANDARD_THREADING */

#if G_USE_STANDARD_THREADING == 1
#    define G_THREAD_LOCAL_SUPPORTS_DESTRUCTOR 1
#    include <thread>
#    include <mutex>
#    define G_THREAD_LOCAL_VAR thread_local
#    define G_MUTEX_TYPE std::mutex
#    define G_MUTEX_ACQUIRE(Mutex) const std::lock_guard<std::mutex> cleanup_lock(Mutex)
#    define G_MUTEX_RELEASE(Mutex) do {} while (0)
#    define G_MUTEX_INIT(Mutex) do {} while(0)
#else
#    if defined(_MSC_VER)
#        define G_THREAD_LOCAL_VAR __declspec(thread)
#        include <windows.h>
#        define G_MUTEX_TYPE CRITICAL_SECTION
#        define G_MUTEX_ACQUIRE(Mutex) EnterCriticalSection(&Mutex)
#        define G_MUTEX_RELEASE(Mutex) LeaveCriticalSection(&Mutex)
#        define G_MUTEX_INIT(Mutex) InitializeCriticalSection(&Mutex)
//#    elif defined(__GNUC__) || defined(__clang__)
//#        define G_THREAD_LOCAL_VAR __thread
#    else
#        error Don't know how to declare thread-local variables.
#    endif
// XXX: Actually need to implement the memory deletion.
#endif

#endif /* GREENLET_THREAD_SUPPORT_HPP */
