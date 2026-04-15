# Pi1541

Commodore 1541/1581 emulator for the Raspberry Pi

Pi1541 is a real-time, cycle exact, Commodore 1541 disk drive emulator that can run on a Raspberry Pi 3A, 3B or 3B+. The software is free and I have endeavored to make the hardware as simple and inexpensive as possible.

Pi1541 provides you with an SD card solution for using D64, G64, NIB and NBZ Commodore disk images on real Commodore 8 bit computers such as;-
Commodore 64
Commodore 128
Commodore Vic20
Commodore 16
Commodore Plus4

See https://cbm-pi1541.firebaseapp.com/ for SD card and hardware configurations.

Toolchain Installation
----------------------

On Windows use GNU Tools ARM Embedded tool chain 5.4:
https://launchpad.net/gcc-arm-embedded/5.0/5-2016-q2-update
and Make:
http://gnuwin32.sourceforge.net/packages/make.htm


On dpkg based linux systems install:
(Tested on osmc/rpi3)
```
apt-get install binutils-arm-none-eabi gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
```

On RHEL/Centos/Fedora systems follow the guide at:
https://web1.foxhollow.ca/?menu=centos7arm
(Tested on Centos7/x64 with GCC7)
https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads/7-2017-q4-major

Building
--------
```
make
```
This will build kernel.img


In order to build the Commodore programs from the `CBM-FileBrowser_v1.6/sources/` directory, you'll need to install the ACME cross assembler, which is available at https://github.com/meonwax/acme/

Disk Visualization
------------------

The disk info screen now displays a graphical floppy disk visualization that
shows real-time drive activity. When a D64 disk image is loaded, the old
rectangular BAM grid is replaced with a circular disk rendering:

- **Track/sector layout**: Concentric rings represent tracks (outermost = track 1). Each ring is divided into sectors based on the D64 zone layout (21/19/18/17 sectors per track).
- **BAM coloring**: Sectors are colored based on the Block Availability Map — green for allocated (has data), black for free, and cyan for directory track 18.
- **Active sector highlighting**: When the drive LED is on (indicating a read/write), the sector currently under the head is highlighted in red.
- **Read head**: A small carriage on the south side of the disk moves vertically to track the current head position. The current track number is displayed inside the head.
- **Signal graph**: The existing IEC bus signal graph (ATN, DATA, CLOCK) continues to render across the bottom of the screen below the disk.

The visualization uses integer-only math (`int_sqrt`, `int_atan2`) to avoid
floating-point dependencies on the bare-metal Raspberry Pi kernel.

### Files

- `src/DiskVisualizer.h` — Class interface and color constants
- `src/DiskVisualizer.cpp` — Rendering engine (full disk render + lightweight head updates)
- `src/FileBrowser.cpp` — Layout integration in `DisplayDiskInfo()`
- `src/main.cpp` — Real-time `UpdateHead()` hook in `UpdateScreen()`
