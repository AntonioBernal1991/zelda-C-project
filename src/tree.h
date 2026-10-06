// =============================================================================
// tree.h  —  El árbol mágico de la planta (ver tree.c)
// =============================================================================

#ifndef TREE_H
#define TREE_H

#include "game.h"

#define TREE_STAGES 4 // brote, arbolito, árbol y árbol mágico

// Estilos de dibujo del árbol
enum {
    TREE_NORMAL, // sano y con todos sus adornos
    TREE_WILTED, // mustio (le falta agua): hojas marrones y sin adornos
    TREE_FLASH   // todo blanco, para la animación de crecer
};

// Calcula la forma de los árboles de todas las etapas. Se llama una vez al
// empezar. Usa semillas fijas, así que el árbol sale siempre igual.
void tree_init(void);

// Dibuja el árbol de una etapa con la base del tronco en (base_x, base_y).
// t = tiempo (para animar), sway = cuántos píxeles mueve el viento la copa.
void tree_draw(Framebuffer *fb, int stage, int base_x, int base_y, float t, float sway, int style);

#endif
