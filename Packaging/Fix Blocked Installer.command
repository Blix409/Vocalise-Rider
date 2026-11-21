#!/bin/bash
# If macOS refuses to open "Install Vocalise-Rider.pkg", double-click this.
# It removes the "downloaded from the internet" flag from the installer only. No security setting is changed.
cd "$(dirname "$0")" || exit 1
xattr -cr "Install Vocalise-Rider.pkg" 2>/dev/null
echo ""; echo "  Done. Now double-click \"Install Vocalise-Rider.pkg\"."
read -n 1 -s -r -p "  Press any key to close."; echo ""
