#!/bin/sh
start ()
{
    echo -e '\033[9;0]' > /dev/tty1

    echo "Mounting SD Card Partition #2+"

    mkdir /media/sd
    mount -t ext4 /dev/mmcblk0p2 /media/sd

    # create game file and set permissions
    touch /usr/bin/game
    adduser ectf --shell /usr/bin/game --disabled-password --gecos ""
    chown ectf:ectf /usr/bin/game
    chmod o+x /usr/bin/game

    # uio stuff
    chmod a+wr /dev/uio*
    a=$(grep mesh_drm /sys/class/uio/uio*/maps/map*/name | cut -d'/' -f5)
    if [ ! -z "$a" ]; then
        mv /dev/$a /dev/mesh_drm
    fi

    # set tty device with correct baud
    stty -F /dev/ttyPS0 speed 115200

    # login ectf
    /bin/login -f ectf

    # load and launch game
    mesh-game-loader
    game

    # restart so user doesnt fall through to petalinux shell
    echo "Game over. Restarting system..."
    sleep 5
    shutdown -r now

}
stop ()
{
    echo " Stopping..."
}
restart()
{
    stop
    start
}
case "$1" in
    start)
start; ;;
    stop)
    stop; ;;
    restart)
    restart; ;;
    *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1

esac

exit 0
