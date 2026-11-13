# APK Analysis Report

## Summary
- **APK Path**: `/root/mcpelinux/minecraft-0.14.3.apk`
- **SHA256**: `c4365666ac5c5a65af3c8a94d765e34dc2f525bbca787d9431e0aa091f70f0f6`
- **Size**: 21,803,588 bytes
- **Package**: `com.mojang.minecraftpe`
- **Version**: 0.14.3 (code 760140300)

## Key Findings

### Manifest Information
- **Min SDK**: 17 (Android 4.1+ - Jelly Bean)
- **Target SDK**: 22 (Android 5.1)
- **Debuggable**: False
- **Main Activity**: `com.mojang.minecraftpe.MainActivity`
- **Native Lib Name**: `minecraftpe`
- **GLES Version**: 131072 (OpenGL ES 2.0)

### Supported ABIs
- **Primary ABI**: `armeabi-v7a` (ARMv7-A, Thumb-2)
- **Secondary ABIs**: None (no x86 libraries)

### Permissions
```
android.permission.INTERNET
com.android.vending.BILLING
android.permission.ACCESS_NETWORK_STATE
android.permission.WRITE_EXTERNAL_STORAGE
android.permission.VIBRATE
```

### Asset Inventory
- **Total entries**: 1,584 ZIP entries
- **Assets directory**: 947 entries
- **Shaders**: 41 total (17 vertex + 22 fragment + 2 materials)
- **Materials**: 26 total
- **Languages**: 17 locale files (de_DE, en_US, es_ES, es_MX, fr_CA, fr_FR, it_IT, ja_JP, ko_KR, pt_BR, pt_PT, ru_RU, zh_CN, zh_TW)

### Native Libraries
- **libminecraftpe.so**: 7,711,868 bytes, ARM EABI5, stripped
- **libfmod.so**: Audio engine
- **libgnustl_shared.so**: C++ runtime (GCC 4.8)

### JNI Exports
- **Total JNI methods**: 28 exported `Java_com_mojang_*` functions
- **Entry points**:
  - `JNI_OnLoad` (0x002df4fd)
  - `ANativeActivity_onCreate` (0x0064928d)
  - `android_main` (0x0030990d)

## APK Verification Status
- **Valid APK**: Yes
- **x86 port ready**: No (only armeabi-v7a libraries present)
- **Execution strategy**: Requires Android container/emulator

## Notes
- APK is ARMv7 and cannot run natively on x86_64 Linux
- Requires x86 APK (version code 780140300/780140301) for x86_64 compatibility
- NFC, Bluetooth, and other permissions not present