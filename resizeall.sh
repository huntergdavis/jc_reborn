#!/bin/bash


iter=0

for p in *.png

do
	iter=$(($iter + 1)) 
	echo Converting $p
	convert $p -resize '1200x850!' -colorspace LinearGray  ./out/$iter.png
	


done



