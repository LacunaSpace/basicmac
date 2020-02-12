# BasicMAC

BasicMAC is a portable implementation of the LoRa™ Alliance's LoRaWAN™
specification in the C programming language. It is a fork of IBM's LMiC
library, and supports multiple regions, which are selectable at compile and/or
run time. It can handle Class A, Class B, and Class C devices.

## This repository
This repository contains an Arduino port of the BasicMAC library.
However, the port is done without changing the structure of the
repository itself, so development can continue on other targets at the
same time.

### Development status

This master branch should be working, testing and contributions are
welcome. There are still some rough edges, so this should not be
considered a finished version.

Initially, development of this repository has been quite unstable, with
lots of history editing and rebasing, but the intention is to keep the
master branch stable now (i.e. no force pushes). Rebasing and history
editing might still happen in other branches and is encourage in
pullrequests to keep history clean.

The API is still likely to change in a breaking way, especially wrt
configuration.

### Relation to other projects

BasicMAC is a privately developed fork of LMIC, which was released
publically by Semtech in 2019. This repository borrows heavily from the
Arduino LMIC port that was first published at
https://github.com/matthijskooijman/arduino-lmic/. There are some other
LMIC versions (notably https://github.com/mcci-catena/arduino-lmic) for
Arduino based off matthijskooijman's version and have seen more
development, but no effort has been made to incorporate any changes from
there.

Ideally, all of these LMIC and BasicMAC-based projects would unify their
efforts in a repository that is not Arduino-specific, but given that
there have been signficant changes in both BasicMAC and the MCCI
versions compared to the original IBM LMIC version, this might be a
challenge (both technically, and project-wise).

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

### Using this repository without Arduino

To build and upload without Arduino, a makefile-based build system is
present in the `projects` subdirectory.  It currently only supports
STM32, on some specific boards.

To use it, go into the example project directory (`projects/ex-join`)
and run `make`.

Some effort has been made to keep these builds working, but testing of
the code in this repostiory has only been done under Arduino, so the
Makefile-based builds might very well be broken.

### Hardware support

This port is intended to work on any Arduino board, regardless of
architecture. It was tested on AVR, SAMD and STM32 boards, and with
SX1272, SX1276 and SX1262 radios.

Unfortunately, BasicMAC is quite a bit bigger than LMIC, so it seems it
does not fit in an atmega328p anymore. PROGMEM optimizations have not
been applied yet, but those would only free up RAM, not flash (and now
the ttn-otaa example compiles down to 35k of flash, with only 32k
available).

### Converting from LMIC

BasicMAC is based on LMIC and largely works in the same way. Sketches
can likely be largely reused, though some small changes need to be made.
See for example commit 3ccfb5b for the changes needed to the examples.

Additionally the pinmap was significantly changed, so look at one of the
current examples to see how that looks now.
