// =============================================================================
// game.c  —  "Mi Planta": un tamagotchi de una planta en pixel art
// =============================================================================
//
// CÓMO SE JUEGA
//   La planta tiene tres necesidades (agua, luz y cariño) que bajan solas con
//   el tiempo. Con el menú de abajo (REGAR, SOL, MIMAR) las vuelves a subir.
//   Si las tres están por encima de 60, la planta crece y pasa de etapa:
//   BROTE -> ARBOLITO -> ARBOL -> ARBOL MAGICO.
//
// CÓMO ESTÁ ORGANIZADO ESTE ARCHIVO
//   1. Colores, dibujos (sprites) y posiciones en pantalla.
//   2. El "estado": todos los datos que cambian mientras juegas.
//   3. game_update(): la lógica, se ejecuta 60 veces por segundo.
//   4. game_render() y sus ayudantes: dibujan la escena de atrás hacia delante.
//
// El árbol se dibuja en tree.c; aquí está todo lo demás (habitación, maceta
// con cara, menú, barras, partículas...).
// =============================================================================

#include "game.h"
#include "render.h"
#include "tree.h"
#include "util.h"

const char *game_title = "Mi Planta"; // título de la ventana

#define W  SCREEN_W       // ancho del lienzo (160)
#define H  SCREEN_H       // alto del lienzo (120)
#define S  2              // cada "píxel de dibujo" de los sprites ocupa 2x2 píxeles

// -----------------------------------------------------------------------------
// 1. PALETA DE COLORES (formato 0xRRGGBB)
// -----------------------------------------------------------------------------
#define C_OUTLINE 0x2b2135   // contorno oscuro de los dibujos
#define C_WALL    0xf2e2c4   // pared
#define C_WALL2   0xe9d5b2   // rayas de la pared
#define C_WOOD    0xa86a44   // madera (mesa y ventana)
#define C_WOOD_L  0xc88a5c   // madera clara (brillo)
#define C_WOOD_D  0x7a4a32   // madera oscura (sombra)
#define C_SKY     0x9fd6f0   // cielo de la ventana
#define C_SKY2    0xc4eaf8   // cielo más claro, abajo
#define C_WHITE   0xffffff
#define C_POT     0xd0703f   // barro de la maceta
#define C_POT_L   0xe8955e
#define C_POT_D   0x9a4a2c
#define C_SOIL    0x4a3024   // tierra
#define C_BLUSH   0xf58fa0   // mofletes
#define C_MOTE    0xfff4d8   // motas de polvo y rayos de sol
#define C_PANEL   0x2b2135   // fondo de los paneles de arriba y abajo
#define C_PANEL_L 0x4a3d5c
#define C_BAR_BG  0x5a4d6c   // fondo vacío de las barras
#define C_TEXT    0xf2e2c4
#define C_TEXT_D  0x8a7f9a   // texto apagado
#define C_WATER   0x4fa8e8
#define C_SUN     0xffcf3a
#define C_LOVE    0xf0607a
#define C_PETAL   0xf27ea0
#define C_PETAL_L 0xffc4d6
#define C_GROW    0x7fd36a   // barra de crecimiento

static const char *stage_names[] = { "BROTE", "ARBOLITO", "ARBOL", "ARBOL MAGICO" };
#define STAGE_COUNT TREE_STAGES

// -----------------------------------------------------------------------------
// SPRITES DIBUJADOS CON LETRAS
// Cada letra es un color (ver la función palette más abajo) y '.' es
// transparente. Así se puede "dibujar" directamente en el código.
// Las cadenas de 8 letras pegadas ("..." "...") C las une en una sola fila.
// -----------------------------------------------------------------------------

// Maceta (24x14). o = contorno, S = tierra, r/p/x = barro claro/medio/oscuro
static const char *pot_rows[] = {
    "oooooooo" "oooooooo" "oooooooo",
    "oSSSSSSS" "SSSSSSSS" "SSSSSSSo",
    "orpppppp" "pppppppp" "ppppppxo",
    "oooooooo" "oooooooo" "oooooooo",
    ".orppppp" "pppppppp" "pppppxo.",
    ".orppppp" "pppppppp" "pppppxo.",
    ".orppppp" "pppppppp" "pppppxo.",
    ".orppppp" "pppppppp" "pppppxo.",
    ".orppppp" "pppppppp" "pppppxo.",
    ".orppppp" "pppppppp" "pppppxo.",
    "..orpppp" "pppppppp" "ppppxo..",
    "..orpppp" "pppppppp" "ppppxo..",
    "..oxxxxx" "xxxxxxxx" "xxxxxo..",
    "...ooooo" "oooooooo" "ooooo...",
};
#define POT_ROWS 14

// Iconos de 5x5 para el menú y las barras: b = azul, y = amarillo, k = rosa, w = blanco
static const char *icon_water[] = { "..b..", ".bbb.", "bbbbb", "bbwbb", ".bbb." };
static const char *icon_sun[]   = { "y.y.y", ".yyy.", "yywyy", ".yyy.", "y.y.y" };
static const char *icon_heart[] = { ".k.k.", "kkkkk", "kwkkk", ".kkk.", "..k.." };

// Posiciones en pantalla (en píxeles del lienzo de 160x120)
#define POT_X   56    // esquina de la maceta
#define POT_Y   64
#define TABLE_Y 92    // donde empieza la mesa
#define MENU_Y  98    // donde empieza el menú de abajo
#define WIN_X0  14    // rectángulo de la ventana de la pared
#define WIN_Y0  18
#define WIN_X1  50
#define WIN_Y1  50

// -----------------------------------------------------------------------------
// 2. ESTADO DEL JUEGO
// -----------------------------------------------------------------------------

// Las tres acciones del menú. También sirven de índice para las tres barras.
enum { ACT_WATER, ACT_SUN, ACT_LOVE, ACT_COUNT };
static const char *menu_labels[ACT_COUNT] = { "REGAR", "SOL", "MIMAR" };

// Partículas: cosas pequeñas que se mueven un rato y desaparecen
enum { P_DROP, P_SPARK, P_HEART, P_MOTE };  // gota, destello, corazón, mota de polvo
typedef struct {
    float x, y, vx, vy;   // posición y velocidad (píxeles por segundo)
    float life;           // segundos que le quedan
    int type, alive;
} Particle;
#define MAX_PARTICLES 128

#define ACTION_TIME 1.6f   // segundos que dura cada acción (regar, sol, mimar)
#define ACTION_GAIN 35.0f  // cuánto sube la barra con cada acción

// #ifndef permite cambiarlo al compilar, por ejemplo -DGROW_TIME=2.0f para probar rápido
#ifndef GROW_TIME
#define GROW_TIME      20.0f // segundos bien cuidada para pasar de etapa
#endif
#define GROW_THRESHOLD 60.0f // todas las barras por encima de esto para crecer
#define EVOLVE_TIME    1.6f  // duración de la animación de crecer
#define MESSAGE_TIME   2.5f  // cuánto se ve el cartel "HA CRECIDO!"

// Colores de las hojas para los sprites (claro, medio, oscuro)
typedef struct { uint32_t light, mid, dark; } LeafPalette;
static const LeafPalette leaves_healthy = { 0x9be06a, 0x5fb84e, 0x3d8040 };

// Todos los datos del juego juntos en una sola variable "g"
static struct {
    float t;                // tiempo total de juego, en segundos (para animar)
    float stat[ACT_COUNT];  // agua, luz y cariño, de 0 a 100
    int   stage;            // etapa de crecimiento (0 = brote ... 3 = mágico)
    float growth;           // progreso hacia la siguiente etapa (0 a 1)
    float evolve_time;      // > 0 mientras dura la animación de crecer
    float message_time;     // > 0 mientras se ve "HA CRECIDO!"
    int   selected;         // opción del menú marcada
    int   action;           // acción en marcha (-1 = ninguna)
    float action_time;      // segundos que le quedan a la acción
    float spawn_acc, mote_acc; // relojes para soltar partículas cada cierto tiempo
    float blink_timer, blink_left; // cuándo parpadear y cuánto dura el parpadeo
    Particle p[MAX_PARTICLES];
} g;

// -----------------------------------------------------------------------------
// UTILIDADES
// -----------------------------------------------------------------------------

static uint32_t rng_state = 0x2545f491;   // semilla del azar
static float randf(float a, float b) { return rng_range(&rng_state, a, b); }

// Traduce una letra de sprite a su color. 0xffffffff significa "transparente".
static uint32_t palette(char c, const LeafPalette *lp) {
    switch (c) {
        case 'o': return C_OUTLINE;
        case 'l': return lp->light;
        case 'g': return lp->mid;
        case 'd': return lp->dark;
        case 's': return lp->mid;
        case 'S': return C_SOIL;
        case 'r': return C_POT_L;
        case 'p': return C_POT;
        case 'x': return C_POT_D;
        case 'b': return C_WATER;
        case 'y': return C_SUN;
        case 'k': return C_LOVE;
        case 'w': return C_WHITE;
        case 'f': return C_PETAL;
        case 'F': return C_PETAL_L;
        case 'c': return C_SUN;
    }
    return 0xffffffffu; // transparente
}

// Dibuja un sprite de letras. Cada letra se pinta como un cuadrado de
// scale x scale píxeles. row_offset (opcional) desplaza cada fila en X;
// así se podía "mecer" un sprite fila a fila.
static void draw_sprite(Framebuffer *fb, const char **rows, int nrows, int x, int y,
                        int scale, const int *row_offset, const LeafPalette *lp) {
    for (int r = 0; r < nrows; r++) {
        int ox = row_offset ? row_offset[r] : 0;
        for (int c = 0; rows[r][c]; c++) {
            uint32_t col = palette(rows[r][c], lp);
            if (col == 0xffffffffu) continue;  // transparente: no pintar
            fill_rect(fb, x + (c + ox) * scale, y + r * scale, scale, scale, col);
        }
    }
}

// Como fill_rect, pero sin salirse de una zona. Lo usamos para que las nubes
// solo se vean dentro del cristal de la ventana.
static void fill_rect_clip(Framebuffer *fb, int x, int y, int w, int h, uint32_t color,
                           int cx0, int cy0, int cx1, int cy1) {
    int x0 = x < cx0 ? cx0 : x, y0 = y < cy0 ? cy0 : y;
    int x1 = x + w > cx1 ? cx1 : x + w, y1 = y + h > cy1 ? cy1 : y + h;
    if (x1 > x0 && y1 > y0) fill_rect(fb, x0, y0, x1 - x0, y1 - y0, color);
}

// Pinta un "píxel de dibujo" (2x2) sobre la maceta, en la columna c y fila r
// del sprite. Lo usamos para dibujar la cara.
static void pot_px(Framebuffer *fb, int c, int r, uint32_t color) {
    fill_rect(fb, POT_X + c * S, POT_Y + r * S, S, S, color);
}

// Crea una partícula nueva en el primer hueco libre (si no hay hueco, no la crea).
static void spawn(int type, float x, float y, float vx, float vy, float life) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g.p[i].alive) continue;
        g.p[i] = (Particle){ x, y, vx, vy, life, type, 1 };
        return;
    }
}

// -----------------------------------------------------------------------------
// 3. LÓGICA
// -----------------------------------------------------------------------------

void game_init(void) {
    // La planta empieza bien cuidada para que se la vea crecer desde el principio
    g.stat[ACT_WATER] = 90;
    g.stat[ACT_SUN]   = 85;
    g.stat[ACT_LOVE]  = 85;
    g.action = -1;
    g.blink_timer = 2;
#ifdef START_STAGE
    g.stage = START_STAGE; // para pruebas: compilar con -DSTART_STAGE=n
#endif
    tree_init(); // genera la forma de los árboles de cada etapa
}

// Se llama 60 veces por segundo. dt = segundos desde la vez anterior.
int game_update(const Input *in, float dt) {
    if (in->pressed[KEY_ESCAPE]) return 0; // Esc: salir
    g.t += dt;

    // --- Menú: izquierda/derecha para elegir, Espacio/Enter para hacer la acción.
    // El "% ACT_COUNT" hace que al pasarse por un lado se vuelva por el otro.
    if (in->pressed[KEY_LEFT] || in->pressed[KEY_A])
        g.selected = (g.selected + ACT_COUNT - 1) % ACT_COUNT;
    if (in->pressed[KEY_RIGHT] || in->pressed[KEY_D])
        g.selected = (g.selected + 1) % ACT_COUNT;
    if ((in->pressed[KEY_SPACE] || in->pressed[KEY_ENTER]) && g.action < 0) {
        g.action = g.selected;
        g.action_time = ACTION_TIME;
        g.spawn_acc = 0;
    }

    // --- Las necesidades bajan con el tiempo (puntos por segundo)
    static const float decay[ACT_COUNT] = { 0.7f, 0.5f, 0.9f };
    for (int i = 0; i < ACT_COUNT; i++) {
        g.stat[i] -= decay[i] * dt;
        if (g.stat[i] < 0) g.stat[i] = 0;
    }

    // --- Acción en curso: la barra sube poco a poco y salen partículas
    if (g.action >= 0) {
        float *st = &g.stat[g.action];             // puntero a la barra que toca
        *st += ACTION_GAIN / ACTION_TIME * dt;
        if (*st > 100) *st = 100;

        // Cada "every" segundos sale una partícula del tipo de la acción
        g.spawn_acc += dt;
        float every = g.action == ACT_LOVE ? 0.18f : 0.05f;
        while (g.spawn_acc > every) {
            g.spawn_acc -= every;
            switch (g.action) {
            case ACT_WATER: spawn(P_DROP, randf(66, 94), 22, 0, randf(70, 90), 2); break;          // gotas que caen
            case ACT_SUN:   spawn(P_SPARK, randf(48, 112), randf(24, 86), 0, -6, randf(0.3f, 0.7f)); break; // destellos
            case ACT_LOVE:  spawn(P_HEART, randf(66, 90), 50, 0, -22, 1.4f); break;                // corazones que suben
            }
        }
        g.action_time -= dt;
        if (g.action_time <= 0) g.action = -1;     // la acción ha terminado
    }

    // --- Crecimiento: solo avanza si las tres barras están altas
    float lowest = 100;
    for (int i = 0; i < ACT_COUNT; i++) if (g.stat[i] < lowest) lowest = g.stat[i];
    if (g.evolve_time > 0) {
        // Está haciendo la animación de crecer; al terminar, sube de etapa
        g.evolve_time -= dt;
        if (g.evolve_time <= 0) {
            g.stage++;
            g.message_time = MESSAGE_TIME;
            for (int i = 0; i < 30; i++) // explosión de destellos
                spawn(P_SPARK, randf(50, 110), randf(20, 86), randf(-15, 15), randf(-25, -5), randf(0.5f, 1.2f));
        }
    } else if (g.stage < STAGE_COUNT - 1 && lowest >= GROW_THRESHOLD) {
        g.growth += dt / GROW_TIME;                // tarda GROW_TIME segundos en llenarse
        if (g.growth >= 1) {
            g.growth = 0;
            g.evolve_time = EVOLVE_TIME;           // empieza la animación de crecer
        }
    }
    if (g.message_time > 0) g.message_time -= dt;

    // --- Motas de polvo flotando en la luz (una cada 0,7 segundos)
    g.mote_acc += dt;
    if (g.mote_acc > 0.7f) {
        g.mote_acc = 0;
        spawn(P_MOTE, randf(10, 150), randf(16, 88), randf(1, 4), randf(-3, -1), randf(3, 6));
    }

    // --- Parpadeo de la cara: cada 2-5 segundos cierra los ojos 0,12 s
    g.blink_timer -= dt;
    g.blink_left  -= dt;
    if (g.blink_timer <= 0) {
        g.blink_left  = 0.12f;
        g.blink_timer = randf(2, 5);
    }

    // --- Mover las partículas: posición += velocidad * tiempo
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.p[i];
        if (!p->alive) continue;
        if (p->type == P_HEART) p->vx = m_sin(g.t * 5 + i) * 10; // los corazones zigzaguean
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->life -= dt;
        if (p->life <= 0) p->alive = 0;
        if (p->type == P_DROP && p->y > POT_Y + 2) p->alive = 0; // la gota llega a la tierra
    }
    return 1;
}

// -----------------------------------------------------------------------------
// 4. DIBUJO
// Se pinta de atrás hacia delante: lo último que se pinta queda encima.
// -----------------------------------------------------------------------------

// La habitación: pared con rayas, ventana con nubes y la mesa.
static void draw_room(Framebuffer *fb) {
    // Pared con rayas
    clear(fb, C_WALL);
    for (int x = 3; x < W; x += 8) fill_rect(fb, x, 0, 2, TABLE_Y, C_WALL2);

    // Ventana: cielo en dos tonos, una nube que pasa y el marco de madera
    int mid = (WIN_Y0 + WIN_Y1) / 2;
    fill_rect(fb, WIN_X0, WIN_Y0, WIN_X1 - WIN_X0, mid - WIN_Y0, C_SKY);
    fill_rect(fb, WIN_X0, mid, WIN_X1 - WIN_X0, WIN_Y1 - mid, C_SKY2);
    // La nube avanza con el tiempo; el % hace que vuelva a empezar por la izquierda
    int cx = WIN_X0 - 20 + ((int)(g.t * 4)) % (WIN_X1 - WIN_X0 + 30);
    fill_rect_clip(fb, cx,     28, 18, 4, C_WHITE, WIN_X0, WIN_Y0, WIN_X1, WIN_Y1);
    fill_rect_clip(fb, cx + 4, 25, 8,  3, C_WHITE, WIN_X0, WIN_Y0, WIN_X1, WIN_Y1);
    fill_rect_clip(fb, cx + 1, 41, 10, 3, C_WHITE, WIN_X0, WIN_Y0, WIN_X1, WIN_Y1);

    fill_rect(fb, WIN_X0 - 2, WIN_Y0 - 2, WIN_X1 - WIN_X0 + 4, 2, C_WOOD);   // marco de arriba
    fill_rect(fb, WIN_X0 - 2, WIN_Y0, 2, WIN_Y1 - WIN_Y0, C_WOOD);           // izquierda
    fill_rect(fb, WIN_X1, WIN_Y0, 2, WIN_Y1 - WIN_Y0, C_WOOD);               // derecha
    fill_rect(fb, (WIN_X0 + WIN_X1) / 2 - 1, WIN_Y0, 2, WIN_Y1 - WIN_Y0, C_WOOD); // cruz vertical
    fill_rect(fb, WIN_X0, mid - 1, WIN_X1 - WIN_X0, 2, C_WOOD);              // cruz horizontal
    fill_rect(fb, WIN_X0 - 4, WIN_Y1, WIN_X1 - WIN_X0 + 8, 3, C_WOOD_L);     // alféizar
    fill_rect(fb, WIN_X0 - 4, WIN_Y1 + 3, WIN_X1 - WIN_X0 + 8, 1, C_WOOD_D);

    // Mesa
    fill_rect(fb, 0, TABLE_Y, W, MENU_Y - TABLE_Y, C_WOOD);
    fill_rect(fb, 0, TABLE_Y, W, 1, C_WOOD_L);
    fill_rect(fb, POT_X + 4, TABLE_Y, 44, 2, C_WOOD_D); // sombra de la maceta
}

// La cara de la maceta. Se dibuja píxel a píxel con pot_px(columna, fila).
// mood: 0 triste, 1 normal, 2 contenta, 3 encantada (cuando la mimas)
static void draw_face(Framebuffer *fb, int mood) {
    int blinking = g.blink_left > 0;

    // Ojos
    if (mood == 3) {
        // Ojos felices en forma de ^ ^
        pot_px(fb, 7, 7, C_OUTLINE);  pot_px(fb, 8, 6, C_OUTLINE);  pot_px(fb, 9, 7, C_OUTLINE);
        pot_px(fb, 14, 7, C_OUTLINE); pot_px(fb, 15, 6, C_OUTLINE); pot_px(fb, 16, 7, C_OUTLINE);
    } else if (blinking || mood == 0) {
        // Ojos cerrados (una raya) al parpadear o si está triste
        for (int c = 8; c <= 9; c++)   pot_px(fb, c, 7, C_OUTLINE);
        for (int c = 14; c <= 15; c++) pot_px(fb, c, 7, C_OUTLINE);
        if (mood == 0) { pot_px(fb, 9, 5, C_OUTLINE); pot_px(fb, 14, 5, C_OUTLINE); } // cejas tristes
    } else {
        // Ojos abiertos de 2x2
        for (int r = 6; r <= 7; r++) {
            pot_px(fb, 8, r, C_OUTLINE);  pot_px(fb, 9, r, C_OUTLINE);
            pot_px(fb, 14, r, C_OUTLINE); pot_px(fb, 15, r, C_OUTLINE);
        }
        // Brillo de los ojos (un solo píxel de pantalla)
        fill_rect(fb, POT_X + 9 * S, POT_Y + 6 * S, 1, 1, C_WHITE);
        fill_rect(fb, POT_X + 15 * S, POT_Y + 6 * S, 1, 1, C_WHITE);
    }

    // Mofletes rosas (si no está triste)
    if (mood > 0) {
        pot_px(fb, 6, 8, C_BLUSH);  pot_px(fb, 7, 8, C_BLUSH);
        pot_px(fb, 16, 8, C_BLUSH); pot_px(fb, 17, 8, C_BLUSH);
    }

    // Boca
    switch (mood) {
    case 0: // triste: curva hacia abajo
        pot_px(fb, 11, 8, C_OUTLINE); pot_px(fb, 12, 8, C_OUTLINE);
        pot_px(fb, 10, 9, C_OUTLINE); pot_px(fb, 13, 9, C_OUTLINE);
        break;
    case 1: // normal: rayita
        pot_px(fb, 11, 9, C_OUTLINE); pot_px(fb, 12, 9, C_OUTLINE);
        break;
    case 2: // sonrisa: curva hacia arriba
        pot_px(fb, 10, 8, C_OUTLINE); pot_px(fb, 13, 8, C_OUTLINE);
        pot_px(fb, 11, 9, C_OUTLINE); pot_px(fb, 12, 9, C_OUTLINE);
        break;
    case 3: // boca abierta de alegría
        pot_px(fb, 11, 8, C_OUTLINE); pot_px(fb, 12, 8, C_OUTLINE);
        pot_px(fb, 11, 9, C_LOVE);    pot_px(fb, 12, 9, C_LOVE);
        break;
    }
}

// La planta entera: árbol + maceta + cara, con su humor y su balanceo.
static void draw_plant(Framebuffer *fb) {
    // El humor depende de la barra más baja
    float lowest = 100;
    for (int i = 0; i < ACT_COUNT; i++) if (g.stat[i] < lowest) lowest = g.stat[i];

    int mood;
    if (g.action == ACT_LOVE)                          mood = 3;
    else if (lowest < 25)                              mood = 0;
    else if (lowest > 60 || g.action >= 0)             mood = 2;
    else                                               mood = 1;

    // Balanceo con el viento: un seno que va y viene. Triste se mueve menos.
    float speed = mood == 0 ? 1.2f : 2.0f;
    float amp   = mood == 0 ? 0.8f : (g.action >= 0 ? 2.4f : 1.6f);
    float sway  = m_sin(g.t * speed) * amp;

    // Al crecer: parpadea en blanco alternando la etapa actual y la siguiente
    int stage = g.stage;
    int style = g.stat[ACT_WATER] < 25 ? TREE_WILTED : TREE_NORMAL; // sin agua, se mustia
    if (g.evolve_time > 0) {
        float k = 1 - g.evolve_time / EVOLVE_TIME;   // va de 0 a 1 durante la animación
        int flash = ((int)(k * k * 24)) % 2;          // alterna cada vez más rápido
        if (flash) stage = g.stage + 1;
        style = TREE_FLASH;
        sway = 0;
    }

    // Primero el árbol y luego la maceta encima: así el tronco queda "enterrado"
    tree_draw(fb, stage, POT_X + 12 * S, POT_Y + 3, g.t, sway, style);
    draw_sprite(fb, pot_rows, POT_ROWS, POT_X, POT_Y, S, 0, &leaves_healthy);
    draw_face(fb, mood);
}

// Dibuja las partículas. front = 0: las de detrás (polvo); front = 1: las de delante.
static void draw_particles(Framebuffer *fb, int front) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g.p[i];
        // El polvo va detrás de la planta y el resto delante
        if (!p->alive || (p->type == P_MOTE) == front) continue;
        int x = (int)p->x, y = (int)p->y;
        switch (p->type) {
        case P_MOTE:
            fill_rect(fb, x, y, 1, 1, C_MOTE);
            break;
        case P_DROP:
            fill_rect(fb, x, y, 1, 3, C_WATER);
            fill_rect(fb, x, y, 1, 1, C_WHITE);
            break;
        case P_SPARK: // un punto blanco que a ratos se convierte en estrellita
            fill_rect(fb, x, y, 1, 1, C_WHITE);
            if (((int)(p->life * 12)) % 2) {
                fill_rect(fb, x - 1, y, 3, 1, C_SUN);
                fill_rect(fb, x, y - 1, 1, 3, C_SUN);
            }
            break;
        case P_HEART:
            draw_sprite(fb, icon_heart, 5, x, y, 1, 0, &leaves_healthy);
            break;
        }
    }
}

// Nubecita que aparece encima al regar.
static void draw_rain_cloud(Framebuffer *fb) {
    fill_rect(fb, 63, 14, 34, 8, C_OUTLINE);
    fill_rect(fb, 69, 11, 14, 4, C_OUTLINE);
    fill_rect(fb, 64, 15, 32, 6, C_WHITE);
    fill_rect(fb, 70, 12, 12, 4, C_WHITE);
    fill_rect(fb, 64, 19, 32, 2, C_SKY2);
}

// Rayos de luz diagonales que entran desde arriba al darle SOL.
static void draw_sun_glow(Framebuffer *fb) {
    int phase = ((int)(g.t * 8)) % 4; // los rayos se desplazan un poco para parecer vivos
    for (int i = 0; i < 6; i++) {
        int x = 52 + i * 11 + phase;
        for (int y = 12; y < TABLE_Y; y += 4)
            fill_rect(fb, x + (y - 12) / 8, y, 1, 2, C_MOTE);
    }
}

// Panel de arriba: icono + barra de cada necesidad. Si una barra está
// por debajo de 25, parpadea para avisar.
static void draw_stats(Framebuffer *fb) {
    fill_rect(fb, 0, 0, W, 11, C_PANEL);
    fill_rect(fb, 0, 11, W, 1, C_PANEL_L);

    static const char **icons[ACT_COUNT] = { icon_water, icon_sun, icon_heart };
    static const uint32_t colors[ACT_COUNT] = { C_WATER, C_SUN, C_LOVE };
    int alert = ((int)(g.t * 4)) % 2; // cambia 4 veces por segundo

    for (int i = 0; i < ACT_COUNT; i++) {
        int x = 6 + i * 52;
        draw_sprite(fb, icons[i], 5, x, 3, 1, 0, &leaves_healthy);
        int bw = 36, fillw = (int)(bw * g.stat[i] / 100); // parte llena de la barra
        uint32_t c = (g.stat[i] < 25 && alert) ? C_TEXT : colors[i];
        fill_rect(fb, x + 8, 3, bw, 5, C_BAR_BG);
        fill_rect(fb, x + 8, 3, fillw, 5, c);
    }
}

// Menú de abajo con las tres opciones. La elegida se resalta y lleva flechitas.
static void draw_menu(Framebuffer *fb) {
    fill_rect(fb, 0, MENU_Y, W, H - MENU_Y, C_PANEL);
    fill_rect(fb, 0, MENU_Y, W, 1, C_PANEL_L);

    static const char **icons[ACT_COUNT] = { icon_water, icon_sun, icon_heart };
    for (int i = 0; i < ACT_COUNT; i++) {
        int cx = W / 6 + i * W / 3; // centro de cada tercio de la pantalla
        int sel = i == g.selected;
        if (sel) {
            fill_rect(fb, cx - 22, MENU_Y + 2, 44, H - MENU_Y - 4, C_PANEL_L);
            // Flechitas a los lados que se mueven un píxel adelante y atrás
            int bob = ((int)(g.t * 3)) % 2;
            fill_rect(fb, cx - 20 + bob, MENU_Y + 9,  1, 5, C_SUN);
            fill_rect(fb, cx - 19 + bob, MENU_Y + 10, 1, 3, C_SUN);
            fill_rect(fb, cx - 18 + bob, MENU_Y + 11, 1, 1, C_SUN);
            fill_rect(fb, cx + 20 - bob, MENU_Y + 9,  1, 5, C_SUN);
            fill_rect(fb, cx + 19 - bob, MENU_Y + 10, 1, 3, C_SUN);
            fill_rect(fb, cx + 18 - bob, MENU_Y + 11, 1, 1, C_SUN);
        }
        draw_sprite(fb, icons[i], 5, cx - 5, MENU_Y + 3, 2, 0, &leaves_healthy);
        const char *label = menu_labels[i];
        draw_text(fb, label, cx - text_width(label, 1) / 2, MENU_Y + 15, 1, sel ? C_TEXT : C_TEXT_D);
    }
}

// Cartel en la pared con el nombre de la etapa y la barra de crecimiento.
static void draw_growth(Framebuffer *fb) {
    const char *name = stage_names[g.stage];
    int x = 107, y = 70, bw = 48;
    draw_text(fb, name, x + (bw - text_width(name, 1)) / 2, y, 1, C_WOOD_D);

    fill_rect(fb, x, y + 8, bw, 4, C_OUTLINE);
    if (g.stage == STAGE_COUNT - 1) {
        fill_rect(fb, x + 1, y + 9, bw - 2, 2, C_PETAL);  // etapa final: barra rosa llena
    } else {
        float lowest = 100;
        for (int i = 0; i < ACT_COUNT; i++) if (g.stat[i] < lowest) lowest = g.stat[i];
        uint32_t c = lowest >= GROW_THRESHOLD ? C_GROW : C_TEXT_D; // gris si no está creciendo
        float progress = g.evolve_time > 0 ? 1 : g.growth;
        fill_rect(fb, x + 1, y + 9, (int)((bw - 2) * progress), 2, c);
    }
}

// Cartel grande de "HA CRECIDO!" en el centro.
static void draw_message(Framebuffer *fb) {
    if (g.message_time <= 0) return;
    const char *msg = g.stage == STAGE_COUNT - 1 ? "HA FLORECIDO!" : "HA CRECIDO!";
    int tw = text_width(msg, 2);
    int x = (W - tw) / 2, y = 17;
    fill_rect(fb, x - 5, y - 4, tw + 10, 18, C_OUTLINE);
    fill_rect(fb, x - 4, y - 3, tw + 8, 16, C_PANEL_L);
    draw_text(fb, msg, x, y + 1, 2, C_SUN);
}

// Dibuja la imagen completa, capa a capa, de atrás hacia delante.
void game_render(Framebuffer *fb) {
    draw_room(fb);
    draw_growth(fb);
    if (g.action == ACT_SUN) draw_sun_glow(fb);
    draw_particles(fb, 0);  // polvo, detrás de la planta
    draw_plant(fb);
    draw_particles(fb, 1);  // gotas, brillos y corazones, delante
    if (g.action == ACT_WATER) draw_rain_cloud(fb);
    draw_stats(fb);
    draw_menu(fb);
    draw_message(fb);
}
