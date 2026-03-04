#!/bin/bash

#make init

echo "Cloning Submodules..."
git submodule update --init --recursive
git submodule update --remote
echo "Setting Library Paths..."
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/PBFS/PBFS/PEFI/build
echo "Done!"