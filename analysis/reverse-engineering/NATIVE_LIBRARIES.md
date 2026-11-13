# Native Libraries Analysis Report

## Library Inventory

### libminecraftpe.so
- **Size**: 7,711,868 bytes
- **SHA256**: `78989534f382a7b46fcecdf7e868fb254f50f80faa9b37a4a4dc66522e664520`
- **Architecture**: ARMv7, EABI5
- **Compilation**: GCC 4.8, Thumb-2
- **Entry point**: 0x0 (dynamic)

#### Key Sections
- **Text**: 0x0029f5a0 - 0x3d5c2 (runtime code)
- **ROData**: 0x0069ccd8 - 0x4ce68 (read-only data/strings)
- **BSS**: 0x0075b778 - 0x75b778 (uninitialized data)
- **Data**: 0x0075b000 - 0x59280 (initialized data)

#### Dependencies (NEEDED)
1. **libfmod.so** - FMOD audio engine
2. **libgnustl_shared.so** - C++ runtime (GCC 4.8)
3. **liblog.so** - Android logging
4. **libandroid.so** - Android native API
5. **libEGL.so** - EGL native interface
6. **libGLESv2.so** - OpenGL ES 2.0
7. **libOpenSLES.so** - OpenSL ES audio
8. **libz.so** - zlib compression
9. **libstdc++.so** - C++ standard library
10. **libm.so** - math library
11. **libc.so** - C standard library
12. **libdl.so** - dynamic linking

### libfmod.so
- **Size**: 1,186,316 bytes
- **SHA256**: `e8ed6009af4b0f3fb136eb3ed28a61606ff18497f97bef3b8528fd07927ccf1d`
- **Architecture**: ARM, EABI5
- **Purpose**: Audio engine integration

### libgnustl_shared.so
- **Size**: 661,132 bytes
- **SHA256**: `39043dc1c83a3f5108b5a341404ea8a71d3b228db9742ed2cbf94799715a311f`
- **Architecture**: ARM, EABI5
- **Purpose**: C++ runtime functions

## JNI Analysis

### JNI_OnLoad Details (0x002df4fd)

#### Initialization Sequence
1. **Setup JavaVM**: `JavaUtil::setVM(_JavaVM*)`
2. **Signal Handler**: SIGTERM handler installed at 0x002cd211
3. **Platform Init**: `AppPlatform_android::init(_JavaVM*)`
4. **HTTP Init**: `initHTTPRequestAndroid(_JavaVM*)`
5. **Store Listener**: `initNativeStoreListener()`
6. **Android Store**: `initAndroidStore()`

#### Mutex/Condition Variables
```
# pthread_mutex_init calls at 0x002df56c
# pthread_mutex_lock at 0x002df540
# pthread_mutex_unlock at 0x002df5e4
# pthread_create at 0x002df530
# pthread_cond_init at 0x002df548
# pthread_cond_wait at 0x6493ad0
```

### JNI Method Exports (28 total)

#### MainActivity JNI Methods
1. **nativeSetHeadphonesConnected** (0x002af28c)
2. **nativeRegisterThis** (0x002df0c4)
3. **nativeUnregisterThis** (0x002df0f0)
4. **nativeOnDestroy** (0x002df10c)
5. **nativeStopThis** (0x002df111)
6. **nativeSuspend** (0x002df115)
7. **nativeBackPressed** (0x002df119)
8. **nativeLoginData** (0x002df131)
9. **nativeKeyHandler** (0x002df135)

#### Input Device Manager
1. **onInputDeviceAddedNative** (0x0031fa95)
2. **onInputDeviceChangedNative** (0x002df16d)
3. **onInputDeviceRemovedNative** (0x0031fb95)
4. **setDoubleTriggersSupportedNative** (0x002df171)

#### Native Store Listener
1. **onPurchaseCanceled** (0x0032e1b5)
2. **onPurchaseFailed** (0x0032dedd)
3. **onPurchaseSuccessful** (0x0032e049)
4. **onQueryProductsSuccess** (0x0032d62d)
5. **onQueryPurchasesSuccess** (0x0032d93d)
6. **onQueryProductsFail** (0x0032dbf5)
7. **onQueryPurchasesFail** (0x0032dcad)
8. **onStoreInitialized** (0x0032de21)

#### Image/Pick Handlers
1. **nativeOnPickImageCanceled** (0x0032dd65)
2. **nativeOnPickImageSuccess** (0x0032e321)

## Key Function Addresses

### Platform/Storage
```
0x002ca99c  _ZN19AppPlatform_android22getExternalStoragePathEv
0x002ca9a4  _ZN19AppPlatform_android22getInternalStoragePathEv
0x002ca9ac  _ZN19AppPlatform_android15getUserdataPathEv
0x002ca9a8  _ZN19AppPlatform_android24getUserdataPathForLevelsEv
```

### JNI Call Wrappers
```
0x002df0c4  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeRegisterThisEv
0x002df0f0  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeUnregisterThisEv
0x002df10c  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeOnDestroyEv
```

### Activity Lifecycle
```
0x002df111  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeStopThisEv
0x002df115  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeSuspendEv
0x002df119  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeBackPressedEv
0x002df131  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeLoginDataEv
```

### Input Handling
```
0x002df135  _ZN24Java_com_mojang_minecraftpe_MainActivity_nativeKeyHandlerEii
0x002df16d  _ZN76Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceChangedNativeEi
0x002df171  _ZN76Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setDoubleTriggersSupportedNativeEb
```

## Library Integration Analysis

### Audio System
- **FMOD**: Used for game audio, integrated via JNI
- **SoundSystemBase**: Provides audio engine abstraction
- **Channel control**: Volume, pitch, 3D attributes support

### Storage Management
- **External storage**: Accessible via JNI calls
- **Internal storage**: Used for game data
- **Userdata paths**: Multiple storage directories for different game types

### Native Activity Integration
- **ANativeActivity**: Android native activity interface
- **AppPlatform_android**: Platform abstraction layer
- **InputQueue**: Android input event handling
- **Window/EGL**: OpenGL rendering integration

## Security & Architecture Notes

### Security
- **No scripting API**: No obvious plugin/modification API in binary
- **Stripped symbols**: All debug symbols removed
- **Read-only code**: .text section is read-only (no self-modifying code)
- **RelRO**: Read-only relocations for security

### Architecture Constraints
- **ARMv7 only**: No x86 support in this build
- **Android 4.1+**: Requires Jelly Bean or higher
- **GLES 2.0**: OpenGL ES 2.0 only (no ES 3.0 features)
- **Single thread**: Main thread for UI, separate for rendering

### Modding Implications
- **No runtime scripting**: Cannot inject arbitrary code without system privileges
- **JNI bridge needed**: Must use JNI for game modifications
- **Cooperative hooks only**: Can only modify JNI calls, not internal memory
- **External storage**: Can write mod assets to external storage

## Recommendations for NEXUS Integration

### Bridge Design
1. **JNI Bridge**: Use JNI to communicate between launcher and game
2. **Hook Points**: Focus on JNI method overrides for game behavior
3. **Asset Injection**: Use native asset loading mechanisms
4. **Runtime Communication**: JSON protocol over Unix domain sockets

### Modding Strategy
1. **Asset modification**: Modify game assets (textures, shaders)
2. **JNI hooks**: Override native methods for game behavior
3. **External storage**: Use game's external storage paths for mods
4. **Memory patching**: Not possible without root/emulator

### Execution Requirements
1. **Android runtime**: Waydroid/Anbox for ARMv7 execution
2. **x86 APK needed**: For native x86_64 Linux execution
3. **Root access**: Not required for cooperative modding
4. **JNI environment**: Must maintain Java VM access

## Unresolved Questions

1. **No scripting API**: Why no plugin system in binary?
2. **Shader compilation**: How are shaders compiled at runtime?
3. **Asset loading**: How does the game load assets from APK?
4. **Multi-threading**: How is game rendering separate from UI?
5. **Memory management**: How is JVM memory managed?
6. **Security**: What anti-tampering measures exist?
7. **Compatibility**: How does it handle different screen sizes?
8. **Input**: How does it handle touch vs. mouse input?
9. **Networking**: How does it handle multiplayer?
10. **Storage**: How is level data stored?

## Conclusion

The native libraries provide a clear view of the game's architecture:
- **Well-structured**: ARMv7, GCC 4.8, modern coding practices
- **Android-focused**: Native activity and JNI integration
- **Graphics-intensive**: Complex OGL rendering pipeline
- **Audio-capable**: Full FMOD audio support
- **Storage-aware**: Multiple storage locations for different data types

For NEXUS to work with this game, it must:
1. Use JNI for game modifications
2. Support Android runtime execution
3. Work within the game's architectural constraints
4. Focus on cooperative hooks rather than invasive modifications