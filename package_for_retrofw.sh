#!/bin/bash

#mkdir -p bin
cp jc_reborn ipkg/home/retrofw/apps/johnny/jc_reborn
#cp -R res ipkg/home/retrofw/apps/johnny/
cd ipkg

tar -czvf control.tar.gz control
tar -czvf data.tar.gz home
ar rv johnny.ipk control.tar.gz data.tar.gz debian-binary

cd ..

mv ipkg/johnny.ipk bin/
