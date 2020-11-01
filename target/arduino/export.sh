#!/bin/sh
#
# Copyright (C) 2016, Matthijs Kooijman <matthijs@stdin.nl>
#
# --- Revised 3-Clause BSD License ---
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of the copyright holder nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# Export script for unix operating systems (e.g. Linux and OSX).
#

SRC=$(cd "$(dirname "$0")"; pwd)

usage() {
	echo "$0 [--link] TARGET_DIR"
	echo "Create an Arduino-compatible library in the given directory, overwriting existing files. If --link is given, creates symbolic links for easy testing."
}


# This recursively copies $1 (file or directory) into $2 (directory)
# This is essentially cp --symbolic-link, but POSIX-compatible.
create_links() {
	local TARGET=$(cd "$2" && pwd)
	local SRCDIR=$(cd "$(dirname "$1")" && pwd)
	local SRCNAME=$(basename "$1")
	(cd "$SRCDIR" && find "$SRCNAME" -type d -exec mkdir -p "$TARGET/{}" \; -o -exec ln -s -v "$SRCDIR/{}" "$TARGET/{}" \; )
}


CMD="cp -r -v"
case "$1" in
	--help)
		usage
		exit 0
	;;
	--link)
		CMD="create_links"
		shift;
	;;
	--*)
		echo "Unknown option: $1" 1>&2
		exit 1
	;;
esac

TARGET=$1

if [ -z "$TARGET" ]; then
	usage
	exit 1
fi

if ! [ -d "$(dirname "$TARGET")" ]; then
	echo "Parent of $TARGET should exist" 1>&2
	exit 1
fi


if [ -e "$TARGET" ]; then
	echo -n "$TARGET exists, remove before export? [yN]"
	read answer
	if [ "$answer" = "y" ]; then
		rm -rf "$TARGET"
	fi
fi

mkdir -p "$TARGET"/src

# This copies or links the relevant directories. For the hal and lmic
# directories, the contained files are copied or linked, so that when
# linking relative includes still work as expected
$CMD "$SRC"/library.properties "$TARGET"
$CMD "$SRC"/basicmac.h "$TARGET"/src
$CMD "$SRC"/../../lmic "$TARGET"/src
$CMD "$SRC"/hal "$TARGET"/src
$CMD "$SRC"/../../aes "$TARGET"/src
$CMD "$SRC"/examples "$TARGET"
$CMD "$SRC"/board.h "$TARGET"/src/lmic
$CMD "$SRC"/hw.h "$TARGET"/src/lmic

# Then copy/link the common files (e.g. pinmap) into each example
# directory
for E in "$TARGET"/examples/*; do
	$CMD "$SRC"/examples-common-files/* "$E"
done
