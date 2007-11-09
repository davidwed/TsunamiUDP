#!/bin/sh

echo "Running aclocal..."
aclocal
echo "Running automake..."
automake
echo "Running autoconf..."
autoconf
./configure
make clean
make

echo
echo "You can do a 'sudo make install' to install binaries into /usr/bin"
echo


