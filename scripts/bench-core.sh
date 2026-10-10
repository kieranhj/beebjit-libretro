#!/usr/bin/env bash
# Benchmark a core on the handheld: run N frames with vsync and audio sync disabled
# and time it. Speed factor = (N/50) / seconds. 1.0 = exactly real time for a 50 Hz core.
# Usage: ./bench-core.sh [so] [disc] [frames]
set -euo pipefail
HOST="${RG_HOST:-root@192.168.1.175}"
SO="${1:-out/b2_libretro.so}"
DISC="${2:-/mnt/sdcard/ROMS/BBC/bs-beeb-niccc.dsd}"
N="${3:-1500}"

scp -q -O -o BatchMode=yes "$SO" "$HOST:/tmp/b2bench.so"
ssh -o BatchMode=yes "$HOST" "
  if pidof retroarch >/dev/null; then echo 'RetroArch already running on device; aborting'; exit 2; fi
  . /opt/muos/script/var/func.sh
  HOME=\"\$(GET_VAR device board/home)\"; export HOME
  SETUP_SDL_ENVIRONMENT >/dev/null 2>&1
  RA_ARGS=\$(CONFIGURE_RETROARCH)
  printf 'video_vsync = \"false\"\naudio_sync = \"false\"\nvideo_frame_delay = \"0\"\nfastforward_ratio = \"0.0\"\nvideo_font_enable = \"false\"\n' > /tmp/bench.cfg
  cd /tmp
  S=\$(date +%s.%N)
  BEEBJIT_AUTOKEY_FRAME=${AUTOKEY:-100} timeout -s KILL 120 retroarch -f \$RA_ARGS --appendconfig=/tmp/bench.cfg --max-frames=$N -L /tmp/b2bench.so '$DISC' > /tmp/bench.log 2>&1 || true
  E=\$(date +%s.%N)
  awk -v s=\$S -v e=\$E -v n=$N 'BEGIN{t=e-s; printf(\"frames=%d  wall=%.2fs  speed=%.2fx real time\n\", n, t, (n/50)/t)}'
"
