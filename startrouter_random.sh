#!/bin/bash


#port = $((RANDOM%10005+10000)) # random port number 10000 to 10005


declare -a Ports=('10000' '10001' '10002' '10003' '10004' '10005')
#echo ${Ports[@]} # print all array entries


for i in 0 1 2 3 4 5
do
	max_ind=${#Ports[@]} #max ind is port length

	index=$((RANDOM%$max_ind+0)) # generate number 0 to maxind
	port=${Ports[$index]}
	#echo "${portArr[@]}" # which port got chosen


	# start a router with that port number
	xterm -e ./myrouter $port &
	sleep 0.2

	# remove used port from array
	Ports=(${Ports[@]:0:$index} ${Ports[@]:$(($index + 1))})

done
