#!/bin/sh
# Xcode 27 compiles the layered Icon Composer artwork; older Xcode uses the same mark.
set -eu
generator=$1
mark=$2
document=$3
output=$4
name=$5
rm -rf "$document" "$output"
mkdir -p "$output"
major=$(xcodebuild -version | sed -n 's/^Xcode \([0-9]*\).*/\1/p')
if [ "${major:-0}" -ge 27 ]; then
    "$generator" "$mark" "$document"
    catalog=$document
else
    catalog="$output/$name.xcassets"
    icons="$catalog/$name.appiconset"
    mkdir -p "$icons"
    "$generator" "$mark" "$output/icon.png"
    python3 - "$icons" "$output/icon.png" <<'PYTHON'
import json
import pathlib
import subprocess
import sys
icons, source = pathlib.Path(sys.argv[1]), sys.argv[2]
images = []
for points in (16, 32, 128, 256, 512):
    for scale in (1, 2):
        filename = f"icon_{points}x{points}@{scale}x.png"
        pixels = str(points * scale)
        subprocess.run(["sips", "-z", pixels, pixels, source, "--out", str(icons / filename)],
                       check=True, stdout=subprocess.DEVNULL)
        images.append({"filename": filename, "idiom": "mac", "size": f"{points}x{points}", "scale": f"{scale}x"})
(icons / "Contents.json").write_text(json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}))
PYTHON
fi
xcrun actool "$catalog" --compile "$output" --platform macosx \
    --minimum-deployment-target 15.0 --app-icon "$name" \
    --output-partial-info-plist "$output/partial.plist" >/dev/null
