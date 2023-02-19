#!/bin/sh

AUTORECONF=$(which autoreconf)

if test -z "$AUTORECONF"; then
  echo "Program autoreconf not found. Please, install GNU Autoconf."
  exit 1
fi

$AUTORECONF -i
