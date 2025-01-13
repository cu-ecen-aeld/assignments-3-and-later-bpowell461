#!/bin/sh
SOCKET_PATH=/usr/bin/aesdsocket

start-stop-daemon --status --name aesdsocket
ret=$?
if [ $ret -ne 0 ]; then
    start-stop-daemon --name aesdsocket --start --startas $SOCKET_PATH -- -d
else
    start-stop-daemon --stop --signal TERM --name aesdsocket
fi