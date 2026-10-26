#!/usr/bin/env bash
# Stress-tests the built plugin with Tracktion's pluginval.
# Usage: scripts/pluginval.sh [build-dir] [strictness 1-10]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
strictness="${2:-10}"
version="v1.0.4"
tools="$root/tools"
bin="$tools/pluginval.app/Contents/MacOS/pluginval"

if [[ ! -x "$bin" ]]; then
    mkdir -p "$tools"
    curl -fsSL -o "$tools/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/$version/pluginval_macOS.zip"
    unzip -qo "$tools/pluginval.zip" -d "$tools"
    rm "$tools/pluginval.zip"
fi

config="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$build/CMakeCache.txt")"
artefacts="$build/Fracture_artefacts/${config:-Release}"

# macOS only finds AUs through its component registry, so validate the installed copy
# and make the registrar rescan (a fresh CI machine has never seen this plugin).
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumf Frct Mcon >/dev/null 2>&1 || true

for plugin in "$artefacts/VST3/Fracture.vst3" "$HOME/Library/Audio/Plug-Ins/Components/Fracture.component"; do
    echo "==> pluginval (strictness $strictness): $(basename "$plugin")"
    "$bin" --strictness-level "$strictness" --validate-in-process --skip-gui-tests \
        --output-dir "$build/pluginval" --validate "$plugin"
done
