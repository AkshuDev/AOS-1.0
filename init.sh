#!/bin/bash

#make init

echo "Cloning Submodules..."


git submodule update --init --recursive
cd PBFS || exit

if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "Local changes detected in PBFS. Stashing..."
    git stash push -u -m "auto-stash before submodule update"
    STASHED=1
else
    echo "No local changes in PBFS."
    STASHED=0
fi

cd ..

git submodule update --remote

cd PBFS || exit

if [ "$STASHED" -eq 1 ]; then
    echo "Dropping auto-created stash..."
    git stash pop >/dev/null 2>&1
fi

cd ..