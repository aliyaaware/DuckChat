#!/usr/bin/bash

#uncomment the topolgy you want. The simple two-server topology is uncommented here.

# Change the SERVER variable below to point your server executable.
SERVER=~/Desktop/Duckchat/Source/server

SERVER_NAME=`echo $SERVER | sed 's#.*/\(.*\)#\1#g'`

# Generate a simple two-server topology
# $SERVER localhost 5000 localhost 5001 &
# $SERVER localhost 5001 localhost 5000 & 
# $SERVER localhost 5001 localhost 5000 localhost 5002 &
# $SERVER localhost 5002 localhost 5001 & 

# Generate a capital-H shaped topology
./server localhost 5000 localhost 5001 &
./server localhost 5001 localhost 5000 localhost 5002 localhost 5003 &
./server localhost 5002 localhost 5001 & 
./server localhost 5003 localhost 5001 localhost 5005 &
./server localhost 5004 localhost 5005 &
./server localhost 5005 localhost 5004 localhost 5003 localhost 5006 &
./server localhost 5006 localhost 5005 &

# Generate a 3x3 grid topology
# $SERVER localhost 5000 localhost 5001 localhost 5003 &
# $SERVER localhost 5001 localhost 5000 localhost 5002 localhost 5005 &
# $SERVER localhost 5002 localhost 5001 localhost 5005 &
# $SERVER localhost 5003 localhost 5000 localhost 5004 localhost 5006 &
# $SERVER localhost 5004 localhost 5001 localhost 5003 localhost 5005 localhost 5007 &
# $SERVER localhost 5005 localhost 5002 localhost 5004 localhost 5008 &
# $SERVER localhost 5006 localhost 5003 localhost 5007 &
# $SERVER localhost 5007 localhost 5006 localhost 5004 localhost 5008 &
# $SERVER localhost 5008 localhost 5005 localhost 5007 &


echo "Press ENTER to quit"
read
pkill $SERVER_NAME
