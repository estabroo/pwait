pwait for linux

Waits on another process or multiple processes to exit.  Similar to Solaris's pwait.
It uses a netlink connection to the kernel to listen for process exiting events.


Usage:

pwait pid [pid ..]


It needs root or certain capabilities to fully function.  When using the debian package
it does "setcap cap_net_admin,cap_kill=ep /usr/bin/pwait" after it's installed.  If you
are installing it by hand you'll need to either do the same or make it setuid root.
It is far less dangerous to use setcap, but there still is potential risk of privilege
escalation.
