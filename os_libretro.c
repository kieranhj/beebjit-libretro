/* OS layer for the libretro build of beebjit.
 * Mirrors beebjit's os.c but swaps in:
 *  - os_sound_libretro.c : sound goes into a ring buffer the core drains.
 *  - os_time_libretro.c  : real clock, but the sleeper is a no-op so the
 *                          emulation thread never paces itself by wall time.
 *  - os_window_null.c    : no window; we render into an internal buffer.
 */
#if defined(__linux__) || __APPLE__
#define _GNU_SOURCE
#include "os_alloc_posix.c"
#include "os_channel_posix.c"
#include "os_fault_posix.c"
#include "os_poller_posix.c"
#include "os_terminal_posix.c"
#include "os_thread_posix.c"
#if __APPLE__
#include "os_lock_macos.c"
#else
#include "os_lock_posix.c"
#endif
#include "os_time_libretro.c"
#include "os_sound_libretro.c"
#include "os_window_null.c"
#if defined(__x86_64__)
#if __APPLE__
#include "os_fault_platform_macos_x64.c"
#else
#include "os_fault_platform_linux_x64.c"
#endif
#elif defined(__aarch64__)
#if __APPLE__
#include "os_fault_platform_macos_arm64.c"
#else
#include "os_fault_platform_linux_arm64.c"
#endif
#else
#include "os_fault_platform_null.c"
#endif
#else
#error libretro build: only Linux/macOS hosts are supported.
#endif
