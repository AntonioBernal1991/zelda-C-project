// =============================================================================
// world.h  —  El valle donde transcurre la aventura (ver world.c)
// =============================================================================
//
// Aquí está todo lo que forma el escenario: la forma del terreno (colinas,
// lago, acantilados), los "props" (árboles, rocas, arbustos, flores, ruinas)
// y la física básica: a qué altura está el suelo, por dónde se puede andar y
// cómo no atravesar los árboles.
//
// Este módulo NO dibuja: solo guarda los datos. gfx.c los lee para pintarlos.
// =============================================================================

#ifndef WORLD_H
#define WORLD_H

#include "math3d.h"
#include "mesh.h"

#define WATER_Y   -0.7f  // altura de la superficie del lago
#define WORLD_LIM 29.0f  // más allá de esta distancia del centro empiezan los acantilados

// Tipos de "props" (objetos del escenario)
enum { PROP_TREE, PROP_TREE2, PROP_PINE, PROP_ROCK, PROP_BUSH, PROP_FLOWER, PROP_PILLAR, PROP_PILLAR_BROKEN, PROP_PEDESTAL };

typedef struct {
    float x, z, r;       // posición en el suelo y radio de choque (0 = se puede atravesar)
    float scale, yaw;    // tamaño y giro (para que no sean todos iguales)
    int kind, var, alive; // tipo, variante (color de flor) y si existe (los arbustos se pueden cortar)
} Prop;

void  world_init(void);                  // crea el terreno, los modelos y coloca los props
void  world_reset(void);                 // vuelve a poner los arbustos cortados (al reiniciar)
float world_height(float x, float z);    // altura del suelo en un punto
V3    world_normal(float x, float z);    // hacia dónde mira el suelo (para saber si es cuesta)
int   world_walkable(float x, float z);  // 1 si se puede pisar (no es lago hondo ni acantilado)
int   world_collide(float *x, float *z, float radius); // si (x,z) choca con algo, lo empuja fuera
int   world_cut_bushes(float x, float z, float radius, V3 *cut, int max); // corta arbustos; devuelve cuántos
float world_pedestal_top(void);          // altura de la parte de arriba del pedestal central

// Datos que necesita gfx.c para dibujar
int   world_grid_n(void);                // celdas por lado del terreno
float world_grid_cell(void);             // tamaño de cada celda en metros
float world_grid_origin(void);           // coordenada de la esquina del terreno
float world_grid_height(int i, int j);   // altura en un punto de la rejilla
int   world_prop_count(void);
const Prop *world_prop(int i);
M34   world_prop_matrix(const Prop *p);  // dónde y cómo dibujar un prop
int   world_mesh_count(void);
const Mesh *world_mesh(int i);
int   world_prop_mesh(const Prop *p);    // qué modelo usa cada prop

#endif
