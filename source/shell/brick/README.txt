TRACKWERK — FIRST START ON TRIMUI BRICK
Version 1.3.0 · Kid Bunka

ALREADY RUNNING KNULLI?
Start at step 3. Do not reflash or format a working card just to install the tracker.

1. CHOOSE THE CORRECT OPERATING SYSTEM
This package is for the original TrimUI Brick / Brick Hammer with KNULLI.
The working reference is KNULLI Scarab, official release 20260511.
Official release and device-specific downloads:
https://github.com/knulli-cfw/knulli-linux/releases/tag/20260511
Original Brick/Hammer image:
https://github.com/knulli-cfw/knulli-linux/releases/download/20260511/knulli-a133-trimui-brick-scarab-20260511.img.gz

Use the correct image for your device; other handhelds need their own image.
KNULLI is a separate project, supplied unmodified through its official download.
It is not inside the Pocket Tracker ZIP.

2. PREPARE A NEW CARD ONLY IF YOU NEED KNULLI
Back up any card you intend to reuse. Writing an OS image erases that card.
Use Raspberry Pi Imager or a compatible image writer, choose the downloaded
image and the correct spare microSD card, write/verify, then boot it in the
Brick. Allow its first-boot setup to finish.
https://www.raspberrypi.com/software/

For direct copying from macOS/Windows, the SHARE data partition must be in a
format your computer can read, such as exFAT. A Linux ext4 SHARE is not natively
readable in Finder/Explorer. KNULLI can prepare a data partition, but formatting
it erases the data: do this only on a new/spare card or after a complete backup.
Do not format the small KNULLI boot partition or your working sample library.

On a NEW/spare card only, after the first KNULLI boot, the data-format menu is:
START > System Settings > Frontend Developer Options > Format a disk >
Device: Internal > File system: exFAT > Format now. Reboot when it finishes.
This erases the selected data partition. Verify the card/device selection and
back up anything important first; leave an already working card unchanged.
Official SD-card and storage guidance:
https://knulli.org/play/add-games/formatting/
https://knulli.org/play/add-games/accessing-sd-card/
https://knulli.org/play/add-games/game-storage/

3. INSTALL POCKET TRACKER
Unzip TrackWerk-1.3.0-Brick-KNULLI-First-Install.zip into:
SHARE/roms/ports/

After extracting, you should have:
roms/ports/TrackWerk.sh
roms/ports/trackwerk/trackwerk.aarch64
roms/ports/trackwerk/factory/Presets/

Keep any existing trackwerk/data folder. Your projects, samples,
recordings and edited presets belong there. PortMaster installation or updating
is not required to start this standalone tracker.

4. FIRST START AND FACTORY LIBRARY
TrackWerk starts the app before preparing the additional factory library.
After the first displayed frame, KNULLI's own Python runs the verified library
installer in the background. Nothing needs to be installed on your computer.

The library contains 2,432 presets. Complete categories appear in a Factory-...
subfolder under Instruments, Delays, Reverbs, Mastering, SampleGranular and
StudioFX. Reopen the browser to refresh a listing opened during setup.

Existing projects, samples and edited presets are kept. An interrupted setup
resumes from private staging on the next app start. A completed library marker
also preserves intentional deletion, renaming and rearrangement of presets.

Setup runs only after the app is visible, at lower CPU priority. It keeps
working while the app is open until the library is complete. Closing the app
stops the worker safely; the next launch resumes any remaining work. The native
first-frame watchdog is independent. Logs are in roms/ports/trackwerk/log.txt.

Optional computer preparation, with Python 3 already installed:
python3 "/media/YOUR_CARD/roms/ports/trackwerk/tools/prepare_presets.py" --library
Windows (replace E: with the actual SHARE drive):
py -3 "E:\roms\ports\trackwerk\tools\prepare_presets.py" --library
Wait for Ready, safely eject, then start TrackWerk. No manual marker is needed.

5. START
Wait for all computer copies to finish, safely eject the card, insert it into
the powered-off Brick and turn it on. Choose Ports > TrackWerk.
If it is absent, refresh KNULLI's game list. The package keeps the proven SDL
software-surface compatibility mode and the existing physical controls.

6. ADD YOUR SAMPLES
Copy your audio and category folders into:
SHARE/roms/ports/trackwerk/data/Samples

In the app choose INSTRUMENT > LOAD > Samples / Audio.
Up/Down selects; A opens a folder or loads a sound; START auditions it.
R+Left or the '..' row opens the parent folder; B closes the browser.
You do not need Android-style folder permissions or a library scan on KNULLI.

7. FOLLOW THE ILLUSTRATED ENGLISH GUIDE
https://kidbee85-beep.github.io/TrackWerk/guide.html

This is beta software. Musical workflows have been user-tested on a Brick with
no errors reported so far; the full test workbook is still in progress. USB
interfaces and the experimental direct iPhone input require separate hardware
tests. Start with a short known WAV and save a test project before an important
session. Back up your data folder regularly.
