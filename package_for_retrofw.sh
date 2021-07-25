#!/bin/bash

#mkdir -p bin
rm ipkg/home/retrofw/apps/johnny/jc_reborn
cp jc_reborn ipkg/home/retrofw/apps/johnny/jc_reborn
#cp -R res ipkg/home/retrofw/apps/johnny/
cd ipkg

rm *.gz
tar -czvf control.tar.gz control
tar -czvf data.tar.gz home
ar rv johnny.ipk control.tar.gz data.tar.gz debian-binary

cd ..
mkdir bin
mv ipkg/johnny.ipk bin/
