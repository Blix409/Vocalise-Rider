#!/usr/bin/env python3
"""Lets JUCE 7.0.12 build with the macOS 15 SDK (Xcode 16).
JUCE 7 calls CGWindowListCreateImage, which Apple removed from the macOS 15 SDK. It is only used to take a
snapshot of a native window (not used by Vocalise-Rider), so the call is replaced by a function that returns
an empty image. Usage: python3 patch_juce_macos15.py <path to JUCE folder>"""
import re, sys, pathlib

f = pathlib.Path(sys.argv[1]) / "modules/juce_gui_basics/native/juce_Windowing_mac.mm"
s = f.read_text()
if "patched: API removed" in s or "CGImageRef screenShot = CGWindowListCreateImage" not in s:
    print("nothing to patch"); sys.exit(0)

call = re.compile(r"CGImageRef screenShot = CGWindowListCreateImage \(.*?kCGWindowImageBoundsIgnoreFraming\);", re.S)
s, n = call.subn("CGImageRef screenShot = nullptr; (void) nsWindow;   // patched: API removed in the macOS 15 SDK", s)
guard = "NSBitmapImageRep* bitmapRep = [[NSBitmapImageRep alloc] initWithCGImage: screenShot];"
assert n == 1 and s.count(guard) == 1, "unexpected JUCE source layout"
s = s.replace(guard, "if (screenShot == nullptr) return {};\n\n        " + guard)
f.write_text(s)
print("patched", f)
