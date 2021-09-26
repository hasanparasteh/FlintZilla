#!/bin/sh

if ! [ -x "$(command -v make)" ]; then
  echo 'Error: make is not installed.' >&2
  exit 1
fi

if ! [ -x "$(command -v pkg-config)" ]; then
  echo 'Error: make is not installed.' >&2
  exit 1
fi

[ ! -d './build' ] && mkdir build/
make cli && make gui && make main