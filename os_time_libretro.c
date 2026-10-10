/* Time layer for the libretro build. Same clock as os_time_posix.c, but the
 * sleeper does nothing: the libretro frontend paces the emulation by holding
 * the BBC thread at each vsync, so wall-clock sleeps would only add latency.
 */
#include "os_time.h"

#include "util.h"

#include <time.h>

void
os_time_setup_hi_res(void) {
}

uint64_t
os_time_get_us() {
  struct timespec ts;

  int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (ret != 0) {
    util_bail("clock_gettime failed");
  }

  return ((ts.tv_sec * (uint64_t) 1000000) + (ts.tv_nsec / 1000));
}

struct os_time_sleeper*
os_time_create_sleeper(void) {
  return NULL;
}

void
os_time_free_sleeper(struct os_time_sleeper* p_sleeper) {
  (void) p_sleeper;
}

void
os_time_sleeper_sleep_us(struct os_time_sleeper* p_sleeper, uint64_t us) {
  (void) p_sleeper;
  (void) us;
}
