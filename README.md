# DroidDesk

DroidDesk is a Windows desktop utility for Android devices using Android Debug Bridge (ADB). It is a Qt Widgets application and uses the official `adb` executable for device operations.

The Android PNG icon is based on Google's [Material Design Android icon](https://github.com/google/material-design-icons/blob/master/png/action/android/materialicons/24dp/2x/baseline_android_black_24dp.png), licensed under the Apache License 2.0. The bundled copy is tinted DroidDesk green.

## Features

- Detect authorized ADB devices and show model, Android version, serial, display size/density, and battery level.
- Install APKs, list user or all packages, launch apps, force-stop apps, clear app data, and uninstall packages for user 0.
- Transfer files to and from the device, and capture screenshots.
- Set home-screen and lock-screen wallpapers through Android's wallpaper shell service.
- Change or reset screen density.
- Reboot normally, to recovery, or to the bootloader; power off; enable ADB over TCP/IP; and connect to an ADB-over-Wi-Fi endpoint.
- Install an OTA package through ADB sideload when the device is already in recovery sideload mode.
- Stream and filter live Logcat output, capture recent logs, and save logs to a file.
- List installed Magisk modules, enable or disable modules, schedule modules for removal, and install Magisk module ZIPs on devices with Magisk and working `su` access. Reboot the device to apply module changes.
- Browse and search the public [Magisk-Modules-Alt-Repo catalog](https://github.com/Magisk-Modules-Alt-Repo/json), view module metadata, and download catalog source archives.
- Run standard device shell commands and use the separate Root & Magisk tab for commands through `su`.

## Requirements

- Windows 10 or later.
- Android SDK Platform-Tools (`adb.exe`) in a `platform-tools` folder next to the app/project, installed and available on `PATH`, or select `adb.exe` with **ADB settings** in the app. DroidDesk searches the app folder and its parent folders for a bundled `platform-tools\adb.exe`.
- USB debugging enabled on the phone and the computer authorized on the device. Device USB drivers may be required.
- Qt 5 or Qt 6 with CMake to build from source.

Build from a configured CMake build directory with:

```powershell
cmake --build build\kits_Debug --config Debug
```

## Root and device safety

Root-only commands and Magisk module actions invoke `su` only on a device that is already rooted and grants the request. DroidDesk does not root a device, unlock its bootloader, bypass Android security, or provide a rooting exploit. The module catalog is a third-party project; catalog metadata and repository source archives are not security-reviewed or verified Magisk installer packages. A catalog archive download does not install a module. Review the source, compatibility, and author packaging instructions, and obtain a genuine installable ZIP before using the local module installer. Modules are marked for removal and removed by Magisk at reboot; enable/disable and module installation changes also require a reboot. Bootloader, recovery, sideload, app-data clearing, uninstall, and shell operations can change device state; verify the target device, module source, and package before confirming.
