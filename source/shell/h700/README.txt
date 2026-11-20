TrackWerk on Anbernic H700 handhelds (KNULLI) - community test

STATUS: untested on any device. Nobody has run TrackWerk on an RG40XX V or any other H700 device yet. This package exists so
that the first person with one can try it and report back. Please read "What is known" first.

Devices
  Written for the Anbernic RG40XX V (640x480). KNULLI runs the same H700 image on the RG35XX H, Plus, SP
  and 2024, the RG28XX, the RG34XX (720x480), the RG40XX H and the RG CubeXX (720x720), so the same
  package is expected to start on them too. TrackWerk scales its 640x480 design to other screen shapes
  (as on the R36 Ultra), but none of these devices has been tried. Report the model with the log.

What it is
  The same aarch64 program as the TrimUI Brick build (Cortex-A53, glibc 2.31 or newer, the system's
  SDL2, nothing bundled). Only the launcher differs: the first start tries the GPU renderer and, if no
  first frame appears within 25 seconds, tries the window-surface mode once. The mode that worked is
  remembered in trackwerk/video-mode.txt. POCKETTRACKER_VIDEO=renderer or surface forces one mode.

What is known (from public documentation, not from a device)
  - H700 SoC, four Cortex-A53 cores (aarch64), 1 GB RAM, 640x480 screen - TrackWerk's native size.
  - KNULLI supports it with its h700 image. Use KNULLI "Scarab" (2026-05-11) or newer: older images
    are not meant for the revised RG40XX H/V units.
  - KNULLI's H700 image draws through its own patched SDL2 (Mali, framebuffer). The Brick uses a
    different graphics stack, so the display path is the part that may differ.
  - glibc 2.38 on KNULLI, so the program's glibc 2.31 minimum is met.

What is NOT known
  The exact button set and names the pad reports, the speaker/headphone/USB audio devices, whether
  the screen needs any rotation, and whether the renderer or the surface mode is the one that works.
  The launcher writes what it can see (model, kernel, SDL environment, "aplay -l", input devices) to
  trackwerk/log.txt on every start.

PLEASE WAIT - the first start is slow
  - The first start copies about 2,400 factory presets to the card. Depending on the card this takes
    from under a minute to several minutes. The screen can stay black or on the menu meanwhile.
  - If the first display mode does not show a picture, the launcher waits 25 seconds and then tries a
    second mode. So up to about 50 seconds can pass before TrackWerk appears. This is expected.
  - Do not power the device off and do not press buttons during this time. If nothing has appeared
    after 5 minutes, hold the power button, then see "If it does not start".
  - Later starts are fast and use the display mode that worked.
  - This version has not been run on an RG40XX V yet. If you would rather not be the first, wait for
    a report from someone who has, before you install it.

Install (new)
  1. Power the device off and put the SD card in your computer.
  2. Copy TrackWerk.sh and the trackwerk folder into roms/ports/ on the SHARE partition.
  3. Optional menu art: python3 setup_brick_menu.py --target /media/CARD/roms/ports/trackwerk
     (review the plan, then repeat with --apply). It only adds TrackWerk's own entry.
  4. Eject, boot, open Ports > TrackWerk. Put samples in roms/ports/trackwerk/data/Samples.
  Do not overwrite an existing TrackWerk folder from another device's card: use the program update
  of that device instead.

If it does not start
  Take trackwerk/log.txt (and log.previous.txt if present) from the card and send it with the exact
  KNULLI version (Start > System Settings > Version). The log contains no personal data beyond the
  device summary. Do not include iPhone pairing records.

What this package does not claim
  USB audio, direct iPhone recording and any other hardware feature are not part of this test. Nothing
  here changes KNULLI, PortMaster, the kernel or the graphics drivers.
