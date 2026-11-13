# Binary Architecture Report

## libminecraftpe.so Analysis

### ELF Properties
- **Format**: ELF32, ARM, EABI5, stripped
- **Machine**: ARM (EM_ARM)
- **Endianness**: Little-endian
- **Entry point**: 0x0 (dynamically linked)
- **Interpreter**: `/system/bin/linker`
- **Linker**: GNU gold 1.11

### Section Layout
```
.text      0x0029f5a0  size: 0x3d9dc2
.rodata     0x0069ccd8  size: 0x04ce68
.bss       0x0075b778  size: 0x074151
.data.rel.ro 0x006eb160  size: 0x06c150
.data       0x0075b000  size: 0x000778
.bss       0x0075b778  size: 0x074151
```

### Dynamic Symbols
- **Total symbols**: 30,996
- **Defined functions**: 27,543
- **Undefined imports**: 511
- **Objects**: 2,936
- **Vtables**: 1,398
- **Typeinfo symbols**: 0

### Key Subsystem Functions

#### Graphics/OGL Subsystem (1,583 functions)
**Core Renderer Components:**
- `mce::ShaderProgramBase::ShaderProgramBase()` - 0x002b4a15
- `mce::ShaderProgramOGL::ShaderProgramOGL()` - 0x002b9619
- `mce::RenderContext::RenderContext()` - 0x002b8375
- `mce::RenderContextOGL::RenderContextOGL()` - 0x002b82bd
- `mce::RenderDevice::RenderDevice()` - 0x002b8525

**Shader Management:**
- `mce::ShaderBase::reflectShader(mce::RenderDevice&)` - 0x002b4965
- `mce::ShaderBase::validateVertexFormat(mce::VertexFormat const&) const` - 0x002b4961

**Texture System:**
- `mce::TextureOGL::TextureOGL()` - 0x002b981d
- `mce::TextureOGL::bindTexture(mce::RenderContext&, unsigned int, unsigned int) const` - 0x002ba1f9
- `mce::TextureOGL::createMipStorage()` - 0x002b9ccd

**Framebuffer Objects:**
- `mce::FrameBufferObjectOGL::FrameBufferObjectOGL()` - 0x002b8525
- `mce::FrameBufferAttachmentOGL::FrameBufferAttachmentOGL()` - 0x002b984d

#### World/Entity Subsystem (3,297 functions)
**Level System:**
- `Level::getLevel()` - 0x004c09c5
- `Level::getLevelData()` - 0x002d5f9
- `LevelChunk::getDimensionId()` - 0x005ecc85
- `LevelChunk::recalcHeightmap()` - 0x005ece55

**Biome/Chunk Generation:**
- `BiomeSource::getBiome(BlockPos const&)` - 0x00581d85
- `ChunkViewSource::ChunkViewSource(ChunkSource&, ChunkSource::LoadMode)` - 0x005eaadd
- `Biome::getBiome(int)` - 0x00597591

**Entity/Physics:**
- `VerletIntegrator::VerletIntegrator()` - 0x002afa51
- `Entity::getAABB()` - Multiple overloads
- `Player::setChunkRadius(int)` - 0x006f5f45

#### Audio/Video Subsystem
- `FMOD::System::create()` - PLT entry at 0x29e28c
- `SoundSystemBase::preinit()` - 0x002cd31d
- `SoundSystemBase::setListenerDirection()` - 0x002cd31d

#### Platform/Android Subsystem
- `AppPlatform_android::initWithActivity(ANativeActivity*)` - 0x002caa25
- `AppPlatform_android::init(_JavaVM*)` - 0x002dc88d
- `JNI_OnLoad()` - 0x002df4fd (with SIGTERM handler at 0x002cd211)

### VTable Analysis
- **Total vtables**: 1,398
- **Graphics vtables**: 287 (mce::ShaderConstantWithDataOGL, mce::TexturePtr, etc.)
- **Object vtables**: 987
- **System vtables**: 124

### PLT Entries (Import Table)
- **libfmod.so**: Audio system functions
- **libgnustl_shared.so**: C++ standard library
- **liblog.so, libandroid.so, libEGL.so, libGLESv2.so, libOpenSLES.so, libz.so**
- **libc.so, libm.so, libdl.so**: Standard C library

### Code Characteristics
- **Instruction Set**: ARM Thumb-2 (mixed ARM/Thumb)
- **Floating Point**: VFPv3 (NEON support)
- **Compiler**: GCC 4.8
- **Position Independent**: Yes (PIE)
- **RelRO**: Present
- **TEXTREL**: Free (DYLD_NONE)
- **Init Array**: 22 constructors

### Key Functions

#### Entry Points
1. **JNI_OnLoad** (0x002df4fd): JNI environment initialization
2. **ANativeActivity_onCreate** (0x0064928d): Android activity creation
3. **android_main** (0x0030990d): Native application entry point

#### Platform Initialization
1. **AppPlatform_android::initWithActivity** (0x002caa25): Set up Android platform with activity
2. **AppPlatform_android::init** (0x002dc88d): Initialize platform with JVM
3. **JavaUtil::setVM** (0x002de7c4): Set Java VM reference

#### Asset/Storage
1. **AppPlatform_android::getExternalStoragePath** (0x002ca99c): Get external storage path
2. **AppPlatform_android::getInternalStoragePath** (0x002ca9a4): Get internal storage path
3. **AppPlatform_android::readAssetFile** (0x002de6b4): Read asset files

#### JNI Call Handlers
1. **MainActivity::nativeRegisterThis** (0x002df0c5): Register native methods
2. **MainActivity::nativeSetHeadphonesConnected** (0x002af28d): Handle headphone state
3. **JellyBeanDeviceManager::onInputDeviceAddedNative** (0x0031fa95): Handle input device changes

### Architecture Summary
The binary is a well-optimized ARMv7 application using GCC 4.8 with:
- **Modern ARM features**: Thumb-2, VFPv3, NEON
- **Robust error handling**: Multiple error check points and validation
- **Cross-platform**: Android-specific native activity support
- **Complex rendering pipeline**: 26 material files, 41 shader files
- **Multi-threaded**: pthread mutex/condition variable usage throughout

### Hypotheses & Unknowns
1. **Shader compilation**: Runtime shader compilation mechanism details
2. **Modding hooks**: No obvious scripting/plugin API in binary
3. **Performance**: Exact optimization techniques used
4. **Security**: Anti-tampering measures
5. **Compatibility**: Full Android API compatibility details

### Recommendations
1. **Focus on cooperative hooks**: Since no scripting API exists, use NEXUS cooperative hooks
2. **Android runtime**: Need Android container (Waydroid/Anbox) for execution
3. **Bridge design**: Implement JNI bridge for communication between launcher and game
4. **Shader support**: Translation needed for GLSL ES 1.0 -> GLSL ES 3.0