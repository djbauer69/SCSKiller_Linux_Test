# SCSKiller KDE UI

The experimental Linux UI is a Qt 6 / KDE Kirigami application. It uses Plasma's Qt styling and provides a desktop front end for the existing managed CLI instead of reimplementing capture and warm logic in QML.

## Features

- Capture a native Vulkan app or a Windows game launched through Proton.
- Inspect the JSONL recording and its replay coverage.
- Warm the reconstructible compute and graphics pipelines, optionally requiring complete replay.
- Launch a native app or Proton game with the warmed driver-owned cache injected.
- Stream command output and errors in the window.

## Build

Install Qt 6, Qt Quick/QML development tools, CMake, and the KF6 Kirigami development package for your distribution, then run:

    cmake -S gui/kde -B build/kde-ui -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/kde-ui

The app expects the `scskiller-linux` launcher to be available on PATH, at `$SCSKILLER_HOME/bin/scskiller-linux`, or at the path provided by `SCSKILLER_CLI`.

The UI invokes the existing executable with a program/argument vector through QProcess; it does not pass user-entered paths through a shell. For command-line troubleshooting, every action can also be performed from a terminal using `scskiller-linux`.

This UI and the whole Linux path are experimental. Validate capture coverage and inspect strict-warming output before relying on the cache in a real game.
