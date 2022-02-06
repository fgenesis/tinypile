#!/bin/sh
## Run this script to create amalgamated .c/.cpp files

hascmd() {
    type "$1" >/dev/null 2>&1
    return $?
}

lua=$(for x in luajit lua lua51 lua52 lua53 lua54 lua5.1 lua5.2 lua5.3 lua5.4; do
	if hascmd $x; then
		echo $x
		break
	fi
done)

if [ x$lua == x ]; then
	echo "Lua 5.1 or newer is required to run this! Exiting."
	exit 1
fi

echo Using $lua

$lua dep/amalg.lua src/tio
$lua dep/amalg.lua src/tio_vfs
$lua dep/amalg.lua src/tio_zip
