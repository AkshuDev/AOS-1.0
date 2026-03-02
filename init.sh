#!/bin/bash

echo "Cloning Submodules..."

git submodule update --init --recursive
echo "Setting Library Paths..."
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/PBFS/PBFS/PEFI/build
echo "Done!"