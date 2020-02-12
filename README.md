# Basic MAC

Basic MAC is a portable implementation of the LoRa™ Alliance's LoRaWAN™
specification in the C programming language. It is a fork of IBM's LMiC
library, and supports multiple regions, which are selectable at compile and/or
run time. It can handle Class A, Class B, and Class C devices.

## This repository
This repository contains an Arduino port of the BasicMAC library.
However, the port is done without changing the structure of the
repository itself, so development can continue on other targets at the
same time.

### Development status

This branch is still under heavy development and should be considered an
early testing version. Some commits are unfinished or hacky (in
particular the debug printing support is a bit of a mess in an attempt
to reduce RAM usage on AVR).

There will likely be some history editing and rebasing in the future.
Also, the API is likely to change in a breaking way.

However, the library should actually work as it is now.

### Using this repository on Arduino

This repository is not directly usable as an Arduino library, in order
to keep the directory structure more generic. To use it in Arduino,
there is a bash script that generates an Arduino library out of it. To do so,
make a git checkout of this repository and from the root of it, run:


	./target/arduino/export.sh --link ~/Arduino/libraries/BasicMAC

The `--link` parameter makes the script generate symlinks into the
original repository, allowing you to make changes to the library there
and have them reflected in the Arduino version directly. If you omit
`--link`, copies will be made instead.

The directory name passed is created and becomes the library directory,
it should be in your Arduino sketchbook's `libraries` directly (the path
shown should work with the default Arduino IDE configuration).

This script was tested on Linux, should work on OSX as well, might work
on Windows (if you have bash).

### Hardware support

This port is intended to work on any Arduino board, regardless of
architecture. It was tested on AVR, SAMD and STM32 boards, and with
SX1272, SX1276 and SX1262 radios.

Unfortunately, BasicMAC is quite a bit bigger than LMIC, so it seems it
does not fit in an atmega328p anymore (maybe some more optimization can
be done, but now the ttn-otaa example compiles down to 35k of flash,
with only 32k available).

### Converting from LMIC

BasicMAC is based on LMIC and largely works in the same way. Sketches
can likely be largely reused, though some small changes need to be made.
See for example a5cf1f3 on the changed needed to the example.
Additionally the pinmap was significantly changed, so look at one of the
examples to see how that looks now (the examples have pinmaps for
different boards, the last one is intended for custom boards and has
some useful comments).
