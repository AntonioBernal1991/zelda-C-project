// =============================================================================
// game.h  —  El "contrato" entre la ventana y el juego
// =============================================================================
//
// El programa está partido en dos mitades que no se conocen entre sí:
//
//   1) La PLATAFORMA (main.c): habla con Windows. Abre la ventana, lee el
//      teclado, mide el tiempo y enseña los píxeles en pantalla.
//
//   2) El JUEGO (game.c para la planta): decide qué pasa y qué se dibuja.
//      No sabe nada de Windows; solo recibe teclas y un "lienzo" de píxeles.
//
// Este archivo define cómo se comunican: qué teclas existen, cómo es el
// lienzo y qué tres funciones tiene que tener todo juego. Gracias a esta
// separación se puede llevar el juego a otro aparato (por ejemplo una
// Raspberry Pi Pico) cambiando solo la plataforma.
// =============================================================================

#ifndef GAME_H   // Estas tres líneas (y el #endif del final) evitan que el
#define GAME_H   // archivo se lea dos veces si varios archivos lo incluyen.

#include <stdint.h> // tipos de tamaño fijo, como uint32_t (número de 32 bits sin signo)

// Resolución interna del juego: el tamaño "de verdad" de la imagen, en píxeles.
// Luego se agranda para llenar la ventana. Cada juego elige la suya al
// compilar con -DSCREEN_W=... -DSCREEN_H=... (mira build.bat); si no se
// dice nada, se usan estos valores.
#ifndef SCREEN_W
#define SCREEN_W 160
#endif
#ifndef SCREEN_H
#define SCREEN_H 120
#endif

// El lienzo donde dibuja el juego: una lista de píxeles fila a fila.
// Cada píxel es un número 0x00RRGGBB: dos cifras hexadecimales para el rojo,
// dos para el verde y dos para el azul. Por ejemplo 0xFF0000 es rojo puro.
// El píxel (x, y) está en la posición  y * width + x.
typedef struct {
    uint32_t *pixels;
    int width, height;
} Framebuffer;

// Las teclas que entiende el juego (la plataforma traduce las de Windows a estas).
// KEY_COUNT no es una tecla: al estar la última, vale "cuántas teclas hay".
enum {
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_W, KEY_S, KEY_A, KEY_D,
    KEY_Q, KEY_E, KEY_J,
    KEY_SPACE, KEY_ENTER, KEY_ESCAPE,
    KEY_COUNT
};

// Estado del teclado en cada momento.
typedef struct {
    int down[KEY_COUNT];    // 1 mientras la tecla esté apretada (para moverse)
    int pressed[KEY_COUNT]; // 1 solo en el instante en que se aprieta (para acciones como saltar)
} Input;

// Título de la ventana. "extern" significa: existe, pero está escrito en
// otro archivo (cada juego pone el suyo).
extern const char *game_title;

// Las funciones que todo juego debe tener:
void game_init(void);                        // se llama una vez al empezar
int  game_update(const Input *in, float dt); // se llama en cada imagen; dt = segundos desde la anterior.
                                             // Devuelve 0 si el juego quiere cerrarse.
void game_render(Framebuffer *fb);           // dibuja la imagen actual en el lienzo

#endif
