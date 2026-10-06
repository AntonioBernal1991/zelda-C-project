// =============================================================================
// render.h  —  Herramientas de dibujo 2D (ver render.c)
// =============================================================================
// Un ".h" (cabecera) es como el índice de un libro: dice qué funciones
// existen y cómo se usan. El código de verdad está en el ".c" del mismo nombre.
// Los colores van en formato 0xRRGGBB (por ejemplo 0x00FF00 = verde).
// =============================================================================

#ifndef RENDER_H
#define RENDER_H

#include "game.h"

void clear(Framebuffer *fb, uint32_t color);                                  // pinta todo de un color
void fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color);  // rectángulo relleno
void draw_text(Framebuffer *fb, const char *text, int x, int y, int scale, uint32_t color); // texto
int  text_width(const char *text, int scale);                                 // cuánto mide un texto

#endif
