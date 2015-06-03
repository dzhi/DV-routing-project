TODO:
(Done) Verify that the initialization worked
(Code written but not tested) Actual DV algorithm
Write route tables to log file
(Fixed) 0.0.0.0 bug (the first time the "router" receives a message, it thinks
    it received the message from IP address 0.0.0.0 port 0 for some reason)
Actually route a file
    and show the route that it took
(Done) Detect when router is killed
(Done) Be able to handle killing a router
Start overall program (probably shell script to start multiple routers in different terminal windows)


Report
Slides
Test the demo

Known bugs/issues:
- If you kill a router and restart it, doesn't get re-added to my_dv.
	(not sure if necessary)
- Each router assumes all of its neighbors in file are initially live.
	DV table can contain nodes before they've been initiated. 
	(not sure if issue or safe to assume all nodes are present unless killed)


