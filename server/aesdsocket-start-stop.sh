#! /bin/sh

if [ -z "$1" ]; then
    echo "usage: $0 {start|stop}"
    exit 1
fi

case "$1" in
    start)
        echo "Starting aesdsocketserver"
        start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Stopping aesdsocketserver"
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "usage: $0 {start|stop}"
        exit 1
        ;;
esac
exit 0