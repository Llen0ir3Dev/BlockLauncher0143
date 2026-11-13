# Native dependencies

ELF: 32-bit ARM EABI5, PIE shared object, stripped.

## DT_NEEDED

- `libfmod.so`
- `libgnustl_shared.so`
- `liblog.so`
- `libandroid.so`
- `libEGL.so`
- `libGLESv2.so`
- `libOpenSLES.so`
- `libz.so`
- `libstdc++.so`
- `libm.so`
- `libc.so`
- `libdl.so`

## Imported FMOD / GL / EGL / OpenSL symbols

```
         U AAssetManager_open
         U AAsset_close
         U AAsset_getBuffer
         U AAsset_getLength
         U ANativeActivity_finish
         U ANativeWindow_setBuffersGeometry
         U FMOD_System_Create
         U eglChooseConfig
         U eglCreateContext
         U eglCreateWindowSurface
         U eglDestroyContext
         U eglDestroySurface
         U eglGetConfigAttrib
         U eglGetDisplay
         U eglGetProcAddress
         U eglInitialize
         U eglMakeCurrent
         U eglQuerySurface
         U eglSwapBuffers
         U eglSwapInterval
         U eglTerminate
         U fmod
         U fmodf
         U glActiveTexture
         U glAttachShader
         U glBindBuffer
         U glBindFramebuffer
         U glBindRenderbuffer
         U glBindTexture
         U glBlendFunc
         U glBlendFuncSeparate
         U glBufferData
         U glBufferSubData
         U glCheckFramebufferStatus
         U glClear
         U glClearColor
         U glClearDepthf
         U glClearStencil
         U glColorMask
         U glCompileShader
         U glCreateProgram
         U glCreateShader
         U glCullFace
         U glDeleteBuffers
         U glDeleteFramebuffers
         U glDeleteProgram
         U glDeleteRenderbuffers
         U glDeleteShader
         U glDeleteTextures
         U glDepthFunc
         U glDepthMask
         U glDepthRangef
         U glDisable
         U glDrawArrays
         U glDrawElements
         U glEnable
         U glEnableVertexAttribArray
         U glFlush
         U glFramebufferRenderbuffer
         U glFramebufferTexture2D
         U glGenBuffers
         U glGenFramebuffers
         U glGenRenderbuffers
         U glGenTextures
         U glGetActiveAttrib
         U glGetActiveUniform
         U glGetAttribLocation
         U glGetError
         U glGetIntegerv
         U glGetProgramInfoLog
         U glGetProgramiv
         U glGetShaderInfoLog
         U glGetShaderPrecisionFormat
         U glGetShaderiv
         U glGetString
         U glGetTexParameteriv
         U glGetUniformLocation
         U glIsTexture
         U glLinkProgram
         U glPolygonOffset
```
