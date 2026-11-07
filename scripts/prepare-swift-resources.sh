#!/bin/sh
# Xcode 26 SwiftPM copies string catalogs without compiling them. Xcode 27's
# bundles already contain compiled strings; xcstringstool handles both layouts.
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
configuration=${1:-release}
architecture=${2:-$(uname -m)}
bin=$(swift build --package-path "$repo/app" -c "$configuration" --arch "$architecture" --show-bin-path)
for target in BridgeplayApp BridgeplayInstaller; do
    bundle="$bin/BridgeplayApp_$target.bundle"
    [ -d "$bundle" ] || continue
    resources=$bundle
    [ ! -d "$bundle/Contents/Resources" ] || resources="$bundle/Contents/Resources"
    xcrun xcstringstool compile "$repo/app/Sources/$target/Resources/Localization/Localizable.xcstrings" \
        --output-directory "$resources"
    rm -f "$resources/Localizable.xcstrings"
done
