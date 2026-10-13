<<<<<<< HEAD
# DroidDesk

DroidDesk is a Windows desktop utility for Android devices using Android Debug Bridge (ADB). It is a Qt Widgets application and uses the official `adb` executable for device operations.

The Android PNG icon is based on Google's [Material Design Android icon](https://github.com/google/material-design-icons/blob/master/png/action/android/materialicons/24dp/2x/baseline_android_black_24dp.png), licensed under the Apache License 2.0. The bundled copy is tinted DroidDesk green.

## Features

- Choose a light or dark interface theme; the selection is remembered between launches.
- Detect authorized ADB devices and show model, Android version, serial, display size/density, and battery level.
- Automatically refresh ADB connection status and log device connect, disconnect, and authorization-state changes.
- Install APKs, list user or all packages, launch apps, force-stop apps, clear app data, and uninstall packages for user 0.
- Transfer files to and from the device, and capture screenshots.
- Mirror and control Android from a dedicated screen-mirroring tab with audio, resolution, and frame-rate options.
- Use scrcpy OTG mode for USB keyboard, mouse, and optional gamepad control without USB debugging; create a virtual Android display; flex-resize that display; record mirror sessions to MP4 or MKV; keep the device awake; turn its screen off; show touches; and keep the mirror window on top.
- Set home-screen and lock-screen wallpapers through Android's wallpaper shell service.
- Change or reset screen density.
- Reboot normally, to recovery, or to the bootloader; power off; enable ADB over TCP/IP; and connect to an ADB-over-Wi-Fi endpoint.
- Install an OTA package through ADB sideload when the device is already in recovery sideload mode.
- Stream and filter live Logcat output, capture recent logs, and save logs to a file.
- List installed Magisk modules, enable or disable modules, schedule modules for removal, and install Magisk module ZIPs on devices with Magisk and working `su` access. The Root & Magisk tab shows live copy and installer output; reboot the device to apply module changes.
- Check whether the selected device grants root access and whether Magisk is detected.
- Check SELinux mode and, with root access, request Enforcing or Permissive mode from the dedicated Root & Magisk section.
- Browse and search the public [Magisk-Modules-Alt-Repo catalog](https://github.com/Magisk-Modules-Alt-Repo/json), and download source archives as ZIP or TAR.GZ without displaying metadata.
- Run standard device shell commands and use the separate Root & Magisk tab for commands through `su`.

## Requirements

- Windows 10 or later.
- Android SDK Platform-Tools (`adb.exe`) in a `platform-tools` folder next to the app/project, installed and available on `PATH`, or select `adb.exe` with **ADB settings** in the app. DroidDesk searches the app folder and its parent folders for a bundled `platform-tools\adb.exe`.
- scrcpy is bundled under `scrcpy\` from the official [scrcpy v5.0.1 Windows x64 release](https://github.com/Genymobile/scrcpy/releases/tag/v5.0.1), distributed under the Apache License 2.0. The release's `LICENSE.txt` is included in that folder. Source code: https://github.com/Genymobile/scrcpy/tree/v5.0.1.
- For ADB features and ordinary screen mirroring, USB debugging must be enabled and the computer authorized on the device. OTG mode requires a direct USB connection and does not require USB debugging. Device USB drivers may be required.
- Qt 5 or Qt 6 with CMake to build from source.

Build from a configured CMake build directory with:

```powershell
cmake --build build\kits_Debug --config Debug
```

## Root and device safety

Root-only commands and Magisk module actions invoke `su` only on a device that is already rooted and grants the request. The root checker sends a harmless `id -u` probe through `su`; approve or deny the resulting superuser request on the device. It reports root access separately from Magisk detection and does not root a device, unlock its bootloader, bypass Android security, or provide a rooting exploit. The SELinux section reads the current mode through `su` and requires confirmation before requesting a mode change. Permissive mode weakens Android's security enforcement; use it only when necessary, and verify the device's reported mode because Android policy may reject or override the request. The module catalog is a third-party project; its repository source archives are not security-reviewed or verified Magisk installer packages. A catalog archive download does not install a module. Review the source, compatibility, and author packaging instructions, and obtain a genuine installable ZIP before using the local module installer. Modules are marked for removal and removed by Magisk at reboot; enable/disable and module installation changes also require a reboot. Bootloader, recovery, sideload, app-data clearing, uninstall, and shell operations can change device state; verify the target device, module source, and package before confirming.

## Third-party software

DroidDesk redistributes the official scrcpy v5.0.1 Windows x64 release without modification in `scrcpy\`. The upstream release SHA-256 is `b12a2c4ee8be317422451fc7dcf8ee20a71b5ea7ef9ad73ddd825a25316227a5`. scrcpy is licensed under the Apache License 2.0; see `scrcpy\LICENSE.txt` and the upstream source at https://github.com/Genymobile/scrcpy/tree/v5.0.1. The **Screen mirror** tab starts the scrcpy executable as a separate process and opens its own device-control window. OTG control is USB-only and has no video or audio; virtual display support depends on the Android version and device implementation. Recordings are saved to a user-selected local MP4 or MKV file.
=======
idk but this project is entirely vibe-coded not single handcoded, bcuz im bored
>>>>>>> 5028972ff00ad02d9e943348881eadce7e7fc2ca
