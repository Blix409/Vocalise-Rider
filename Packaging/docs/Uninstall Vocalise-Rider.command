#!/bin/bash
# Removes Vocalise-Rider (all formats). Asks for your Mac password.
echo ""; echo "  This removes Vocalise-Rider. You may be asked for your Mac password."
sudo rm -rf "/Library/Audio/Plug-Ins/Components/Vocalise-Rider.component" \
            "/Library/Audio/Plug-Ins/VST3/Vocalise-Rider.vst3" \
            "/Library/Audio/Plug-Ins/CLAP/Vocalise-Rider.clap" \
            "/Applications/Vocalise-Rider.app" \
            "$HOME/Library/Audio/Plug-Ins/Components/Vocalise-Rider.component"
killall -9 AudioComponentRegistrar 2>/dev/null
echo "  Removed. Restart your DAW."
read -n 1 -s -r -p "  Press any key to close."; echo ""
