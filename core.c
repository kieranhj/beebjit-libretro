/* libretro core wrapping beebjit.
 *
 * Threading model: beebjit runs the BBC on its own thread (bbc_run_async).
 * At every BBC vsync that thread sends k_message_vsync over a pipe and then
 * blocks until it receives k_message_render_done. We use that handshake as the
 * lockstep point: retro_run() releases the thread for one frame, waits for the
 * next vsync, presents the frame and queued audio, and leaves the thread parked
 * until the frontend calls retro_run() again. Wall-clock pacing inside beebjit
 * is disabled by os_time_libretro.c (no-op sleeper) and os_sound_libretro.c
 * (non-blocking ring buffer), so the frontend is the only clock.
 */
#include "libretro.h"

#include "bbc.h"
#include "cpu_driver.h"
#include "keyboard.h"
#include "log.h"
#include "os_channel.h"
#include "os_sound.h"
#include "os_sound_libretro.h"
#include "render.h"
#include "sound.h"
#include "state_6502.h"
#include "video.h"

#include "roms_data.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  k_sample_rate = 48000,
  k_sound_buffer = 2048,
  k_sound_periods = 4,
  k_audio_chunk = 4096,
};

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static struct bbc_struct* s_p_bbc;
static struct render_struct* s_p_render;
static struct video_struct* s_p_video;
static struct keyboard_struct* s_p_keyboard;
static struct os_sound_struct* s_p_sound_driver;
static intptr_t s_chan_read_ui, s_chan_write_bbc, s_chan_read_bbc, s_chan_write_ui;
static int s_running;
static int s_thread_exited;
static int s_pending_ack;
static uint32_t s_width;
static uint32_t* s_p_frame_copy;   /* frame handed to the frontend; see retro_run */
static uint32_t s_height;
static int16_t s_audio_mono[k_audio_chunk];
static int16_t s_audio_stereo[k_audio_chunk * 2];
static int s_break_held_frames;
static char s_disc_path[4096];
static int s_swram;
static int s_is_master;
static int s_cpu_mode = k_cpu_mode_jit;
static uint32_t s_frame_count;
static long s_autokey_frame = -1;   /* env BEEBJIT_AUTOKEY_FRAME, for benchmarks */

/* Per-phase timing, logged every k_stats_frames frames. */
enum { k_stats_frames = 250 };
static uint64_t s_t_wait, s_t_render, s_t_video, s_t_audio, s_t_total;
static uint32_t s_stats_count;
uint64_t os_time_get_us(void);

/* ---- joypad to BBC key mapping -------------------------------------- */

struct key_name {
  const char* p_name;
  uint8_t key;
};

/* Names are what the user sees in core options; keys are beebjit host keys
 * (ASCII uppercase for printable keys, k_keyboard_key_* for the rest).
 */
static const struct key_name s_key_names[] = {
  { "None", 0 },
  { "Z", 'Z' }, { "X", 'X' }, { "A", 'A' }, { "S", 'S' }, { "D", 'D' },
  { "Q", 'Q' }, { "W", 'W' }, { "E", 'E' }, { "R", 'R' }, { "P", 'P' },
  { "L", 'L' }, { "K", 'K' }, { "M", 'M' }, { "N", 'N' }, { "C", 'C' },
  { "B", 'B' }, { "F", 'F' }, { "G", 'G' }, { "H", 'H' }, { "I", 'I' },
  { "J", 'J' }, { "O", 'O' }, { "T", 'T' }, { "U", 'U' }, { "V", 'V' },
  { "Y", 'Y' },
  { "Colon", '\'' }, { "Slash", '/' }, { "Semicolon", ';' },
  { "Comma", ',' }, { "Full stop", '.' },
  { "Space", ' ' }, { "Return", k_keyboard_key_enter },
  { "Escape", k_keyboard_key_escape },
  { "Shift", k_keyboard_key_shift_left }, { "Ctrl", k_keyboard_key_ctrl },
  { "Caps Lock", k_keyboard_key_caps_lock }, { "Tab", k_keyboard_key_tab },
  { "Delete", k_keyboard_key_backspace }, { "Copy", k_keyboard_key_end },
  { "Cursor Up", k_keyboard_key_arrow_up },
  { "Cursor Down", k_keyboard_key_arrow_down },
  { "Cursor Left", k_keyboard_key_arrow_left },
  { "Cursor Right", k_keyboard_key_arrow_right },
  { "f0", k_keyboard_key_f0 }, { "f1", k_keyboard_key_f1 },
  { "f2", k_keyboard_key_f2 }, { "f3", k_keyboard_key_f3 },
  { "f4", k_keyboard_key_f4 }, { "f5", k_keyboard_key_f5 },
  { "f6", k_keyboard_key_f6 }, { "f7", k_keyboard_key_f7 },
  { "f8", k_keyboard_key_f8 }, { "f9", k_keyboard_key_f9 },
  { "1", '1' }, { "2", '2' }, { "3", '3' }, { "4", '4' }, { "5", '5' },
  { "6", '6' }, { "7", '7' }, { "8", '8' }, { "9", '9' }, { "0", '0' },
  { "BREAK", k_keyboard_key_f12 },
};
#define k_num_key_names (sizeof(s_key_names) / sizeof(s_key_names[0]))

struct pad_button {
  const char* p_option;      /* core option key suffix */
  const char* p_label;
  unsigned id;               /* RETRO_DEVICE_ID_JOYPAD_* */
  const char* p_default;
};

static const struct pad_button s_pad_buttons[] = {
  { "up",     "D-pad Up",    RETRO_DEVICE_ID_JOYPAD_UP,     "Colon" },
  { "down",   "D-pad Down",  RETRO_DEVICE_ID_JOYPAD_DOWN,   "Slash" },
  { "left",   "D-pad Left",  RETRO_DEVICE_ID_JOYPAD_LEFT,   "Z" },
  { "right",  "D-pad Right", RETRO_DEVICE_ID_JOYPAD_RIGHT,  "X" },
  { "a",      "A",           RETRO_DEVICE_ID_JOYPAD_A,      "Return" },
  { "b",      "B",           RETRO_DEVICE_ID_JOYPAD_B,      "Space" },
  { "x",      "X",           RETRO_DEVICE_ID_JOYPAD_X,      "Shift" },
  { "y",      "Y",           RETRO_DEVICE_ID_JOYPAD_Y,      "Ctrl" },
  { "l",      "L",           RETRO_DEVICE_ID_JOYPAD_L,      "Escape" },
  { "r",      "R",           RETRO_DEVICE_ID_JOYPAD_R,      "Copy" },
  { "l2",     "L2",          RETRO_DEVICE_ID_JOYPAD_L2,     "f0" },
  { "r2",     "R2",          RETRO_DEVICE_ID_JOYPAD_R2,     "BREAK" },
  { "start",  "Start",       RETRO_DEVICE_ID_JOYPAD_START,  "Escape" },
  { "select", "Select",      RETRO_DEVICE_ID_JOYPAD_SELECT, "Space" },
};
#define k_num_pad_buttons (sizeof(s_pad_buttons) / sizeof(s_pad_buttons[0]))

static uint8_t s_pad_key[k_num_pad_buttons];
static int s_pad_down[k_num_pad_buttons];
static char s_option_keys[k_num_pad_buttons][32];
static char s_option_desc[k_num_pad_buttons][512];
static struct retro_variable s_variables[k_num_pad_buttons + 8];

static uint8_t
key_from_name(const char* p_name) {
  size_t i;
  for (i = 0; i < k_num_key_names; ++i) {
    if (!strcmp(s_key_names[i].p_name, p_name)) {
      return s_key_names[i].key;
    }
  }
  return 0;
}

static void
build_variables(void) {
  size_t i;
  for (i = 0; i < k_num_pad_buttons; ++i) {
    size_t j;
    char* p = s_option_desc[i];
    size_t left = sizeof(s_option_desc[i]);
    int n;
    snprintf(s_option_keys[i], sizeof(s_option_keys[i]),
             "beebjit_pad_%s", s_pad_buttons[i].p_option);
    /* libretro v0 option format: "Description; default|other|other" */
    n = snprintf(p, left, "Pad %s; %s", s_pad_buttons[i].p_label,
                 s_pad_buttons[i].p_default);
    p += n; left -= n;
    for (j = 0; j < k_num_key_names; ++j) {
      if (!strcmp(s_key_names[j].p_name, s_pad_buttons[i].p_default)) {
        continue;
      }
      n = snprintf(p, left, "|%s", s_key_names[j].p_name);
      p += n; left -= n;
    }
    s_variables[i].key = s_option_keys[i];
    s_variables[i].value = s_option_desc[i];
  }
  s_variables[i].key = "beebjit_model";
  s_variables[i].value = "Machine (restart); Model B|Master 128";
  i++;
  s_variables[i].key = "beebjit_cpu";
  s_variables[i].value = "6502 core (restart); JIT|Interpreter";
  i++;
  s_variables[i].key = "beebjit_autoboot";
  s_variables[i].value = "Auto-boot disc (Shift+BREAK); enabled|disabled";
  i++;
  s_variables[i].key = "beebjit_swram";
  s_variables[i].value = "Sideways RAM in banks 4-7 (restart); enabled|disabled";
  i++;
  s_variables[i].key = "beebjit_aspect";
  s_variables[i].value = "Aspect ratio; Monitor|4:3";
  i++;
  s_variables[i].key = NULL;
  s_variables[i].value = NULL;
}

static int
option_enabled(const char* p_key, int default_value) {
  struct retro_variable var = { p_key, NULL };
  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
    return !strcmp(var.value, "enabled");
  }
  return default_value;
}

/* The shape of the picture on the monitor. beebjit draws a 16 MHz pixel
 * grid with every scanline doubled, which is square on a Beeb monitor: the
 * 640x512 MODE 0-2 area is 5:4, as BeebEm and b2 show it, and beebjit's
 * border makes the whole frame 768x640, 6:5. "4:3" stretches the frame to
 * fill the screen instead. */
static int s_aspect_4_3;

static float
frame_aspect(void) {
  if (s_aspect_4_3 || !s_height) return 4.0f / 3.0f;
  return (float) s_width / (float) s_height;
}

static void
read_aspect_option(void) {
  struct retro_variable var = { "beebjit_aspect", NULL };
  int is_4_3 = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && !strcmp(var.value, "4:3");
  if (is_4_3 != s_aspect_4_3) {
    s_aspect_4_3 = is_4_3;
    if (s_height) {
      struct retro_game_geometry geom;
      geom.base_width = s_width;
      geom.base_height = s_height / 2;
      geom.max_width = s_width;
      geom.max_height = s_height / 2;
      geom.aspect_ratio = frame_aspect();
      environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
    }
  }
}

static void
read_pad_options(void) {
  size_t i;
  for (i = 0; i < k_num_pad_buttons; ++i) {
    struct retro_variable var = { s_option_keys[i], NULL };
    const char* p_name = s_pad_buttons[i].p_default;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      p_name = var.value;
    }
    s_pad_key[i] = key_from_name(p_name);
  }
}

/* ---- host keyboard ----------------------------------------------------- */

static uint8_t
retrok_to_beebjit(unsigned keycode) {
  if ((keycode >= RETROK_a) && (keycode <= RETROK_z)) {
    return (uint8_t) ('A' + (keycode - RETROK_a));
  }
  if ((keycode >= RETROK_0) && (keycode <= RETROK_9)) {
    return (uint8_t) ('0' + (keycode - RETROK_0));
  }
  switch (keycode) {
  case RETROK_ESCAPE: return k_keyboard_key_escape;
  case RETROK_BACKSPACE: return k_keyboard_key_backspace;
  case RETROK_TAB: return k_keyboard_key_tab;
  case RETROK_RETURN: case RETROK_KP_ENTER: return k_keyboard_key_enter;
  case RETROK_LCTRL: case RETROK_RCTRL: return k_keyboard_key_ctrl;
  case RETROK_LSHIFT: return k_keyboard_key_shift_left;
  case RETROK_RSHIFT: return k_keyboard_key_shift_right;
  case RETROK_CAPSLOCK: return k_keyboard_key_caps_lock;
  case RETROK_F1: return k_keyboard_key_f1;
  case RETROK_F2: return k_keyboard_key_f2;
  case RETROK_F3: return k_keyboard_key_f3;
  case RETROK_F4: return k_keyboard_key_f4;
  case RETROK_F5: return k_keyboard_key_f5;
  case RETROK_F6: return k_keyboard_key_f6;
  case RETROK_F7: return k_keyboard_key_f7;
  case RETROK_F8: return k_keyboard_key_f8;
  case RETROK_F9: return k_keyboard_key_f9;
  case RETROK_F10: return k_keyboard_key_f0;
  case RETROK_F11: return k_keyboard_key_f11;
  case RETROK_F12: return k_keyboard_key_f12;
  case RETROK_UP: return k_keyboard_key_arrow_up;
  case RETROK_DOWN: return k_keyboard_key_arrow_down;
  case RETROK_LEFT: return k_keyboard_key_arrow_left;
  case RETROK_RIGHT: return k_keyboard_key_arrow_right;
  case RETROK_END: return k_keyboard_key_end;
  case RETROK_PAGEUP: return k_keyboard_key_page_up;
  case RETROK_PAGEDOWN: return k_keyboard_key_page_down;
  case RETROK_DELETE: return k_keyboard_key_delete;
  case RETROK_SPACE: return ' ';
  case RETROK_MINUS: return '-';
  case RETROK_EQUALS: return '=';
  case RETROK_LEFTBRACKET: return '[';
  case RETROK_RIGHTBRACKET: return ']';
  case RETROK_SEMICOLON: return ';';
  case RETROK_QUOTE: return '\'';
  case RETROK_BACKQUOTE: return '`';
  case RETROK_BACKSLASH: return '\\';
  case RETROK_COMMA: return ',';
  case RETROK_PERIOD: return '.';
  case RETROK_SLASH: return '/';
  default: return 0;
  }
}

static void
keyboard_event_cb(bool down, unsigned keycode, uint32_t character, uint16_t mods) {
  uint8_t key;
  (void) character;
  (void) mods;
  if (!s_p_keyboard) {
    return;
  }
  key = retrok_to_beebjit(keycode);
  if (getenv("BEEBJIT_LOG_INPUT")) {
    log_cb(RETRO_LOG_INFO, "beebjit kbd %s retrok=%u -> key=%u\n", down ? "down" : "up", keycode, key);
  }
  if (key == 0) {
    return;
  }
  if (down) {
    keyboard_system_key_pressed(s_p_keyboard, key);
  } else {
    keyboard_system_key_released(s_p_keyboard, key);
  }
}

/* ---- libretro API ------------------------------------------------------- */

static void
fallback_log(enum retro_log_level level, const char* fmt, ...) {
  (void) level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

void
retro_set_environment(retro_environment_t cb) {
  struct retro_log_callback logging;
  bool no_game = false;
  environ_cb = cb;
  build_variables();
  cb(RETRO_ENVIRONMENT_SET_VARIABLES, s_variables);
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
  if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
    log_cb = logging.log;
  } else {
    log_cb = fallback_log;
  }
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void) cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void
retro_init(void) {
  log_set_do_log_to_stdout(1);
}

void
retro_deinit(void) {
}

unsigned
retro_api_version(void) {
  return RETRO_API_VERSION;
}

void
retro_get_system_info(struct retro_system_info* info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "beebjit";
  info->library_version = "0.1";
  info->valid_extensions = "ssd|dsd|hfe|adl|adf|fsd|dfi|scp|rfi";
  info->need_fullpath = true;
  info->block_extract = true;
}

void
retro_get_system_av_info(struct retro_system_av_info* info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = s_width;
  info->geometry.base_height = s_height / 2;
  info->geometry.max_width = s_width;
  info->geometry.max_height = s_height / 2;
  info->geometry.aspect_ratio = frame_aspect();
  info->timing.fps = 50.0;
  info->timing.sample_rate = (double) k_sample_rate;
}

void
retro_set_controller_port_device(unsigned port, unsigned device) {
  (void) port;
  (void) device;
}

static void
send_render_done(void) {
  struct bbc_message message;
  message.data[0] = k_message_render_done;
  bbc_client_send_message(s_p_bbc, &message);
}

static void
stop_bbc_thread(void) {
  struct cpu_driver* p_cpu_driver;
  if (!s_running) {
    return;
  }
  p_cpu_driver = bbc_get_cpu_driver(s_p_bbc);
  if (!(p_cpu_driver->p_funcs->get_flags(p_cpu_driver) & k_cpu_flag_exited)) {
    p_cpu_driver->p_funcs->apply_flags(p_cpu_driver, k_cpu_flag_exited, 0);
    p_cpu_driver->p_funcs->set_exit_value(p_cpu_driver, 0);
  }
  /* Release the thread if it is parked at a vsync, then drain messages until
   * it reports that it has exited.
   */
  if (s_pending_ack) {
    send_render_done();
    s_pending_ack = 0;
  }
  while (!s_thread_exited) {
    struct bbc_message message;
    bbc_client_receive_message(s_p_bbc, &message);
    if (message.data[0] == k_message_exited) {
      s_thread_exited = 1;
    } else if (message.data[0] == k_message_vsync) {
      if (message.data[4]) {
        send_render_done();
      }
    }
  }
  s_running = 0;
}

bool
retro_load_game(const struct retro_game_info* info) {
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  struct retro_keyboard_callback kcb = { keyboard_event_cb };
  /* bbc_create() keeps the OS ROM pointer and re-reads it whenever a Master
   * pages HAZEL out, so this must outlive retro_load_game(). A stack buffer
   * here cost a day: it worked in a tiny host whose stack happened to stay
   * intact and booted the Master into garbage inside RetroArch.
   */
  static uint8_t os_rom[k_bbc_rom_size];
  uint8_t rom[k_bbc_rom_size];
  int autoboot;

  if (!info || !info->path) {
    return false;
  }
  if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    log_cb(RETRO_LOG_ERROR, "XRGB8888 not supported\n");
    return false;
  }
  environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kcb);
  read_pad_options();
  read_aspect_option();
  autoboot = option_enabled("beebjit_autoboot", 1);
  s_swram = option_enabled("beebjit_swram", 1);
  {
    struct retro_variable var = { "beebjit_model", NULL };
    s_is_master = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      s_is_master = !strcmp(var.value, "Master 128");
    }
    var.key = "beebjit_cpu"; var.value = NULL;
    s_cpu_mode = k_cpu_mode_jit;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "Interpreter")) s_cpu_mode = k_cpu_mode_interp;
    }
  }

  strncpy(s_disc_path, info->path, sizeof(s_disc_path) - 1);

  memcpy(os_rom, s_is_master ? mos320_mos_rom : os12_rom, k_bbc_rom_size);
  /* JIT, slow (real-time) mode => accurate + synchronous sound.
   * Model B: 8271 + DFS 0.9. Master 128: MOS 3.20, 1770, sideways RAM 4-7.
   */
  s_p_bbc = bbc_create(s_cpu_mode,
                       s_is_master,
                       (s_is_master || s_swram),   /* has_sideways_ram */
                       os_rom,
                       s_is_master ? 1 : 0,        /* wd_1770_type */
                       getenv("BEEBJIT_DEBUG") ? 1 : 0,   /* debug: needed for print */
                       getenv("BEEBJIT_DEBUG") ? 1 : 0,   /* run (don't stop at start) */
                       getenv("BEEBJIT_PRINT") ? 1 : 0,   /* print every instruction */
                       0,       /* fast */
                       0,       /* accurate (implied by !fast) */
                       0,       /* fasttape */
                       0,       /* test_map */
                       getenv("BEEBJIT_OPT_FLAGS") ? getenv("BEEBJIT_OPT_FLAGS") : "",
                       getenv("BEEBJIT_LOG_FLAGS") ? getenv("BEEBJIT_LOG_FLAGS") : "");
  if (s_p_bbc == NULL) {
    log_cb(RETRO_LOG_ERROR, "bbc_create failed\n");
    return false;
  }

  bbc_add_disc(s_p_bbc, s_disc_path, 0, 0, 0, 0, 0, 0);
  if (autoboot) {
    bbc_set_autoboot(s_p_bbc, 1);
  }

  s_p_render = bbc_get_render(s_p_bbc);
  s_p_keyboard = bbc_get_keyboard(s_p_bbc);
  s_p_video = bbc_get_video(s_p_bbc);
  render_create_internal_buffer(s_p_render);
  s_width = render_get_width(s_p_render);
  s_height = render_get_height(s_p_render);
  /* beebjit allocates this with plain malloc. In a fresh process that is zero
   * pages; inside a long-lived frontend it is recycled heap.
   */
  memset(render_get_buffer(s_p_render), 0, (size_t) s_width * s_height * sizeof(uint32_t));
  log_cb(RETRO_LOG_INFO, "beebjit render buffer %ux%u\n", s_width, s_height);

  bbc_power_on_reset(s_p_bbc);

  if (s_is_master) {
    static const struct { uint8_t slot; const uint8_t* p_rom; } master_roms[] = {
      { 0x9, mos320_dfs_rom },     { 0xA, mos320_viewsht_rom },
      { 0xB, mos320_edit_rom },    { 0xC, mos320_basic4_rom },
      { 0xD, mos320_adfs_rom },    { 0xE, mos320_view_rom },
      { 0xF, mos320_terminal_rom },
    };
    size_t i;
    for (i = 0; i < sizeof(master_roms) / sizeof(master_roms[0]); ++i) {
      memcpy(rom, master_roms[i].p_rom, k_bbc_rom_size);
      bbc_load_rom(s_p_bbc, master_roms[i].slot, rom);
    }
  } else {
    memcpy(rom, basic_rom, k_bbc_rom_size);
    bbc_load_rom(s_p_bbc, k_bbc_default_basic_rom_slot, rom);
    memcpy(rom, dfs09_rom, k_bbc_rom_size);
    bbc_load_rom(s_p_bbc, k_bbc_default_dfs_rom_slot, rom);
  }
  if (s_is_master || s_swram) {
    int bank;
    for (bank = 4; bank <= 7; ++bank) {
      bbc_make_sideways_ram(s_p_bbc, bank);
    }
  }

  s_p_sound_driver = os_sound_create(NULL, k_sample_rate, k_sound_buffer, k_sound_periods);
  os_sound_init(s_p_sound_driver);
  sound_set_driver(bbc_get_sound(s_p_bbc), s_p_sound_driver);

  os_channel_get_handles(&s_chan_read_ui, &s_chan_write_bbc,
                         &s_chan_read_bbc, &s_chan_write_ui);
  bbc_set_channel_handles(s_p_bbc, s_chan_read_bbc, s_chan_write_bbc,
                          s_chan_read_ui, s_chan_write_ui);

  s_thread_exited = 0;
  s_pending_ack = 0;
  s_break_held_frames = 0;
  s_frame_count = 0;
  {
    const char* p_env = getenv("BEEBJIT_AUTOKEY_FRAME");
    s_autokey_frame = p_env ? atol(p_env) : -1;
  }
  memset(s_pad_down, 0, sizeof(s_pad_down));
  bbc_run_async(s_p_bbc);
  s_running = 1;
  return true;
}

void
retro_unload_game(void) {
  if (!s_p_bbc) {
    return;
  }
  stop_bbc_thread();
  bbc_destroy(s_p_bbc);
  s_p_bbc = NULL;
  s_p_keyboard = NULL;
  s_p_render = NULL;
  s_p_video = NULL;
  if (s_p_sound_driver) {
    os_sound_destroy(s_p_sound_driver);
    s_p_sound_driver = NULL;
  }
  os_channel_free_handles(s_chan_read_ui, s_chan_write_bbc,
                          s_chan_read_bbc, s_chan_write_ui);
}

bool
retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  (void) type; (void) info; (void) num;
  return false;
}

static void
poll_pad(void) {
  size_t i;
  input_poll_cb();
  for (i = 0; i < k_num_pad_buttons; ++i) {
    int down;
    if (s_pad_key[i] == 0) {
      continue;
    }
    down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, s_pad_buttons[i].id) ? 1 : 0;
    if (down != s_pad_down[i] && getenv("BEEBJIT_LOG_INPUT")) {
      log_cb(RETRO_LOG_INFO, "beebjit pad %s %s -> key=%u\n", s_pad_buttons[i].p_label, down ? "down" : "up", s_pad_key[i]);
    }
    if (down && !s_pad_down[i]) {
      keyboard_system_key_pressed(s_p_keyboard, s_pad_key[i]);
    } else if (!down && s_pad_down[i]) {
      keyboard_system_key_released(s_p_keyboard, s_pad_key[i]);
    }
    s_pad_down[i] = down;
  }
}

static void
drain_audio(void) {
  for (;;) {
    uint32_t i;
    uint32_t n = os_sound_libretro_read(s_audio_mono, k_audio_chunk);
    if (n == 0) {
      break;
    }
    for (i = 0; i < n; ++i) {
      s_audio_stereo[i * 2] = s_audio_mono[i];
      s_audio_stereo[i * 2 + 1] = s_audio_mono[i];
    }
    audio_batch_cb(s_audio_stereo, n);
  }
}

void
retro_run(void) {
  struct bbc_message message;
  bool updated = false;

  if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
    read_pad_options();
    read_aspect_option();
  }

  if (!s_running || s_thread_exited) {
    video_cb(NULL, s_width, s_height, 0);
    return;
  }

  poll_pad();
  s_frame_count++;
  if (getenv("BEEBJIT_LOG_VIDEO") && (s_frame_count <= 6 || (s_frame_count % 25) == 1) && s_frame_count < 400) {
    uint8_t crtc[k_video_crtc_num_registers];
    video_get_crtc_registers(s_p_video, crtc);
    log_cb(RETRO_LOG_INFO, "beebjit f=%u ula=%02X r12=%02X r13=%02X romsel=%02X acccon=%02X pc=%04X\n",
           s_frame_count, video_get_ula_control(s_p_video), crtc[12], crtc[13],
           bbc_get_romsel(s_p_bbc), bbc_get_acccon(s_p_bbc),
           (unsigned) state_6502_get_pc(bbc_get_6502(s_p_bbc)));
  }
  if (s_autokey_frame >= 0) {
    if (s_frame_count == (uint32_t) s_autokey_frame) {
      keyboard_system_key_pressed(s_p_keyboard, ' ');
    } else if (s_frame_count == (uint32_t) s_autokey_frame + 3) {
      keyboard_system_key_released(s_p_keyboard, ' ');
    }
  }
  if (s_break_held_frames > 0) {
    if (--s_break_held_frames == 0) {
      keyboard_system_key_released(s_p_keyboard, k_keyboard_key_f12);
    }
  }

  uint64_t t0 = os_time_get_us();
  uint64_t t1, t2, t3, t4;

  /* Let the BBC thread run the next frame. */
  if (s_pending_ack) {
    send_render_done();
    s_pending_ack = 0;
  }

  /* Wait for it to reach vsync. */
  for (;;) {
    bbc_client_receive_message(s_p_bbc, &message);
    if (message.data[0] == k_message_exited) {
      log_cb(RETRO_LOG_INFO, "beebjit BBC thread exited\n");
      s_thread_exited = 1;
      video_cb(NULL, s_width, s_height, 0);
      return;
    }
    if (message.data[0] == k_message_vsync) {
      break;
    }
  }

  t1 = os_time_get_us();
  if (message.data[1]) {
    video_render_full_frame(s_p_video);
  }
  render_process_full_buffer(s_p_render);
  t2 = os_time_get_us();
  /* beebjit's buffer holds both fields line-doubled (640 lines). The panel is
   * 480 lines, so present every other line: halves the upload for free.
   */
  /* Hand the frontend a private copy. Some frontends keep reading the frame
   * after video_cb returns (screenshots, threaded or deferred uploads), by
   * which time the BBC thread may be drawing the next frame into beebjit's
   * buffer. Copying one field is ~1 MB, well under a millisecond on an A53.
   */
  {
    const uint32_t* p_src = render_get_buffer(s_p_render);
    uint32_t y;
    uint32_t h2 = s_height / 2;
    if (!s_p_frame_copy) {
      s_p_frame_copy = malloc((size_t) s_width * h2 * sizeof(uint32_t));
    }
    for (y = 0; y < h2; ++y) {
      memcpy(s_p_frame_copy + y * s_width, p_src + (y * 2) * s_width, s_width * sizeof(uint32_t));
    }
  }
  {
    /* Debug aid: BEEBJIT_DUMP_FRAME=<path> writes the frame given to the
     * frontend at frame BEEBJIT_DUMP_AT (default 200), same format as rt.c.
     */
    static int dumped;
    const char* p_dump = getenv("BEEBJIT_DUMP_FRAME");
    if (p_dump && !dumped) {
      const char* p_at = getenv("BEEBJIT_DUMP_AT");
      uint32_t at = p_at ? (uint32_t) atoi(p_at) : 200;
      if (s_frame_count >= at) {
        FILE* f = fopen(p_dump, "wb");
        if (f) {
          unsigned hdr[4] = { s_width, s_height / 2, s_width * 4, 0 };
          fwrite(hdr, sizeof(hdr), 1, f);
          fwrite(s_p_frame_copy, 4, (size_t) s_width * (s_height / 2), f);
          fclose(f);
          log_cb(RETRO_LOG_INFO, "beebjit dumped frame %u to %s\n", s_frame_count, p_dump);
          {
            uint8_t crtc[k_video_crtc_num_registers];
            int i;
            char buf[128];
            char* p = buf;
            video_get_crtc_registers(s_p_video, crtc);
            for (i = 0; i < k_video_crtc_num_registers; ++i) {
              p += sprintf(p, "%02X ", crtc[i]);
            }
            log_cb(RETRO_LOG_INFO, "beebjit ula=%02X crtc=%s\n",
                   video_get_ula_control(s_p_video), buf);
          }
        }
        dumped = 1;
      }
    }
  }
  video_cb(s_p_frame_copy, s_width, s_height / 2, s_width * sizeof(uint32_t));
  t3 = os_time_get_us();
  if (message.data[2]) {
    render_clear_buffer(s_p_render);
  }
  if (message.data[4]) {
    s_pending_ack = 1;
  }

  drain_audio();
  t4 = os_time_get_us();

  s_t_wait += (t1 - t0);
  s_t_render += (t2 - t1);
  s_t_video += (t3 - t2);
  s_t_audio += (t4 - t3);
  s_t_total += (t4 - t0);
  if (++s_stats_count == k_stats_frames) {
    log_cb(RETRO_LOG_INFO,
           "beebjit frame avg us: emu-wait %u render %u video_cb %u audio %u total %u pc=%04X cycles=%llu\n",
           (unsigned) (s_t_wait / k_stats_frames),
           (unsigned) (s_t_render / k_stats_frames),
           (unsigned) (s_t_video / k_stats_frames),
           (unsigned) (s_t_audio / k_stats_frames),
           (unsigned) (s_t_total / k_stats_frames),
           (unsigned) state_6502_get_pc(bbc_get_6502(s_p_bbc)),
           (unsigned long long) state_6502_get_cycles(bbc_get_6502(s_p_bbc)));
    s_t_wait = s_t_render = s_t_video = s_t_audio = s_t_total = 0;
    s_stats_count = 0;
  }
}

void
retro_reset(void) {
  if (!s_running || !s_p_keyboard) {
    return;
  }
  keyboard_system_key_pressed(s_p_keyboard, k_keyboard_key_f12);
  s_break_held_frames = 3;
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void* data, size_t size) { (void) data; (void) size; return false; }
bool retro_unserialize(const void* data, size_t size) { (void) data; (void) size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) { (void) index; (void) enabled; (void) code; }
unsigned retro_get_region(void) { return RETRO_REGION_PAL; }
void* retro_get_memory_data(unsigned id) { (void) id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void) id; return 0; }
