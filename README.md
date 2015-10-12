pwait for linux

Waits on another process or multiple processes to exit.  Similar to Solaris's pwait.
It uses a netlink connection to the kernel to listen for process exiting events.


Usage:

pwait pid [pid ..]
