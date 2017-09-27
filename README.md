
# filterdrv

This is a homework I made for a company when applying for a Windows Driver Developer position.
The task was to create a console application that prints a log entry every time a file is opened
in the system and can be interrupted by Ctrl+C but it was not allowed to use signals for the implementation (so that Ctrl+C could be easily changed to something else).
For the implementation I used Microsoft's nullFilter and minispy examples.

## Building and testing

On the development machine:
+ You can build the application and the driver using build_all.bat in *Win7 x64 checked environment* (WinDDK 7600.16385.1).

On the target machine:
1. Install filterdrv.inf
1. In an admin console enter to load the driver: net start filterdrv
1. Start the application: testdrv.exe
1. You can interrupt it with Ctrl+C

Tested on Win7 SP2 x64 target.

