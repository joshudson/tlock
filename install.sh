#!/bin/sh
PREFIX=/usr/local
if [ $# -gt 0 ]
then :
	case "$1" in
	--prefix=*) PREFIX="`echo -n "$1" | sed -e 's/^--prefix=//'`" ;;
	*) echo "$1" not understood ; exit 1 ;;
	esac
fi
[ -z "$CFLAGS" ] && CFLAGS=-Os
export CFLAGS
[ -x ./tlock ] || make tlock || exit
install -m 555 tlock $PREFIX/bin/tlock
install -m 444 tlock.1 $PREFIX/share/man/man1/tlock.1
