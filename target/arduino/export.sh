#!/bin/sh

SRC=$(cd "$(dirname "$0")"; pwd)

usage() {
	echo "$0 [--link] TARGET_DIR"
	echo "Create an Arduino-compatible library in the given directory, overwriting existing files. If --link is given, creates symbolic links for easy testing."
}


CPOPTS=
case "$1" in
	--help)
		usage
		exit 0
	;;
	--link)
		CPOPTS=--symbolic-link
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

mkdir -p "$TARGET"/src
cp $CPOPTS -f -v "$SRC"/library.properties "$TARGET"
cp $CPOPTS -f -v "$SRC"/lmic.h "$TARGET"/src
cp $CPOPTS -r -f -v "$SRC"/../../lmic "$TARGET"/src
cp $CPOPTS -r -f -v "$SRC"/../../aes "$TARGET"/src
cp $CPOPTS -r -f -v "$SRC"/hal "$TARGET"/src
cp $CPOPTS -r -f -v "$SRC"/examples "$TARGET"
