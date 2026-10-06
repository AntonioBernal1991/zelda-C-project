// =============================================================================
// glload.c  —  Pide al driver de la tarjeta gráfica las funciones de OpenGL
// =============================================================================
// (Lee primero la explicación de glload.h)

#include "glload.h"

// Crear de verdad una variable por cada función de la lista (empiezan vacías)
#define X(type, name) type name;
GL_FUNCS(X)
#undef X

// En C, para convertir entre punteros a funciones de tipos distintos sin que
// el compilador se queje, se pasa por este tipo "comodín".
typedef void (*GLproc)(void);

// Busca una función por su nombre.
static GLproc load(const char *name) {
    // Primero se le pregunta al driver...
    PROC p = wglGetProcAddress(name);
    // ...algunos drivers, si no la tienen, devuelven 1, 2, 3 o -1 en vez de NULL.
    // En ese caso la buscamos en el OpenGL básico de Windows (opengl32.dll).
    if (p == 0 || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1)
        p = GetProcAddress(GetModuleHandleA("opengl32.dll"), name);
    return (GLproc)p;
}

int gl_load(void) {
    int ok = 1;
    // Para cada función de la lista: glCreateShader = load("glCreateShader"); etc.
    // El "#name" convierte el nombre en texto entre comillas.
#define X(type, name) name = (type)load(#name); if (!name) ok = 0;
    GL_FUNCS(X)
#undef X
    return ok;
}
