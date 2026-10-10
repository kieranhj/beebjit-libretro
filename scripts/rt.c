/* Minimal libretro host for headless testing on the device.
 *
 *   rt <core.so> <content> <frames> <out.bgra> [key=value ...]
 *
 * Runs the core for <frames> frames with stub callbacks, answering core option
 * queries from the key=value arguments, and writes the last frame as raw
 * XRGB8888 (with a 16-byte header: width, height, pitch, 0). Build:
 *   zig cc -target aarch64-linux-gnu.2.31 -O2 -o rt scripts/rt.c -ldl
 */
#include "../libretro.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_argc;
static char** s_argv;
static const void* s_frame;
static unsigned s_w, s_h;
static size_t s_pitch;
static unsigned long s_audio_frames;

static void log_cb(enum retro_log_level level, const char* fmt, ...) {
  va_list va; (void) level;
  va_start(va, fmt); vfprintf(stderr, fmt, va); va_end(va);
}

static bool env_cb(unsigned cmd, void* data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    ((struct retro_log_callback*) data)->log = log_cb; return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    return *(enum retro_pixel_format*) data == RETRO_PIXEL_FORMAT_XRGB8888;
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    struct retro_variable* v = (struct retro_variable*) data;
    int i;
    size_t kl = strlen(v->key);
    v->value = NULL;
    for (i = 5; i < s_argc; ++i) {
      if (!strncmp(s_argv[i], v->key, kl) && s_argv[i][kl] == '=') {
        v->value = s_argv[i] + kl + 1;
        return true;
      }
    }
    return false;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    *(bool*) data = false; return true;
  case RETRO_ENVIRONMENT_SET_VARIABLES:
  case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
  case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
    return true;
  default:
    return false;
  }
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (data) { s_frame = data; s_w = w; s_h = h; s_pitch = pitch; }
}
static size_t audio_batch_cb(const int16_t* d, size_t frames) { (void) d; s_audio_frames += frames; return frames; }
static void audio_cb(int16_t l, int16_t r) { (void) l; (void) r; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  (void) port; (void) dev; (void) idx; (void) id; return 0;
}

#define SYM(name) name = dlsym(h, #name); if (!name) { fprintf(stderr, "missing %s\n", #name); return 1; }

int main(int argc, char** argv) {
  void* h;
  int frames, i;
  struct retro_game_info info;
  struct retro_system_av_info av;
  void (*retro_set_environment)(retro_environment_t);
  void (*retro_set_video_refresh)(retro_video_refresh_t);
  void (*retro_set_audio_sample)(retro_audio_sample_t);
  void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*retro_set_input_poll)(retro_input_poll_t);
  void (*retro_set_input_state)(retro_input_state_t);
  void (*retro_init)(void);
  void (*retro_deinit)(void);
  bool (*retro_load_game)(const struct retro_game_info*);
  void (*retro_unload_game)(void);
  void (*retro_run)(void);
  void (*retro_get_system_av_info)(struct retro_system_av_info*);

  if (argc < 5) { fprintf(stderr, "usage: rt core content frames out.bgra [key=value...]\n"); return 2; }
  s_argc = argc; s_argv = argv;
  frames = atoi(argv[3]);

  h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
  SYM(retro_set_environment) SYM(retro_set_video_refresh) SYM(retro_set_audio_sample)
  SYM(retro_set_audio_sample_batch) SYM(retro_set_input_poll) SYM(retro_set_input_state)
  SYM(retro_init) SYM(retro_deinit) SYM(retro_load_game) SYM(retro_unload_game)
  SYM(retro_run) SYM(retro_get_system_av_info)

  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(input_poll_cb);
  retro_set_input_state(input_state_cb);
  retro_init();
  memset(&info, 0, sizeof(info));
  info.path = argv[2];
  if (!retro_load_game(&info)) { fprintf(stderr, "load_game failed\n"); return 1; }
  retro_get_system_av_info(&av);
  fprintf(stderr, "geometry %ux%u fps %.2f rate %.0f\n", av.geometry.base_width,
          av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
  {
    /* BEEBJIT_RT_SLEEP_US=20000 paces frames like a real frontend would. */
    const char* p_sleep = getenv("BEEBJIT_RT_SLEEP_US");
    const char* p_start = getenv("BEEBJIT_RT_START_SLEEP_US");
    long sleep_us = p_sleep ? atol(p_sleep) : 0;
    if (p_start) usleep(atol(p_start));
    for (i = 0; i < frames; ++i) {
      retro_run();
      if (sleep_us > 0) usleep(sleep_us);
    }
  }
  fprintf(stderr, "ran %d frames, %lu audio frames, last frame %ux%u pitch %zu\n",
          frames, s_audio_frames, s_w, s_h, s_pitch);
  if (s_frame) {
    FILE* f = fopen(argv[4], "wb");
    unsigned hdr[4] = { s_w, s_h, (unsigned) s_pitch, 0 };
    unsigned y;
    fwrite(hdr, sizeof(hdr), 1, f);
    for (y = 0; y < s_h; ++y) fwrite((const char*) s_frame + y * s_pitch, 4, s_w, f);
    fclose(f);
  }
  retro_unload_game();
  retro_deinit();
  return 0;
}
