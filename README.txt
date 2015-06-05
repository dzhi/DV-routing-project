TODO:
(Done) Verify that the initialization worked
(Done) Actual DV algorithm
(Done) Write route tables to log file
(Fixed) 0.0.0.0 bug (the first time the "router" receives a message, it thinks
    it received the message from IP address 0.0.0.0 port 0 for some reason)
(TODO) Actually route a file
    (DONE) and show the route that it took 
    ... by having each router along the way write info to its log file
     (each router it passes through records from whence it came, where it goess)


	(DONE) Handle a data packet that needs forward (forward and write to log)
	(DONE) Handle a data packet that is destined for you (write info and body to log)
(Done) Conditionally make router act as a traffic generator and write to its log


(Done) Detect when router is killed
(Done) Be able to handle killing a router
(Done) Start overall program (probably shell script to start multiple routers in different terminal windows)

ok
(TODO) Report
(TODO) Slides
(TODO) Test the demo

(Done I think) Known bugs/issues:
- If you kill a router and restart it, doesn't get re-added to my_dv.
	(not sure if necessary)
- Each router assumes all of its neighbors in file are initially live.
	DV table can contain nodes before they've been initiated. 
	(not sure if issue or safe to assume all nodes are present unless killed)

