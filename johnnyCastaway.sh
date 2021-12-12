#!/bin/bash

# what version
VERSION=.01

# what time is it, uses unix DATE command
HOUROFDAY=`date +%H`

# what day of year is it
DAYOFYEAR=`date +%j`

# do we run  forever
RUNFOREVER=0

# how long to sleep between scenes
SLEEPVALUE=10

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
}

Version()
{
  echo $VERSION
}

Intro()
{
	echo "This is the story of Johnny Castaway"
	echo "Johnny is stranded on a small island."
	echo "There is naught but a single coconut tree."
	echo "Johnny wears white shorts and a "
	echo "boating hat.  His beard is scruffy."
}

SetTheScene()
{
# First, let's talk about the time of day. 
if ((HOUROFDAY <= 6 || HOUROFDAY >= 18)); then
  # night
  echo "The sun shines over the island."
else
  echo "The moon shines over the island"
fi
  
# now let's talk about the tide
if ((HOUROFDAY <= 0 || HOUROFDAY >= 12)); then
  # high tide
  echo "It is high tide. The waves lap at the island."
else
  echo "It is low tide. The ocean is calm."
fi

# choose a random number from 0 to 99
CHOICE=`date +%2N`

# is it (christmas, NYE, STPAT,halloween,regular day)
# is it a holiday?
if ((DAYOFYEAR >= 344 && DAYOFYEAR <= 362)); then
	echo "It is Christmas."
	echo "There is a small christmas tree on the island."
	echo  "It is decorated with red bulbs and a golden star."
elif ((DAYOFYEAR >= 363 || DAYOFYEAR <= 1)); then
	echo "It is New Years."
	echo "There is a banner that reads:"
	echo "\"Happy New Year\" on the sole coconut tree"
	echo "which grows on the island."
elif ((DAYOFYEAR >= 75 && DAYOFYEAR <= 80)); then
	echo "It is St Patrick's day."
	echo "There is a patch of 4-leaf clovers on the island."
elif ((DAYOFYEAR >= 274 && DAYOFYEAR <= 305)); then
	echo "It is halloween."
	echo "There is a pumpkin which has been carved"
	echo "into a jack-o-lantern on the island."
fi

# How Built Is The Raft
if ((CHOICE <= 20)); then
  # terrible raft
  echo "There is a small pile of sticks one could assume was the beginnings of a raft."
elif ((CHOICE <= 41)); then
  echo "There is the beginnings of a raft on the island."
elif ((CHOICE <= 62)); then
  echo "There is half a raft moored to the island."
elif ((CHOICE <= 83)); then
  echo "There is a mostly complete raft moored to the island."
elif ((CHOICE <= 99)); then
  echo "There is a fully complete raft ready to sail on the island."
fi

# is it a scene, or just walking
# johnny should be walking like, 30% of the time
if ((CHOICE >= 70)); then
	echo "Johnny is walking around the island."
fi	
}

RunOnce() 
{
# choose a random number from 0 to 99
CHOICE=`date +%2N`

# now let's go through all the scenes
if ((CHOICE == 0)); then
    echo "Johnny climbs the lone coconut tree on the island."
    echo "He pokes his head out of the fronds that top the tree."
    echo "Johnny jumps off of the tree like a diving board."
    echo "His flight starts out professionally, and Johnny"
    echo "executes a perfect flip before landing in the ocean."
    echo "The crabs and seagull which inhabit the island cheer"
    echo "and raise dive scorecards, which range in score but are low."
elif ((CHOICE == 1)); then
    echo "Johnny climbs the lone coconut tree on the island."
    echo "He pokes his head out of the fronds that top the tree."
    echo "Johnny jumps off of the tree like a diving board."
    echo "His flight starts out professionally, but quickly"
    echo "his dive turns into a belly-flop with a splash."
    echo "The crabs and seagull which inhabit the island cheer"
    echo "and raise dive scorecards, which range in score but are low."
elif ((CHOICE == 2)); then
    echo "Johnny sits under the shade of the coconut tree to read a book."
    echo "A seagull lands on the coconut tree."
    echo "The seagull then lands on Johnny's head."
    echo "Johnny attempts to hit the seagull with a large club, but misses and hits his head while the seagull flies up with his hat."
    echo "Johnny lifts his hands in a display of "what can I do?""
elif ((CHOICE == 3)); then
    echo "Johnny is bathing in the ocean."
    echo "he washes his hair and uses a scrub brush on his back."
    echo "His clothes are hanging on a stick on the beach."
    echo "A seagull is perched on the coconut tree."
    echo "The seagull swoops down and steals Johnny's clothes for its nest."
elif ((CHOICE == 4)); then
    echo "Johnny sits down to read a book under the coconut tree."
    echo "A seagull lands on the coconut tree."
    echo "The seagull swoops down and steals Johnny's book."
elif ((CHOICE == 5)); then
    echo "Johnny climbs the coconut tree."
    echo "Johnny looks around."
    echo "Johnny dives into the water."
    echo "Johnny walks back into the foreground."
elif ((CHOICE == 6)); then
    echo "Johnny is feeling hot on the island."
    echo "He pulls out a folding fan and fans himself."
    echo "Johnny really gets into it, fanning himself quickly."
    echo "Johnny puts away the fan."
    echo "Johnny dreams of rain, and contemplates how he can make it rain."
    echo "Johnny has an idea, and a lightbulb appears above his head as he smiles and points skyward excitedly."
    echo "Johnny walks out of the foreground behind the coconut tree."
    echo "Johnny's arms flail wildly as he changes clothes."
    echo "Johnny appears wearing a stereotypical pacific islander ceremonial mask and grass skirt."
    echo "Johhny dances softshoe style."
    echo "A small cloud appears."
    echo "Johnny lifts his hand up, but no rain is falling."
    echo "Johnny hops up and down, stomping his feet angrily."
    echo "Lightning strikes Johnny, sendiing the mask flying."
    echo "Johnny is burnt to a crisp, leaving only cartoon ashes and eyes."
elif ((CHOICE == 7)); then
    echo "Johnny is reading under the coconut tree."
    echo "Johnny falls asleep."
    echo "A coconut falls on Johnny's head and rolls into the ocean."
elif ((CHOICE == 8)); then
    echo "Johnny is reading under the tree."
    echo "He scratches his head, not understanding what he is reading."
    echo "He then flips the book, as it was upside down."
elif ((CHOICE == 9)); then
    echo "Johnny is bathing in the ocean."
    echo "he washes his hair and uses a scrub brush on his back."
    echo "He scrubs under his arms, then smells the brush in disgust."
    echo "His clothes are hanging on a stick on the beach."
    echo "He grabs his clothes, hides his nakedness behind them in a bunch and walks behind the tree."
    echo "Johnny then reappears from behind the tree."
elif ((CHOICE == 10)); then
    echo "Johnny is behind his tree changing clothes."
    echo "Johnny appears wearing a stereotypical pacific islander ceremonial mask and grass skirt."
    echo "He begins to dance around, just as a man and a woman on a yaught appear to take photos."
    echo "They start taking photos of Johnny, and he begins to wave."
    echo "Johnny uses the coconut frond from his grass skirt like a flag, waving down the ship."
    echo "This exposes Johnny's private parts."
    echo "Horrified, the man attempts to cover the woman's eyes on the yaught."
    echo "Johnny drops to his knees, begging them to take him away."
    echo "The yaught pulls away, with the man attempting to cover the woman's eyes and the woman waving back at Johnny happily."
elif ((CHOICE == 11)); then
    echo "Johnny walks to the edge of the island."
    echo "He builds a sand castle."
elif ((CHOICE == 12)); then
    echo "Johnny lays down to rest under the coconut tree."
    echo "A 16th century three-mast ship sails into view."
    echo "Tiny lilliputians row ashore as Johnny sleeps."
    echo "They tie Johnny down to the island and sail away."
    echo "When Johnny awakes, he cannot move."
    echo "A seagull lands on Johnny, builds a nest, and lays an egg in the nest."
elif ((CHOICE == 13)); then
    echo "Johnny lays down to rest under the coconut tree."
    echo "Zs appear as Johnny sleeps and rolls around."
elif ((CHOICE == 14)); then
    echo "Johnny walks to the edge of the island."
    echo "He builds a sand castle."
    echo "A 16th century three-mast ship sails into view."
    echo "Tiny lilliputians row ashore as Johnny climbs the coconut tree."
    echo "The lilliputians climb into the castle, and claim it as their own."
    echo "Tiny airplanes fly out of the castle, and begin to attack Johnny atop the tree."
    echo "Johnny swats at the tiny planes atop the coconut tree in a scene akin to King Kong."
elif ((CHOICE == 15)); then
    echo "Johnny builds a fire at the ends of his island."
    echo "Or he attempts to anyway, setting sticks and rubbing them together to attempt to create fire."
    echo "He tries and he tries."
    echo "Smoke appears."
    echo "Johnny blows on the smouldering wood."
    echo "A fire appears!"
    echo "Johnny relaxes by the fire."
    echo "Johnny warms his hands by the fire."
    echo "The fire burns out."
    echo "Johnny walks around his island."
elif ((CHOICE == 16)); then
    echo "Johnny builds a fire at the ends of his island."
    echo "Johnny relaxes by the fire."
    echo "Johnny walks behind a tree and grabs an old boot that he caught fishing."
    echo "Johnny roasts the boot over the fire."
    echo "Johnny eats the roasted boot whole."
    echo "Johnny warms his hands by the fire."
elif ((CHOICE == 17)); then
    echo "Johnny lays down to rest under the coconut tree."
    echo "A 16th century three-mast ship sails into view."
    echo "Tiny lilliputians row ashore as Johnny sleeps."
    echo "They tie Johnny down to the island and sail away."
    echo "When Johnny awakes, he cannot move."
elif ((CHOICE == 18)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a starfish."
elif ((CHOICE == 19)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a boot."
elif ((CHOICE == 20)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a five small green fish."
    echo "Johnny then catches a rather perterbed orange octopus."
    echo "The octopus chokes Johnny, who runs up the tree."
elif ((CHOICE == 21)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a shark."
elif ((CHOICE == 22)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "A shark eats Johnny."
elif ((CHOICE == 23)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a large green fish who spits water in his face."
elif ((CHOICE == 24)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny taps his foot expectantly."
    echo "Johnny catches a crab, which snaps his nose."
elif ((CHOICE == 25)); then
    echo "Johnny grabs his fishing pole and goes fishing at the edge of his island."
    echo "Johnny catches a boot."
elif ((CHOICE == 26)); then
    echo "A cartoon clock with a front on it spins wildly, indicating the passage of time."
    echo "The island is siloutted by a radiant sunset."
    echo "An airplane flies by in shadow."
    echo "A lone figure parachutes out."
    echo "It's Johnny."
    echo "He lands back on his island, jumping up and down happily."
    echo "The words \"The End\" appear over the scene."
elif ((CHOICE == 27)); then
    echo "Johnny walks to the edge of his island."
    echo "A bottle washes ashore."
    echo "Johnny picks it up excitedly."
    echo "A cartoon bubble appears over Johnny's head, showing the island he is on."
    echo "He writes an S.O.S. and shoves it into the bottle."
elif ((CHOICE == 28)); then
    echo "Johnny is writing a message."
    echo "A thought bubble appears over his head, showing Johnny and a clock at 3pm."
elif ((CHOICE == 29)); then
    echo "Johnny walks to the edge of his island."
    echo "A bottle washes ashore."
    echo "Johnny picks it up excitedly."
    echo "Sadly, it's Johnny's own S.O.S."
elif ((CHOICE == 30)); then
    echo "Johnny is writing a message."
    echo "A cartoon bubble appears over Johnny's head, showing the island he is on."
    echo "He writes an S.O.S. and shoves it into the bottle."
elif ((CHOICE == 31)); then
    echo "A cartoon clock with a front on it spins wildly, indicating the passage of time."
    echo "Johnny is in an office environment, typing at a computer wildly."
    echo "A giant stack of papers is in his inbox, and his outbox is empty."
    echo "There is a lone window, behind which a cityscape looms."
    echo "There is a large timeclock on the wall."
    echo "Johnny falls asleep, and dreams of his island and his time with the mermaid."
elif ((CHOICE == 32)); then
    echo "Johnny walks to the edge of his island, then jumps in a fright."
    echo "Johnny runs behind his tree, and changes clothes."
    echo "Johnny appears in a full tuxedo."
    echo "Johnny pulls a dinner table, two chairs, and a nice dinner from behind his tree."
    echo "A mermaid appears and jumps into Johnny's arms."
    echo "Johnny carries the mermaid to her chair."
    echo "The mermaid and Johnny eat a fancy dinner."
    echo "They eat and talk and toast champagne."
    echo "Johnny has an idea, and a lightbulb appears above his head."
    echo "Johnny produces a record player from behind the tree and picks up the mermaid."
    echo "Johnny and the mermaid dance around the island."
    echo "The mermaid jumps into the ocean, and Johnny removes his top hat."
elif ((CHOICE == 33)); then
    echo "Johnny sits down to read a book under his tree."
    echo "A mermaid swims up to the island."
    echo "Johnny throws away his book and runs to the mermaid."
    echo "Johnny seems nervous, and acts shy."
    echo "The mermaid gives Johnny a gift, a necklace."
    echo "Johnny wonders what he can give back to the mermaid."
    echo "He has an idea, and throws the mermaid a life preserver."
    echo "Johnny proposes a date to the mermaid, who gives him the green light."
    echo "The mermaid swims away, and Johnny waves happily"
elif ((CHOICE == 34)); then
    echo "Johnny walks to the edge of his island with his fishing pole."
    echo "Johnny begins fishing."
    echo "A mermaid swims up to the island behind Johnny, but he thinks it is a fish."
elif ((CHOICE == 35)); then
    echo "Johnny brings wood from behind his tree, and begins to fix his raft."
    echo "A mermaid swims up to Johnny, asking him what he is doing."
    echo "Johnny explains he is building a raft to return to civilization."
    echo "The mermaid is heartbroken, Johnny doesn't understand why, and she swims away."
    echo "Johnny calls after the mermaid to no avail."
    echo "Johnny is heartbroken."
elif ((CHOICE == 36)); then
    echo "Johnny walks behind his tree to change clothes."
    echo "He appears wearing his normal clothes, with his bags packed."
    echo "Johnny throws his bags onto his raft."
    echo "The mermaid and the shark appear to wish Johnny goodbye."
    echo "The mermaid and the shark may be a couple now."
    echo "The shark shakes Johnny's hand vigorously."
    echo "The mermaid points to the raft and shrugs."
    echo "The shark and the mermaid swim away."
    echo "Johnny paddles away on his raft towards civilization."
elif ((CHOICE == 37)); then
    echo "Johnny is hot. He procures a pocket folding fan."
    echo "Johnny fans himself vigorously, to no avail."
    echo "Johnny melts into a puddle."
elif ((CHOICE == 38)); then
    echo "Johnny walks with his towel to the ocean."
    echo "Johnny puts a toe into the ocean nervously."
    echo "A shark snaps at Johnny, scaring him."
    echo "The shark licks his lips hungrily before swimming away."
    echo "Johnny is left on the beach with his bathing gear strewn about."
elif ((CHOICE == 39)); then
    echo "Johnny stands at the edge of the island and taps his foot."
    echo "Johnny tips his hat, and taps his foot nervously."
elif ((CHOICE == 40)); then
    echo "Johnny pulls up his pants, adjusting them."
elif ((CHOICE == 41)); then
    echo "Johnny looks out over the ocean."
    echo "Johnny adjusts his hat and his pants, and taps his foot nervously."
elif ((CHOICE == 42)); then
    echo "Johnny taps his foot nervously."
elif ((CHOICE == 43)); then
    echo "Johnny lifts his hat and looks around."
elif ((CHOICE == 44)); then
    echo "Johnny taps his foot nervously."
    echo "Johnny lifts his hat and looks around."
elif ((CHOICE == 45)); then
    echo "Johnny taps his foot nervously."
    echo "Johnny lifts his hat and looks back into the distance."
elif ((CHOICE == 46)); then
    echo "Johnny lifts his hat."
elif ((CHOICE == 47)); then
    echo "Johnny taps his foot nervously."
    echo "Johnny lifts his hat and looks at the coconut tree."
elif ((CHOICE == 48)); then
    echo "Johnny looks at his raft."
elif ((CHOICE == 49)); then
    echo "Johnny looks out over the ocean."
elif ((CHOICE == 50)); then
    echo "Johnny lifts his hat and looks around, shading himself under the coconut tree."
elif ((CHOICE == 51)); then
    echo "Johnny pulls a spyglass out of his pants, and looks around the island."
elif ((CHOICE == 52)); then
    echo "Johnny pulls a spyglass out of his pants, and looks around the island."
elif ((CHOICE == 53)); then
    echo "A yellow clock with a green frog on it appears, hands spinning wildly indicating the passage of time."
    echo "A tall redheaded woman is sunbathing in a bathing suit on an island outside the city."
    echo "She applies lotion to her legs, and relaxes on a green beach towel reading a book."
    echo "As she rests and reads her book, a message in a bottle floats towards her."
    echo "A question mark appears above her head, as she looks over to the bottle."
    echo "She gets up, and strides over to the bottle."
    echo "She picks up the bottle, finding Johnny's note."
    echo "She imagines a giant island with a smouldering volcano."
    echo "She also imagines a very muscular, attractive man waiting for her."
    echo "In her dream, she is comically skinny, unlike in real life."
elif ((CHOICE == 54)); then
    echo "A yellow clock with a green frog on it appears, hands spinning wildly indicating the passage of time."
    echo "Johnny's raft floats towards a redheaded woman sunbathing on the beach."
    echo "She picks Johnny up completely, passionately kissing him."
    echo "She then grabs his ear while scolding and berating him."
elif ((CHOICE == 55)); then
    echo "Johnny is looking around the island through a spyglass."
    echo "An airplane flies overhead, with Johnny comically looking the wrong direction the whole time."
    echo "A question mark appears above Johnny's head, and he shrugs, not knowing what happened."
elif ((CHOICE == 56)); then
    echo "A red boat is in the distance, Johnny waves at it excitedly."
    echo "The boat begins to circle, it has seen Johnny."
    echo "Johnny quickly runs like a cartoon to the right of the island."
    echo "The boat appears, and it's many many times larger than the island, completely obscuring the screen."
elif ((CHOICE == 57)); then
    echo "Johnny is shaking the coconut tree on his island."
    echo "A coconut falls down, bouncing off Johnny's head and flying away into the ocean."
    echo "Johnny stares at the screen morosely."
elif ((CHOICE == 58)); then
    echo "Johnny is shaking the coconut tree on his island."
    echo "A coconut falls down, and Johnny chases it around the island."
    echo "Johnny catches the coconut, before cracking it on the tree and eating the insides."
elif ((CHOICE == 59)); then
    echo "Johnny is shaking the coconut tree on his island."
    echo "A coconut falls down, and Johnny quickly picks it up."
    echo "Johnny crackS the coconut on the tree, then eats the coconut."
elif ((CHOICE == 60)); then
    echo "Jonnh walks around the island while a plane flies in the far distance."
    echo "Johnny jumps up and down excitedly waving at the plane, and throws a coconut at the plane in an attempt to get it's attention."
    echo "The plane crashes, the pilot parachuting into the distance."
elif ((CHOICE == 61)); then
    echo "Johnny is walking along his island, when a schooner pulling a waterskiing woman sails up."
    echo "Johnny waves and jumps excitedly towards the four partygoers on the ship."
    echo "The ship pulls up, with multiple women and men drinking and partying."
    echo "Johnny swims up to the boat, and it sails off into the distance."
    echo "A short time later, the boat reappears, and a very intoxicated Johnny hops off the boat."
    echo "Johnny swims back to the island, before passing out under the coconut tree wearing a party hat and holding a martini glass."
elif ((CHOICE == 62)); then
    echo "Johnny grabs some wood and builds up his raft."
elif ((CHOICE == 63)); then
    echo "Johnny walks behind the coconut tree, changing into a grey jogging outfit."
    echo "Johnny stretches against the tree, then jogs around the island."
    echo "Johnny disappears back behind the tree, and changes back into his normal clothing."
fi

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

if (($RUNFOREVER)); then
	Intro
	SetTheScene
	for (( ; ; ))do
		sleep $SLEEPVALUE
		RunOnce
		echo "Time passes..."
	done
else
	SetTheScene
	RunOnce
fi


