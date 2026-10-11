# Installing on muOS (Anbernic RG40XXV and friends)

Paths below are for muOS "NeXT" (2025). Copy with scp over SSH (user `root`,
default password `root`).

1. Core and info file:

       scp out/beebjit_libretro.so   root@DEVICE:/opt/muos/share/core/
       scp beebjit_libretro.info     root@DEVICE:/opt/muos/share/emulator/retroarch/info/

   If RetroArch is running, copy to `/tmp` first and `mv` into place so the
   running process keeps its old mapping.

2. Tell muOS about the system. Create
   `/opt/muos/share/info/assign/Acorn BBC Micro/` containing `global.ini`
   and `beebjit.ini` from this folder, and copy `beebjit.cfg` to
   `/opt/muos/share/info/config/beebjit/`. muOS sets RetroArch to stretch
   every core to the full panel; that override makes it use the core's
   aspect ratio instead.

3. Put disc images in `ROMS/BBC` on the SD card. Any folder name listed under
   `[friendly]` in `global.ini` (bbc, bbcmicro, beeb, acornbbc) works.

The core defaults to a Model B. For a disc that needs a Master 128, open the
RetroArch quick menu (MENU held plus X), go to Core Options, set
"Machine" to "Master 128", and save it as a game option so it sticks for that
disc only. Restart the content for the change to take effect.

Default pad mapping (change in RetroArch core options):

| Pad | BBC key |
|---|---|
| D-pad | `:` `/` `Z` `X` (up, down, left, right) |
| A / B | Return / Space |
| X / Y | Shift / Ctrl |
| L / R | Escape / Copy |
| L2 / R2 | f0 / BREAK |
| Start / Select | Escape / Space |

muOS uses MENU as the RetroArch hotkey modifier on the RG40XXV (from
`/opt/muos/device/control/retroarch.device.cfg`): MENU plus X opens the
RetroArch quick menu, MENU plus START quits to muOS, MENU plus Y toggles the
FPS counter, MENU plus L2 loads a state.
