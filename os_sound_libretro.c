/* Sound layer for the libretro build: a single-producer ring buffer.
 * beebjit's synchronous sound path calls os_sound_write() from the BBC thread
 * whenever a period's worth of frames is ready. Instead of blocking on a
 * device we queue them; retro_run() drains the queue after each vsync.
 * If the frontend ever falls behind, the oldest frames are dropped.
 */
#include "os_sound.h"
#include "os_sound_libretro.h"

#include "util.h"

#include <string.h>

enum {
  k_ring_frames = 16384,   /* ~340 ms at 48 kHz; must be a power of two. */
  k_ring_mask = (k_ring_frames - 1),
};

static uint32_t s_sample_rate = 48000;
static uint32_t s_buffer_size = 2048;
static uint32_t s_num_periods = 4;

struct os_sound_struct {
  uint32_t sample_rate;
  uint32_t buffer_size;
  uint32_t period_size;
};

static int16_t s_ring[k_ring_frames];
static volatile uint32_t s_write_pos;
static volatile uint32_t s_read_pos;

uint32_t
os_sound_get_default_sample_rate(void) {
  return s_sample_rate;
}

uint32_t
os_sound_get_default_buffer_size(void) {
  return s_buffer_size;
}

uint32_t
os_sound_get_default_num_periods(void) {
  return s_num_periods;
}

struct os_sound_struct*
os_sound_create(char* p_device_name,
                uint32_t sample_rate,
                uint32_t buffer_size,
                uint32_t num_periods) {
  struct os_sound_struct* p_driver = util_mallocz(sizeof(struct os_sound_struct));
  (void) p_device_name;
  p_driver->sample_rate = sample_rate;
  p_driver->buffer_size = buffer_size;
  p_driver->period_size = (buffer_size / num_periods);
  s_write_pos = 0;
  s_read_pos = 0;
  return p_driver;
}

void
os_sound_destroy(struct os_sound_struct* p_driver) {
  util_free(p_driver);
}

int
os_sound_init(struct os_sound_struct* p_driver) {
  (void) p_driver;
  return 0;
}

uint32_t
os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
  return p_driver->sample_rate;
}

uint32_t
os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
  return p_driver->buffer_size;
}

uint32_t
os_sound_get_period_size(struct os_sound_struct* p_driver) {
  return p_driver->period_size;
}

void
os_sound_write(struct os_sound_struct* p_driver,
               int16_t* p_frames,
               uint32_t num_frames) {
  uint32_t i;
  uint32_t w = s_write_pos;
  (void) p_driver;

  for (i = 0; i < num_frames; ++i) {
    s_ring[w & k_ring_mask] = p_frames[i];
    w++;
  }
  s_write_pos = w;
  /* Drop the oldest frames if the reader has fallen behind by a full ring. */
  if ((w - s_read_pos) > k_ring_frames) {
    s_read_pos = (w - k_ring_frames);
  }
}

uint32_t
os_sound_libretro_read(int16_t* p_out, uint32_t max_frames) {
  uint32_t n = 0;
  uint32_t r = s_read_pos;
  uint32_t w = s_write_pos;

  while ((r != w) && (n < max_frames)) {
    p_out[n++] = s_ring[r & k_ring_mask];
    r++;
  }
  s_read_pos = r;
  return n;
}
