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
   and `beebjit.ini` from this folder.

3. Put disc images in `ROMS/BBC` on the SD card. Any folder name listed under
   `[friendly]` in `global.ini` (bbc, bbcmicro, beeb, acornbbc) works.

Default pad mapping (change in RetroArch core options):

| Pad | BBC key |
|---|---|
| D-pad | `:` `/` `Z` `X` (up, down, left, right) |
| A / B | Return / Space |
| X / Y | Shift / Ctrl |
| L / R | Escape / Copy |
| L2 / R2 | f0 / BREAK |
| Start / Select | Escape / Space |

muOS quits RetroArch with MENU held plus START, and opens the RetroArch quick
menu with MENU held plus SELECT.
