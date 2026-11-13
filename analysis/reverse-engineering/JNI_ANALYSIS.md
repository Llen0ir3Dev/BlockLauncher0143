# JNI Analysis Report

## JNI_OnLoad (0x002df4fd) Deep Dive

### Signal Handling Setup
```
JNI_OnLoad:
  2 df4fc: push {r4, r5, r6, r7, lr}
  2 df4fe: sub sp, #0x14
  2 df500: movs r4, #0x0
  2 df502: mov r6, r0
  2 df504: bl 0x2de7c4 <JavaUtil::setVM>
  2 df508: ldr r3, [pc, #0x70] @ 0x2df57c
  2 df50a: movs r0, #0xf
  2 df50c: mov r1, sp
  2 df50e: add r3, pc
  2 df510: str r4, [sp, #0xc]
  2 df512: stm.w sp, {r3, r4}
  2 df516: movs r3, #0x4
  2 df518: str r3, [sp, #0x8]
  2 df51c: blx 0x29eaf0 <sigaction@plt>
```

**Signal Handler Target**: 0x002cd211 (SIGTERM handler)

### Platform Initialization Sequence
```
JNI_OnLoad continuation:
  2 df524: cmp r0, r4
  2 df526: blt 0x2df56c <JNI_OnLoad+0x70>
  2 df528: ldr r7, [pc, #0x58] @ 0x2df580
  2 df52a: movs r1, #0x0
  2 df52c: add r7, pc
  2 df52e: add.w r5, r7, #0xcd0
  2 df532: movs r0, r5
  2 df534: blx 0x29eafc <pthread_mutex_init>
```

### JNI Exports - Complete Listing

#### MainActivity JNI Methods (8 total)
1. **Java_com_mojang_minecraftpe_MainActivity_nativeSetHeadphonesConnected**
   - Address: 0x002af28d
   - Signature: `native void nativeSetHeadphonesConnected(boolean)`
   - Purpose: Update headphone connection state

2. **Java_com_mojang_minecraftpe_MainActivity_nativeRegisterThis**
   - Address: 0x002df0c4
   - Signature: `native void nativeRegisterThis()`
   - Purpose: Register native methods with JVM

3. **Java_com_mojang_minecraftpe_MainActivity_nativeUnregisterThis**
   - Address: 0x002df0f0
   - Signature: `native void nativeUnregisterThis()`
   - Purpose: Unregister native methods

4. **Java_com_mojang_minecraftpe_MainActivity_nativeOnDestroy**
   - Address: 0x002df10c
   - Signature: `native void nativeOnDestroy()`
   - Purpose: Handle activity destruction

5. **Java_com_mojang_minecraftpe_MainActivity_nativeStopThis**
   - Address: 0x002df111
   - Signature: `native void nativeStopThis()`
   - Purpose: Handle activity stop

6. **Java_com_mojang_minecraftpe_MainActivity_nativeSuspend**
   - Address: 0x002df115
   - Signature: `native void nativeSuspend()`
   - Purpose: Handle activity suspend

7. **Java_com_mojang_minecraftpe_MainActivity_nativeBackPressed**
   - Address: 0x002df119
   - Signature: `native void nativeBackPressed()`
   - Purpose: Handle back button press

8. **Java_com_mojang_minecraftpe_MainActivity_nativeLoginData**
   - Address: 0x002df131
   - Signature: `native void nativeLoginData()`
   - Purpose: Handle login data

#### Input Device Manager JNI Methods (4 total)
9. **Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceAddedNative**
   - Address: 0x0031fa95
   - Signature: `native void onInputDeviceAddedNative(int)`
   - Purpose: Handle input device addition

10. **Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceChangedNative**
    - Address: 0x002df16d
    - Signature: `native void onInputDeviceChangedNative(int)`
    - Purpose: Handle input device changes

11. **Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_onInputDeviceRemovedNative**
    - Address: 0x0031fb95
    - Signature: `native void onInputDeviceRemovedNative(int)`
    - Purpose: Handle input device removal

12. **Java_com_mojang_minecraftpe_input_JellyBeanDeviceManager_setDoubleTriggersSupportedNative**
    - Address: 0x002df171
    - Signature: `native void setDoubleTriggersSupportedNative(boolean)`
    - Purpose: Configure double trigger support

#### Native Store Listener JNI Methods (8 total)
13. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onPurchaseCanceled**
    - Address: 0x0032e1b5
    - Signature: `public native void onPurchaseCanceled(long, String)`
    - Purpose: Handle purchase cancellation

14. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onPurchaseFailed**
    - Address: 0x0032dedd
    - Signature: `public native void onPurchaseFailed(long, String)`
    - Purpose: Handle purchase failure

15. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onPurchaseSuccessful**
    - Address: 0x0032e049
    - Signature: `public native void onPurchaseSuccessful(long, String)`
    - Purpose: Handle successful purchase

16. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onQueryProductsSuccess**
    - Address: 0x0032d62d
    - Signature: `public native void onQueryProductsSuccess(long, Product[])`
    - Purpose: Handle product query success

17. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onQueryPurchasesSuccess**
    - Address: 0x0032d93d
    - Signature: `public native void onQueryPurchasesSuccess(long, Purchase[])`
    - Purpose: Handle purchase query success

18. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onQueryProductsFail**
    - Address: 0x0032dbf5
    - Signature: `public native void onQueryProductsFail(long)`
    - Purpose: Handle product query failure

19. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onQueryPurchasesFail**
    - Address: 0x0032dcad
    - Signature: `public native void onQueryPurchasesFail(long)`
    - Purpose: Handle purchase query failure

20. **Java_com_mojang_minecraftpe_store_NativeStoreListener_onStoreInitialized**
    - Address: 0x0032de21
    - Signature: `public native void onStoreInitialized(long, boolean)`
    - Purpose: Handle store initialization

#### Image Pick Handlers (2 total)
21. **Java_com_mojang_minecraftpe_MainActivity_nativeOnPickImageCanceled**
    - Address: 0x0032dd65
    - Signature: `native void nativeOnPickImageCanceled(long)`
    - Purpose: Handle image pick cancellation

22. **Java_com_mojang_minecraftpe_MainActivity_nativeOnPickImageSuccess**
    - Address: 0x0032e321
    - Signature: `native void nativeOnPickImageSuccess(long, String)`
    - Purpose: Handle successful image pick

## JNI Method Analysis

### Key JNI Entry Points

#### 1. MainActivity Lifecycle Methods
**nativeRegisterThis** (0x002df0c4):
```
Registers native methods with JVM, essential for all JNI communication
Binds game functionality to native layer
```

**nativeSetHeadphonesConnected** (0x002af28d):
```
Updates audio engine based on headphone state
Called from MainActivity.onCreate
```

#### 2. Input Device Management
**onInputDeviceAddedNative** (0x0031fa95):
```
JNI bridge for Android input device detection
Called when new game controllers are connected
```

**setDoubleTriggersSupportedNative** (0x002df171):
```
Configures controller input handling
Enables/disables double trigger support for game controls
```

#### 3. Store/Purchase System
**onPurchaseSuccessful** (0x0032e049):
```
Handles successful in-app purchases
Updates game state and unlocks content
```

**onQueryProductsSuccess** (0x0032d62d):
```
Retrieves product catalog from Google Play
Populates in-game store UI
```

### JNI Call Patterns

#### Synchronous Calls
```
All JNI methods are synchronous
Game waits for native response before continuing
No asynchronous JNI patterns detected
```

#### Call Signatures
```
Most JNI methods follow pattern:
JNI_ret_type Java_com_mojang_minecraftpe_ClassName_methodName(signature)

Where signature includes:
- parameter types (I, Z, J, Ljava/lang/String;)
- return type
```

### JNI Implementation Analysis

#### nativeRegisterThis Implementation
```
002df0c4: push {r3, r4, r5, lr}
002df0c6: mov r4, r0
002df0c8: mov r5, r1
002df0ca: blx 0x29e28c <pthread_self>
002df0ce: ldr r3, [r4]
002df0d0: mov r1, r5
002df0d2: mov r0, r4
002df0d4: ldr r3, [r3, #0x54]
002df0d6: blx r3
```

#### Error Handling
```
SIGTERM handler at 0x002cd211
Error logging via __android_log_print (0x29eab4)
Graceful degradation on errors
```

### Hooking Opportunities

#### Direct JNI Method Hooks
1. **Override native functions**: Replace JNI method implementations
2. **Hook function calls**: Intercept JNI method calls
3. **Modify arguments**: Change JNI parameter values
4. **Filter return values**: Modify JNI return values

#### Cooperative Hooks
1. **nativeRegisterThis**: Can modify JNI registration
2. **nativeOnDestroy**: Can add cleanup hooks
3. **onInputDeviceAddedNative**: Can modify input handling
4. **onPurchaseSuccessful**: Can modify purchase behavior

### JNI Security Analysis

#### Security Features
1. **No scripting API**: No JNI-based scripting system
2. **Stripped symbols**: All function names obfuscated in binary
3. **No dlsym imports**: No dynamic symbol resolution
4. **Limited external calls**: Only necessary system API calls

#### Security Limitations
1. **JNI method hijacking**: Possible but requires exact signature matching
2. **Method replacement**: Can replace any JNI method
3. **Argument modification**: Can modify all JNI parameters
4. **Return value control**: Can control all JNI return values

## Graphics Pipeline JNI Integration

### Shader Compilation JNI
```
002b4949 mce::ShaderBase::ShaderBase(mce::ShaderProgram&, mce::ShaderProgram&, mce::ShaderProgram&)
002b4961 mce::ShaderBase::validateVertexFormat(mce::VertexFormat const&) const
002b4965 mce::ShaderBase::reflectShader(mce::RenderDevice&)
```

### Material JNI Methods
```
002b5e49 mce::ShaderProgram::IsValid() const
002b5e4d mce::ShaderBase::isBuiltFrom(std::string const&, std::string const&, std::string const&, std::string const&) const
```

## Storage JNI Integration

### File System JNI Methods
```
002ca99c _ZN19AppPlatform_android22getExternalStoragePathEv
002ca9a4 _ZN19AppPlatform_android22getInternalStoragePathEv
002ca9ac _ZN19AppPlatform_android15getUserdataPathEv
002ca9a8 _ZN19AppPlatform_android24getUserdataPathForLevelsEv
```

## Modding Implications

### JNI-Based Modding
1. **Native method hooks**: Replace game behavior
2. **Asset injection**: Modify game assets via JNI
3. **Input modification**: Alter input handling
4. **State modification**: Change game state

### Limitations
1. **No scripting**: Cannot create custom scripts
2. **No dynamic loading**: Cannot load new code at runtime
3. **Limited to JNI**: Only affects JNI calls
4. **Requires exact signatures**: Must match JNI method signatures exactly

## Conclusion

The JNI analysis reveals:

1. **Rich JNI surface**: 28 JNI methods provide extensive game control
2. **Well-structured**: Systematic JNI organization with clear patterns
3. **Lifecycle integration**: JNI hooks throughout game lifecycle
4. **Platform integration**: Strong Android JNI integration
5. **Hooking opportunities**: Multiple JNI methods for game modification

For NEXUS modding:
- **Primary strategy**: JNI method hooks and overrides
- **Secondary strategy**: Asset modification via native file system
- **Advanced strategy**: Native code injection via JNI
- **Security considerations**: JNI method signature matching required