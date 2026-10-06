// =============================================================================
// glload.h  —  Cargador de OpenGL hecho a mano
// =============================================================================
//
// OPENGL es la forma de pedirle cosas a la tarjeta gráfica (GPU): "guarda
// estos triángulos", "usa este programa para pintarlos", "dibújalos"...
//
// El problema: Windows solo trae de serie las funciones de OpenGL 1.1 (del
// año 1997). Las modernas las tiene el driver de tu tarjeta (NVIDIA, AMD...),
// y hay que pedirle la dirección de cada una por su nombre. Normalmente esto
// lo hace una librería (GLEW o GLAD); aquí lo hacemos nosotros.
//
// Para no escribir lo mismo 37 veces usamos un truco llamado "X-macro":
// GL_FUNCS es una lista de (tipo, nombre). Según cómo definamos X, la misma
// lista sirve para declarar las variables (aquí abajo), crearlas y cargarlas
// (en glload.c).
// =============================================================================

#ifndef GLLOAD_H
#define GLLOAD_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>      // OpenGL 1.1 (viene con Windows)
#include <GL/glext.h>   // solo los TIPOS de las funciones modernas, no las funciones

// Las funciones modernas que usa el juego. Cada línea: (tipo de función, nombre).
#define GL_FUNCS(X) \
    X(PFNGLCREATESHADERPROC,                   glCreateShader) \
    X(PFNGLSHADERSOURCEPROC,                   glShaderSource) \
    X(PFNGLCOMPILESHADERPROC,                  glCompileShader) \
    X(PFNGLGETSHADERIVPROC,                    glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC,               glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC,                   glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC,                  glCreateProgram) \
    X(PFNGLATTACHSHADERPROC,                   glAttachShader) \
    X(PFNGLLINKPROGRAMPROC,                    glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC,                   glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC,              glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC,                     glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC,             glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC,                      glUniform1i) \
    X(PFNGLUNIFORM1FPROC,                      glUniform1f) \
    X(PFNGLUNIFORM2FPROC,                      glUniform2f) \
    X(PFNGLUNIFORM3FPROC,                      glUniform3f) \
    X(PFNGLUNIFORM4FPROC,                      glUniform4f) \
    X(PFNGLUNIFORMMATRIX4FVPROC,               glUniformMatrix4fv) \
    X(PFNGLGENVERTEXARRAYSPROC,                glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC,                glBindVertexArray) \
    X(PFNGLGENBUFFERSPROC,                     glGenBuffers) \
    X(PFNGLBINDBUFFERPROC,                     glBindBuffer) \
    X(PFNGLBUFFERDATAPROC,                     glBufferData) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,            glVertexAttribPointer) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,        glEnableVertexAttribArray) \
    X(PFNGLGENFRAMEBUFFERSPROC,                glGenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC,                glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,           glFramebufferTexture2D) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,        glFramebufferRenderbuffer) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,         glCheckFramebufferStatus) \
    X(PFNGLGENRENDERBUFFERSPROC,               glGenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC,               glBindRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC,            glDeleteRenderbuffers) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLBLITFRAMEBUFFERPROC,                glBlitFramebuffer) \
    X(PFNGLACTIVETEXTUREPROC,                  glActiveTexture)

// Para cada función de la lista, declara una variable con su nombre que
// guardará la dirección de la función. Ejemplo: "extern PFNGLCREATESHADERPROC glCreateShader;"
// Así luego se puede llamar como si fuera una función normal: glCreateShader(...)
#define X(type, name) extern type name;
GL_FUNCS(X)
#undef X

int gl_load(void); // pide todas las funciones al driver; devuelve 1 si las encontró todas

#endif
