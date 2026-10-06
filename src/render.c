// =============================================================================
// render.c  —  Dibujo 2D básico: borrar, rectángulos y texto
// =============================================================================
//
// Todo lo que se ve en pantalla en la planta (y la interfaz de la aventura)
// se construye con estas pocas herramientas. No hay imágenes cargadas de
// archivos: hasta las letras están dibujadas a mano, píxel a píxel.
// =============================================================================

#include "render.h"

// Pinta todo el lienzo de un mismo color.
void clear(Framebuffer *fb, uint32_t color) {
    int n = fb->width * fb->height;
    for (int i = 0; i < n; i++) fb->pixels[i] = color;
}

// Pinta un rectángulo relleno. (x, y) es la esquina de arriba a la izquierda.
void fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color) {
    // Recortar: si el rectángulo se sale del lienzo, solo pintamos la parte
    // de dentro. Si no, escribiríamos fuera de la memoria y el programa fallaría.
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->width ? fb->width : x + w;
    int y1 = y + h > fb->height ? fb->height : y + h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = fb->pixels + py * fb->width; // principio de la fila py
        for (int px = x0; px < x1; px++) row[px] = color;
    }
}

// -----------------------------------------------------------------------------
// Fuente de letras de 3x5 píxeles hecha a mano.
//
// Cada letra son 5 filas. Cada fila es un número del 0 al 7 que indica qué
// 3 píxeles se encienden, usando bits:  4 = izquierda, 2 = centro, 1 = derecha.
//
//   Ejemplo, la "A" = {2,5,7,5,5}:
//       2 = .#.
//       5 = #.#      (4 + 1)
//       7 = ###      (4 + 2 + 1)
//       5 = #.#
//       5 = #.#
// -----------------------------------------------------------------------------
static const uint8_t *glyph(char c) {
    static const uint8_t digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
    };
    static const uint8_t letters[26][5] = {
        {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6}, {7,4,6,4,7}, // A-E
        {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,2}, // F-J
        {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {6,5,5,5,5}, {2,5,5,5,2}, // K-O
        {6,5,6,4,4}, {2,5,5,6,3}, {6,5,6,5,5}, {3,4,2,1,6}, {7,2,2,2,2}, // P-T
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2}, // U-Y
        {7,1,2,4,7},                                                     // Z
    };
    static const uint8_t excl[5]  = {2,2,2,0,2};
    static const uint8_t colon[5] = {0,2,0,2,0};
    static const uint8_t dash[5]  = {0,0,7,0,0};
    static const uint8_t slash[5] = {1,1,2,4,4};
    static const uint8_t dot[5]   = {0,0,0,0,2};

    // Los caracteres en C son números (código ASCII): '0'..'9' y 'A'..'Z'
    // van seguidos, así que restando sabemos la posición en la tabla.
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'a' && c <= 'z') c -= 32;          // minúscula -> mayúscula
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    switch (c) {
        case '!': return excl;
        case ':': return colon;
        case '-': return dash;
        case '/': return slash;
        case '.': return dot;
    }
    return 0; // espacio o carácter que no tenemos: no se dibuja nada
}

// Escribe un texto. "scale" agranda las letras (2 = cada píxel de la letra
// ocupa 2x2 píxeles). Cada letra avanza 4 columnas: 3 de letra + 1 de hueco.
void draw_text(Framebuffer *fb, const char *text, int x, int y, int scale, uint32_t color) {
    for (; *text; text++, x += 4 * scale) {          // recorre el texto letra a letra
        const uint8_t *g = glyph(*text);
        if (!g) continue;
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (g[row] & (4 >> col))              // ¿está encendido este bit?
                    fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color);
    }
}

// Ancho en píxeles que ocupará un texto (útil para centrarlo).
int text_width(const char *text, int scale) {
    int n = 0;
    while (text[n]) n++;                              // contar letras
    return n > 0 ? (n * 4 - 1) * scale : 0;           // sin el hueco de la última
}
