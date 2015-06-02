Current state of affairs:

Run
        ./myrouter 10000
in one terminal window, and
        ./myrouter 10001
in another.
Then in another window, run
        ./sillytestscript.sh
to send a bogus DV packet to 10000.
The "router" on 10000 is configure to broadcast its DV to 10001 and 10005.

The actual DV algorithm (as well as a lot more of the core functionality) is
not yet implemented. See "TODO" comments for more known issues.

Known bugs:
-   The first time the "router" receives a message, it thinks it received the
    message from IP address 0.0.0.0 port 0 for some reason.