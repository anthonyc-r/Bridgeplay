#!/bin/sh
# Normalize SwiftPM's flat and macOS bundle layouts for the shipped application.
set -eu
source=$1
destination=$2
if [ -d "$source/Contents/Resources" ]; then
    ditto "$source" "$destination"
else
    mkdir -p "$destination/Contents/Resources"
    ditto "$source" "$destination/Contents/Resources"
    if [ -f "$destination/Contents/Resources/Info.plist" ]; then
        mv "$destination/Contents/Resources/Info.plist" "$destination/Contents/Info.plist"
    fi
fi
