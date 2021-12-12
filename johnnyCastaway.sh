#!/bin/bash

VERSION=.01

# what time is it, uses unix DATE command
HOUROFDAY=`date +%H`

# do we run  forever
RUNFOREVER=0

# what is the raft level [0-4]
RAFTLEVEL=0;

# is it (christmas, NYE, STPAT,halloween,regular day)
# is it a scene, or just walking

############################################################
# Help                                                     #
############################################################
Help()
{
   # Display Help
   echo "Johnny Castaway - The Bash Script."
   echo
   echo "Syntax: johnnyCastaway.sh [-h|i|V]"
   echo "options:"
   echo "h     Print this Help."
   echo "i     Print the Intro"
   echo "V     Print software version and exit."
   echo "f     Loop and run forever."
   
   echo
}

Version()
{
  echo $VERSION
}

Intro()
{
	echo "This is the story of Johnny Castaway"
	echo "Johnny is stranded on a small island."
	echo "There is naught but a single palm tree."
	echo "Johnny wears white shorts and a "
	echo "boating hat.  His beard is scruffy."
}

############################################################
############################################################
# Main program                                             #
############################################################
############################################################
############################################################
# Process the input options. Add options as needed.        #
############################################################
# Get the options
while getopts ":hVif" option; do
   case $option in
      h) # display Help
         Help
         exit;;
      V) # print version
         Version
         exit;;
      i) # print intro
         Intro
         exit;; 
       f) # print intro
         RUNFOREVER=1
         break;;     
     \?) # Invalid option
         echo "Error: Invalid option"
         exit;;
   esac
done


# First, let's talk about the time of day. 
if ((HOUROFDAY <= 6 || HOUROFDAY >= 18)); then
  # night
  echo "It is day."
else
  echo "It is night."
fi
  
# now let's talk about the tide
if ((HOUROFDAY <= 0 || HOUROFDAY >= 12)); then
  # high tide
  echo "It is high tide."
else
  echo "It is low tide."
fi

# choose a random number from 0 to 99
CHOICE=`date +%2N`


# How Built Is The Raft
if ((RAFTLEVEL == 0)); then
  # terrible raft
  echo "There is a small pile of sticks one could assume was the beginnings of a raft."
elif ((RAFTLEVEL == 1)); then
  echo "There is the beginnings of a raft on the island."
elif ((RAFTLEVEL == 2)); then
  echo "There is half a raft moored to the island."
elif ((RAFTLEVEL == 3)); then
  echo "There is a mostly complete raft moored to the island."
elif ((RAFTLEVEL == 4)); then
  echo "There is a fully complete raft ready to sail on the island."
fi



