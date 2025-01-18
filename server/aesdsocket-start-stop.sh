#!/bin/sh
SOCKET_PATH=/usr/bin/aesdsocket

if [ "$1" = "start" ]; then
    start-stop-daemon --start -n aesdsocket --exec $SOCKET_PATH -- -d
elif [ "$1" = "stop" ]; then
    start-stop-daemon -K -n aesdsocket
else
    echo "Usage: $0 start|stop" >&2
    exit 3
fi