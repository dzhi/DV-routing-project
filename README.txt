TODO:
(Done) Verify that the initialization worked
(Code written but not tested) Actual DV algorithm
Write route tables to log file
0.0.0.0 bug
Actually route a file
    and show the route that it took
Be able to handle killing a router

Report
Slides
Test the demo

Known bugs:
-   The first time the "router" receives a message, it thinks it received the
    message from IP address 0.0.0.0 port 0 for some reason.

Neighbor initialization TODOs:
- will either have user specify label of router on command line or 
automatically assign labels by reading port numbers
- doesn't handle if router is destination in an edge