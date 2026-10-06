// =============================================================================
// math3d.h  —  Vectores y matrices para el mundo en 3D
// =============================================================================
//
// VECTOR (V3): tres números (x, y, z). Sirve para dos cosas:
//   - una POSICIÓN en el mundo ("el héroe está en (3, 0, 11)"),
//   - una DIRECCIÓN o velocidad ("me muevo hacia (0, 0, 1)").
//
// Convenio de ejes de este juego:
//   X = derecha,  Y = arriba,  Z = adelante.   1 unidad = 1 metro.
//
// Un ángulo "yaw" (giro horizontal) mira en la dirección (sin yaw, 0, cos yaw):
//   yaw = 0 mira hacia +Z, yaw = PI/2 mira hacia +X, yaw = PI mira hacia -Z.
//
// MATRIZ (M34): una tabla de 3 filas x 4 columnas que guarda una
// TRANSFORMACIÓN: mover, girar y/o estirar algo. La gracia es que se pueden
// encadenar: "gira el brazo, luego llévalo al hombro, luego al cuerpo,
// luego a donde esté el héroe" se convierte en multiplicar matrices.
//
//   Las 3 primeras columnas = giro y escala.  La 4ª columna = desplazamiento.
// =============================================================================

#ifndef MATH3D_H
#define MATH3D_H

#include "../util.h"

typedef struct { float x, y, z; } V3;
typedef struct { float m[3][4]; } M34; // matriz 3x4: giro/escala + desplazamiento

// --- Operaciones con vectores ---
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }  // crear
static inline V3 v_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); } // sumar
static inline V3 v_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); } // restar (flecha de b a a)
static inline V3 v_mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }    // agrandar/encoger

// Producto escalar: mide cuánto apuntan dos direcciones hacia el mismo lado.
// Con direcciones de longitud 1: 1 = igual, 0 = perpendiculares, -1 = opuestas.
static inline float v_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Producto vectorial: da una dirección perpendicular a las dos.
// Con dos lados de un triángulo, sale la "normal": hacia dónde mira la cara.
static inline V3 v_cross(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static inline float v_len(V3 a) { return m_sqrt(v_dot(a, a)); } // longitud (Pitágoras en 3D)

// Normalizar: la misma dirección pero con longitud 1
static inline V3 v_norm(V3 a) {
    float l = v_len(a);
    return l > 0 ? v_mul(a, 1 / l) : a;
}

// --- Matrices básicas ---

// Identidad: no hace nada (como multiplicar por 1)
static inline M34 m_identity(void) {
    M34 r = {{{ 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }}};
    return r;
}
// Mover (x, y, z)
static inline M34 m_translate(float x, float y, float z) {
    M34 r = {{{ 1, 0, 0, x }, { 0, 1, 0, y }, { 0, 0, 1, z }}};
    return r;
}
// Estirar o encoger en cada eje
static inline M34 m_scale(float x, float y, float z) {
    M34 r = {{{ x, 0, 0, 0 }, { 0, y, 0, 0 }, { 0, 0, z, 0 }}};
    return r;
}
// Girar alrededor del eje Y (vertical): como una peonza
static inline M34 m_rot_y(float a) {
    float c = m_cos(a), s = m_sin(a);
    M34 r = {{{ c, 0, s, 0 }, { 0, 1, 0, 0 }, { -s, 0, c, 0 }}};
    return r;
}
// Girar alrededor del eje X: como asentir con la cabeza
static inline M34 m_rot_x(float a) {
    float c = m_cos(a), s = m_sin(a);
    M34 r = {{{ 1, 0, 0, 0 }, { 0, c, -s, 0 }, { 0, s, c, 0 }}};
    return r;
}
// Girar alrededor del eje Z: como inclinar la cabeza hacia un hombro
static inline M34 m_rot_z(float a) {
    float c = m_cos(a), s = m_sin(a);
    M34 r = {{{ c, -s, 0, 0 }, { s, c, 0, 0 }, { 0, 0, 1, 0 }}};
    return r;
}

// Encadenar dos transformaciones: el resultado hace PRIMERO b y LUEGO a.
// (Se lee de derecha a izquierda, como en matemáticas.)
static inline M34 m_mul(M34 a, M34 b) {
    M34 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j]
                      + (j == 3 ? a.m[i][3] : 0);
    return r;
}

// Aplicar la transformación a un punto (gira, estira y desplaza)
static inline V3 m_apply(const M34 *m, V3 v) {
    return v3(m->m[0][0] * v.x + m->m[0][1] * v.y + m->m[0][2] * v.z + m->m[0][3],
              m->m[1][0] * v.x + m->m[1][1] * v.y + m->m[1][2] * v.z + m->m[1][3],
              m->m[2][0] * v.x + m->m[2][1] * v.y + m->m[2][2] * v.z + m->m[2][3]);
}

// Aplicarla a una dirección: solo gira y estira, sin desplazar
// (una dirección no tiene posición, "hacia arriba" es hacia arriba esté donde esté)
static inline V3 m_apply_dir(const M34 *m, V3 v) {
    return v3(m->m[0][0] * v.x + m->m[0][1] * v.y + m->m[0][2] * v.z,
              m->m[1][0] * v.x + m->m[1][1] * v.y + m->m[1][2] * v.z,
              m->m[2][0] * v.x + m->m[2][1] * v.y + m->m[2][2] * v.z);
}

// Deja un ángulo entre -PI y PI. Sirve para girar siempre por el camino corto:
// de 350° a 10° hay que girar +20°, no -340°.
static inline float wrap_angle(float a) {
    while (a > PI)  a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

#endif
