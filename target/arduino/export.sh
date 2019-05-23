#!/bin/sh

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
$CMD "$SRC"/lmic.h "$TARGET"/src
$CMD "$SRC"/../../lmic "$TARGET"/src
$CMD "$SRC"/hal "$TARGET"/src
$CMD "$SRC"/../../aes "$TARGET"/src
$CMD "$SRC"/examples "$TARGET"
$CMD "$SRC"/board.h "$TARGET"/src/lmic
$CMD "$SRC"/hw.h "$TARGET"/src/lmic
