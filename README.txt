Vocalise-Rider v20 (Daemonj Multimedia) - Audio Unit for Logic Pro (JUCE 7.0.12, Xcode 12.4)

BUILD
1. cd to this folder in Terminal
2. rm -rf build
3. cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
4. cmake --build build --config Release
5. Restart Logic. Insert on an audio track: Audio FX > Audio Units > Daemonj Multimedia > Vocalise-Rider.
Check registration: auval -v aufx Vrdr Djmm

RIDE WORKFLOW
1. Set the plugin to WRITE. Set the track's automation mode to Latch (or Touch) and show Plugin automation.
2. Press play on the vocal. The plugin moves the "Ride (dB)" parameter and Logic records it.
3. Stop. Set the plugin to READ and the track to Read. The recorded ride is now applied.
   Command-select the recorded region on the track's automation to edit or convert it as usual.
4. Bounce in Place / Bounce.
Re-do a ride: set WRITE again and play over it. LIVE = ride in real time, nothing recorded.
Note: the plugin reports 30 ms latency; keep Plug-in Delay Compensation on in Logic.

MAKE THE MAC INSTALLER (.pkg inside a .dmg) TO SHARE
After building: cd ~/Plugins/VocalRider && bash Packaging/make_installer.sh
Creates Vocalise-Rider-1.0.0-Installer.dmg containing:
  - Install Vocalise-Rider.pkg  (a normal Mac installer with a Customize step: Audio Unit and VST3; the Documents are always installed). It clears the quarantine flag and signs the plugin locally after installing.
  - Fix Blocked Installer.command  (for when macOS blocks the unsigned installer)
  - Documents folder  (README.md, INSTALL.txt, HOW-TO-USE.md, "Vocalise-Rider Guide.html", Uninstall script)

WINDOWS (VST3)
Needs: Visual Studio 2022 Community ("Desktop development with C++"), CMake, Git, JUCE 7.0.12 in %USERPROFILE%\JUCE,
and Inno Setup 6 for the installer. In PowerShell, from this folder:
  cmake -B build -G "Visual Studio 17 2022" -A x64
  cmake --build build --config Release
  Packaging\windows\make_installer.bat
Creates Vocalise-Rider-1.0.0-Windows-Setup.exe (VST3 + guides). Use "Visual Studio 16 2019" for VS 2019.

GITHUB BUILDS (no tools needed on your computers)
.github/workflows/build.yml builds Windows (VST3) and Mac (AU + VST3), makes both installers, and on a version tag
(git tag v1.0.0 && git push origin v1.0.0) publishes them as a GitHub Release.
