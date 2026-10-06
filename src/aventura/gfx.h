#ifndef GFX_H
#define GFX_H

// =============================================================================
// gfx.h  —  El renderizador con la tarjeta gráfica (ver gfx.c)
// =============================================================================
// Dibuja con OpenGL 3.3 y shaders propios: cielo con nubes, terreno, hierba
// con viento, agua, sombras reales, niebla, partículas brillantes y la
// interfaz en pixel art por encima.
//
// Uso en cada imagen:
//     gfx_begin(...);              // dónde está la cámara
//     gfx_draw(...); gfx_draw(...) // apuntar los modelos a dibujar
//     gfx_particle(...);           // apuntar partículas
//     gfx_end(interfaz, ...);      // ahora se dibuja todo de verdad
// =============================================================================

#include "math3d.h"
#include "mesh.h"

int  gfx_init(void);              // necesita world_init() hecho; 0 si falla
const char *gfx_error(void);      // mensaje del último error, o NULL

int  gfx_mesh(const Mesh *m);     // sube un modelo a la GPU y devuelve su número

// Un frame: begin, draws y particles, end
void gfx_begin(int width, int height, V3 cam, float yaw, float pitch, V3 focus, float time);
void gfx_draw(int mesh, const M34 *model, uint32_t flash, float gloss, int casts_shadow);
void gfx_particle(V3 pos, float size, uint32_t color, float alpha, int glow);
void gfx_end(const uint32_t *overlay, int ow, int oh); // overlay: 0xFF00FF = transparente

#define GFX_OVERLAY_KEY 0xff00ffu

#endif
