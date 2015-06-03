TODO:
(Done) Verify that the initialization worked
Actual DV algorithm
Write route tables to log file
0.0.0.0 bug
Actually route a file
    and show the route that it took
Be able to handle killing a router

Report
Slides
Test the demo

-------------------------------------------------------------------------------

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

Neighbor initialization TODOs:
- will either have user specify label of router on command line or 
automatically assign labels by reading port numbers
- doesn't handle if router is destination in an edge