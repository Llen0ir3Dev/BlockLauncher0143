# Graphics Pipeline Analysis

## Shader System Overview

### Shader Architecture
The game uses a multi-stage shader system with:
- **39 shader files** in `assets/shaders/`
- **26 material files** in `assets/materials/`
- **230 material passes** total
- **8 uniform groups** with specific constant buffers

### Shader Types
```
Vertex Shaders (.vertex):
  - assets/shaders/cloud.vertex
  - assets/shaders/color.vertex
  - assets/shaders/entity.vertex
  - assets/shaders/renderchunk.vertex
  - ... (15 total)

Fragment Shaders (.fragment):
  - assets/shaders/color.fragment
  - assets/shaders/entity.fragment
  - assets/shaders/renderchunk.fragment
  - ... (22 total)

Shader Headers (.h):
  - assets/shaders/util.h (include resolver)
```

## Shader Compilation Process

### Multi-Version Shader Loading
```c++
// In assets/shaders/renderchunk.vertex:
// __multiversion__ marker triggers version selection

#if __VERSION__ >= 300
#define attribute in
#define varying out
// MSAA support, centroid sampling
_centroid out vec2 uv0;
_centroid out vec2 uv1;
#else
// GLSL ES 1.0 compatibility
varying vec2 uv0;
varying vec2 uv1;
#endif
```

### Shader Program Creation
```c++
mce::ShaderProgramOGL::ShaderProgramOGL(
    mce::ShaderType,           // VERTEX or FRAGMENT
    std::string const& vertexSource,
    std::string const&, 
    std::string const&
)
```

## JNI Graphics Integration

### Key JNI Methods
```
002b4949 mce::ShaderBase::ShaderBase(mce::ShaderProgram&, mce::ShaderProgram&, mce::ShaderProgram&)
002b4961 mce::ShaderBase::validateVertexFormat(mce::VertexFormat const&) const
002b4965 mce::ShaderBase::reflectShader(mce::RenderDevice&)
002b5e49 mce::ShaderProgram::IsValid() const
002b5e4d mce::ShaderBase::isBuiltFrom(std::string const&, std::string const&, std::string const&, std::string const&) const
```

### Shader Compilation JNI Bridge
```
// JNI entry points for shader system:
// - mce::ShaderProgramOGL::ShaderProgramOGL()
// - mce::ShaderBase::reflectShader()
// - mce::ShaderBase::validateVertexFormat()
```

## Uniform System

### 8 Uniform Groups

#### 1. EntityConstants
```glsl
// Entity rendering uniforms
layout(std140) uniform EntityConstants {
    vec4 OVERLAY_COLOR;
    vec4 TILE_LIGHT_COLOR;
    vec4 CHANGE_COLOR;
    vec4 GLINT_COLOR;
    vec2 UV_ANIM;
    vec2 UV_OFFSET;
    vec2 UV_ROTATION;
    vec2 GLINT_UV_SCALE;
};
```

#### 2. HoloConstants
```glsl
// Holographic rendering uniforms
layout(std140) uniform HoloConstants {
    vec4 TABLE_REVEAL_CENTER;
    vec4 TABLE_REVEAL_CONSTANTS;
    vec3 GAZE_DIRECTION;
    vec4 SKIRT_UVS;
    vec4 SKIRT_UV_SCALE;
    vec2 CHUNK_CLIP_MIN;
    vec2 CHUNK_CLIP_MAX;
    vec2 CLIP_MIN;
    vec2 CLIP_MAX;
};
```

#### 3. PerFrameConstants
```glsl
// Frame-to-frame rendering uniforms
layout(std140) uniform PerFrameConstants {
    vec3 VIEW_DIRECTION;
    float TIME;
    vec3 VIEW_POS;
    float FAR_CHUNKS_DIST;
    vec4 FOG_COLOR;
    vec2 FOG_CONTROL;
    float RENDER_DISTANCE;
};
```

## Rendering Pipeline

### Draw Call Flow
```
1. LevelChunk::render()
    - Bind shader program via mce::ShaderProgramOGL
    - Set uniforms via mce::ShaderConstants
    - Call mce::RenderContext::draw()

2. mce::RenderContext::draw()
    - Setup vertex buffers via mce::BufferOGL
    - Bind textures via mce::TextureOGL
    - Execute GL draw calls

3. Material System
    - Material::apply() sets shader uniforms
    - Sampler state configuration
    - Pass through mce::RenderMaterial
```

### JNI Rendering Integration
```c++
// JNI hooks for rendering:
// - mce::ShaderProgram::IsValid() - Check shader validity
// - mce::ShaderBase::reflectShader() - Link shaders
// - mce::RenderContext::beginFrame()/endFrame() - Frame management
```

## Texture System

### Texture Pipeline
```c++
mce::TextureOGL::TextureOGL()                    // Constructor
mce::TextureOGL::bindTexture()                    // Bind to GL
mce::TextureOGL::setFilteringMode()               // Configure filtering
mce::TextureOGL::setWrappingMode()                // Configure wrapping
```

### Texture Units
```glsl
// In material shader files:
uniform sampler2D sampler0;    // Atlas texture
uniform sampler2D sampler1;    // Detail texture
uniform sampler2D sampler2;    // Overlay texture
```

## Graphics API Layer

### OpenGL ES Integration
```c++
// JNI GL function wrappers:
// - glCreateShader, glShaderSource
// - glCompileShader, glGetShaderiv
// - glCreateProgram, glLinkProgram
// - glUseProgram, glUniform*
```

### Framebuffer Management
```c++
mce::FrameBufferObjectOGL::FrameBufferObjectOGL()   // Constructor
mce::FrameBufferObjectOGL::createFrameBuffer()      // Create FBO
mce::FrameBufferObjectOGL::bindFrameBuffer()        // Bind for rendering
mce::FrameBufferObjectOGL::resolveFrameBuffer()     // Resolve to texture
```

## Shader JNI Analysis

### Shader Compilation JNI
```
// Key JNI methods for shader system:
// 002b9619 mce::ShaderProgramOGL::ShaderProgramOGL()
// 002b9675 mce::ShaderProgramOGL::deleteShader()
// 002b9685 mce::ShaderProgramOGL::getUniformNames()
```

### Material JNI Integration
```
// JNI material management:
// 002b981d mce::TextureOGL::TextureOGL()
// 002b984d mce::TextureOGL::TextureOGL(unsigned int)
// 002ba1f9 mce::TextureOGL::bindTexture()
```

## Modding Graphics Pipeline

### Shader Modification
```
// Strategies for shader modding:
// 1. Replace shader source files
// 2. Override JNI shader compilation
// 3. Modify uniform values at runtime
// 4. Inject custom shader code
```

### Material Modification
```
// Strategies for material modding:
// 1. Replace material definition files
// 2. Override material application JNI
// 3. Modify sampler state
// 4. Change shader passes
```

### Texture Modification
```
// Strategies for texture modding:
// 1. Replace texture assets
// 2. Override texture loading JNI
// 3. Modify texture filtering/wrapping
// 4. Inject procedural textures
```

## Technical Specifications

### Shader Format
```glsl
// GLSL ES 1.0 vs GLSL ES 3.0 support
#version 100  // Low precision, ES 1.0
#version 300 es // High precision, ES 3.0

// Multi-version support
// __multiversion__ marker
// Automatic version selection based on device capabilities
```

### Uniform Layout
```c++
// std140 layout for all uniform buffers
// Consistent across all shader types
// 16-byte alignment requirements
```

### Memory Management
```c++
mce::ConstantBufferOGL::createConstantBuffer()   // Allocate GPU memory
mce::BufferOGL::createBuffer()                    // Create vertex/index buffers
mce::FrameBufferObjectOGL::finalizeFrameBuffer()   // Clean up resources
```

## Performance Characteristics

### Draw Call Overhead
```c++
// JNI call overhead:
// - Shader compilation: High (one-time)
// - Uniform updates: Medium (per-frame)
// - Texture binding: Low (per-material)
// - Draw calls: Variable (per-render chunk)
```

### Memory Usage
```
// Uniform buffer memory:
// - EntityConstants: 64 bytes per entity
// - PerFrameConstants: 96 bytes per frame
// - HoloConstants: 80 bytes per holographic frame
// - Shader constants: Variable per shader program
```

## Technical Challenges

### Shader Compatibility
```
// Challenges:
// 1. Device capability detection
// 2. Version fallback handling
// 3. Precision limitations
// 4. Attribute/varying mapping
```

### Performance Optimization
```
// Optimization strategies:
// 1. Shader caching
// 2. Batch rendering
// 3. Texture atlasing
// 4. Uniform buffer objects
```

## NEXUS Integration Points

### Graphics Pipeline Bridges
1. **Shader Translation**: GLSL ES 1.0 ↔ GLSL ES 3.0
2. **Material System**: JSON-based material definition
3. **Texture Management**: Atlas-based texture system
4. **Uniform System**: Structured constant buffer management

### JNI Integration
1. **Shader Compilation**: Override JNI shader compilation
2. **Material Loading**: Intercept JNI material loading
3. **Texture Binding**: Override JNI texture binding
4. **Uniform Updates**: Inject uniform modifications

### Modding Strategies
1. **Shader Injection**: Add custom shader code
2. **Material Override**: Replace material definitions
3. **Texture Replacement**: Replace texture assets
4. **Uniform Modification**: Dynamic uniform updates

## Conclusion

The graphics pipeline analysis reveals:

1. **Complex Shader System**: 39 shader files, 26 materials, 230 passes
2. **Multi-Version Support**: Automatic GLSL version selection
3. **Structured Uniforms**: 8 uniform groups with specific layouts
4. **JNI Integration**: Extensive JNI graphics API
5. **Texture Atlasing**: Efficient texture management

For NEXUS graphics modding:
- **Primary focus**: JNI shader and material hooks
- **Secondary focus**: Asset replacement and modification
- **Advanced focus**: Runtime uniform injection
- **Technical focus**: Shader translation and optimization