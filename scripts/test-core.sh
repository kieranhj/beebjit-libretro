#!/usr/bin/env bash
# Copy out/b2_libretro.so to the handheld as /tmp/b2test.so and run it headlessly for
# ~25 s against Welcome.ssd. Prints whether it survived and its CPU use (500 = one core).
# Does not touch the installed core. Refuses to run if RetroArch is already running.
set -euo pipefail
HOST="${RG_HOST:-root@192.168.1.175}"
SO="${1:-out/b2_libretro.so}"
DISC="${2:-/mnt/sdcard/ROMS/BBC/Welcome.ssd}"

scp -O -o BatchMode=yes "$SO" "$HOST:/tmp/b2test.so"
ssh -o BatchMode=yes "$HOST" "
  if pidof retroarch >/dev/null; then echo 'RetroArch already running on device; aborting'; exit 2; fi
  . /opt/muos/script/var/func.sh
  HOME=\"\$(GET_VAR device board/home)\"; export HOME
  SETUP_SDL_ENVIRONMENT >/dev/null 2>&1
  RA_ARGS=\$(CONFIGURE_RETROARCH)
  cd /tmp
  (timeout -s KILL 25 retroarch -v -f \$RA_ARGS -L /tmp/b2test.so '$DISC' > /tmp/b2test.log 2>&1 &)
  sleep 10
  P=\$(pidof retroarch || true)
  if [ -z \"\$P\" ]; then
    echo 'RESULT: died within 10 s'
  else
    a=\$(awk '{print \$14+\$15}' /proc/\$P/stat); sleep 5; b=\$(awk '{print \$14+\$15}' /proc/\$P/stat)
    echo \"RESULT: alive, cpu ticks/5s=\$((b-a)) (500 = one full core)\"
  fi
  sleep 12
  L=\$(ls -t /mnt/mmc/MUOS/log/retroarch/* | head -1)
  grep -n 'Geometry\|Set audio input rate\|Loading game\|Segm' \"\$L\" | head -5
"
