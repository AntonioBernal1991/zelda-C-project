// =============================================================================
// mesh.h  —  Modelos 3D hechos con formas simples (ver mesh.c)
// =============================================================================
//
// Un MODELO 3D (en inglés "mesh", malla) es una lista de TRIÁNGULOS. Cada
// triángulo tiene 3 VÉRTICES (esquinas) en 3D y un color. Todo lo que ves en
// el juego (el héroe, los árboles, los limos...) son muchos triangulitos.
//
// Aquí no se cargan modelos de archivos: se construyen por código juntando
// cajas, cilindros, esferas... Por ejemplo, un árbol = cilindro (tronco) +
// varias esferas (copa).
//
// Forma de uso:
//     mesh_begin();                       // empieza un modelo nuevo
//     add_cyl(...);  add_ellipsoid(...);  // le añades piezas
//     Mesh arbol = mesh_end();            // y lo cierras
// =============================================================================

#ifndef MESH_H
#define MESH_H

#include "math3d.h"

// Un modelo no guarda los triángulos dentro: solo dice dónde empiezan y
// cuántos son en el almacén común (todos los modelos comparten la memoria).
typedef struct {
    int v0, nv;    // primer vértice y cuántos vértices tiene
    int t0, nt;    // primer triángulo y cuántos triángulos tiene
    float radius;  // distancia del origen al punto más lejano
} Mesh;

void mesh_begin(void);
void mesh_xform(M34 m);   // mover/girar las piezas que se añadan a partir de ahora
void mesh_smooth(int on); // esferas suaves (1, lo normal) o facetadas como gemas (0)
Mesh mesh_end(void);

// Piezas para construir. Los colores van en 0xRRGGBB.
void add_box(float cx, float cy, float cz, float sx, float sy, float sz, uint32_t c);   // caja: centro y tamaño
void add_cyl(float cx, float cy, float cz, float rb, float rt, float h, int sides, uint32_t c); // cilindro: centro de la base, radio abajo, radio arriba, altura (rt=0 hace un cono)
void add_ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int seg, int rings, uint32_t c); // esfera estirada: centro, radios y nº de divisiones
void add_gem(float cx, float cy, float cz, float r, float h, int sides, uint32_t c);    // gema: dos pirámides unidas por la base
void add_disk(float cx, float cy, float cz, float r, int sides, uint32_t c);            // círculo plano tumbado

// Convierte el modelo en la lista de números que entiende la tarjeta gráfica:
// cada vértice son 10 números seguidos:
//    posición (x, y, z), normal (hacia dónde mira: x, y, z), color (r, g, b, a)
// Devuelve cuántos vértices escribió.
#define MESH_VERTEX_FLOATS 10
int mesh_export(const Mesh *m, float *out, int max_verts);

#endif
