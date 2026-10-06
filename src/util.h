// =============================================================================
// util.h  —  Matemáticas y números aleatorios hechos a mano
// =============================================================================
//
// Normalmente se usaría la librería de C (math.h para sin/cos/sqrt y rand()
// para el azar). Aquí lo hacemos nosotros, para entender cómo funcionan por
// dentro. Lo usan los dos juegos.
//
// "static inline" = el compilador copia la función donde se use, así puede
// estar en un .h sin dar problemas y además va más rápido.
// =============================================================================

#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

#define PI 3.14159265f // la "f" final indica número float (decimal de 32 bits)

// -----------------------------------------------------------------------------
// Seno, con una "serie de Taylor": una suma de potencias que se parece mucho
// al seno cuando el ángulo es pequeño:
//     sin(x) ≈ x - x³/6 + x⁵/120 - x⁷/5040 + x⁹/362880
// Como solo es exacta cerca de 0, primero llevamos el ángulo a la zona
// entre -90° y +90° (de -PI/2 a PI/2) usando que el seno se repite.
// Los ángulos van en radianes: una vuelta entera = 2*PI.
// -----------------------------------------------------------------------------
static inline float m_sin(float x) {
    // Quitar vueltas completas para dejarlo entre -PI y PI
    int k = (int)(x / (2 * PI));
    x -= k * 2 * PI;
    if (x > PI)  x -= 2 * PI;
    if (x < -PI) x += 2 * PI;
    // sin(PI - x) = sin(x): "doblar" el ángulo hacia la zona central
    if (x > PI / 2)  x = PI - x;
    if (x < -PI / 2) x = -PI - x;
    // La serie de Taylor escrita de forma anidada para hacer menos operaciones
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}

// El coseno es el seno adelantado un cuarto de vuelta.
static inline float m_cos(float x) { return m_sin(x + PI / 2); }

// Valor absoluto: quita el signo negativo.
static inline float m_abs(float v) { return v < 0 ? -v : v; }

// -----------------------------------------------------------------------------
// Raíz cuadrada con el "método de Newton":
//   si r es una aproximación de la raíz de v, entonces (r + v/r) / 2 es una
//   aproximación mejor. Repitiéndolo 3 veces ya es casi exacta.
// Para empezar cerca, usamos un truco: un float guarda su exponente en
// ciertos bits; dividir esos bits entre 2 equivale más o menos a hacer la
// raíz. La "union" nos deja ver el mismo número como float o como entero.
// -----------------------------------------------------------------------------
static inline float m_sqrt(float v) {
    if (v <= 0) return 0;
    union { float f; uint32_t i; } u = { v };
    u.i = (u.i >> 1) + 0x1fc00000; // aproximación inicial por bits
    float r = u.f;
    for (int i = 0; i < 3; i++) r = 0.5f * (r + v / r); // Newton
    return r;
}

// -----------------------------------------------------------------------------
// atan2(y, x): el ángulo de la flecha que va de (0,0) a (x, y).
// Sirve, por ejemplo, para saber hacia dónde mirar para ir hacia un punto.
// Usamos una fórmula aproximada (error menor de 0,3 grados), suficiente
// para orientar personajes y cámaras.
// -----------------------------------------------------------------------------
static inline float m_atan2(float y, float x) {
    float ax = m_abs(x), ay = m_abs(y);
    if (ax < 1e-9f && ay < 1e-9f) return 0;   // flecha de longitud 0: da igual
    if (ax >= ay) {
        // Ángulos "más horizontales": la fórmula funciona bien con y/x
        float z = y / x;
        float a = z * (PI / 4) + 0.273f * z * (1 - m_abs(z));
        if (x < 0) a += y >= 0 ? PI : -PI;    // si apunta a la izquierda, media vuelta
        return a;
    }
    // Ángulos "más verticales": usamos x/y y lo corregimos
    float z = x / y;
    return (y > 0 ? PI / 2 : -PI / 2) - (z * (PI / 4) + 0.273f * z * (1 - m_abs(z)));
}

// Redondear al entero más cercano (2.6 -> 3, -2.6 -> -3).
static inline int iround(float v) { return (int)(v + (v >= 0 ? 0.5f : -0.5f)); }

// -----------------------------------------------------------------------------
// Números aleatorios con "xorshift": se mezclan los bits del número anterior
// desplazándolos y combinándolos (^ es "o exclusivo"). El resultado parece
// azar, pero si se empieza con la misma "semilla" sale siempre la misma
// secuencia. Eso nos viene genial para que el bosque se genere igual cada vez.
// Cada parte del juego guarda su propia semilla y la pasa por puntero.
// -----------------------------------------------------------------------------
static inline uint32_t rng_next(uint32_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

// Número decimal aleatorio entre a y b.
static inline float rng_range(uint32_t *s, float a, float b) {
    return a + (b - a) * (rng_next(s) % 10000) / 10000.0f;
}

#endif
