#!/bin/sh
if [ ! -d "/usr/local/bin" ]; then
  mkdir -m 755 "/usr/local/bin"
  chown root:admin "/usr/local/bin"
fi
if [ ! -d "/usr/local/lib" ]; then
  mkdir -m 755 "/usr/local/lib"
  chown root:admin "/usr/local/lib"
fi
