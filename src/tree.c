// =============================================================================
// tree.c  —  El árbol mágico, generado por código y pintado píxel a píxel
// =============================================================================
//
// En vez de dibujar el árbol a mano, lo "fabricamos" con reglas:
//
//   1. GENERAR (una sola vez, en tree_init):
//      - Un tronco hecho de varias hebras que se enroscan.
//      - Ramas que se dividen en dos una y otra vez (como un árbol de verdad).
//      - En la punta de cada rama, un racimo de hojas ("blob": un círculo).
//      - Adornos repartidos por la copa: musgo colgante, cristales, flores,
//        lucecitas... según la etapa.
//      Para elegir ángulos y tamaños se usa azar con semilla fija, así que
//      el árbol sale idéntico cada vez que se abre el juego.
//
//   2. DIBUJAR (en cada imagen, en tree_draw):
//      Se pinta todo con círculos ("disk"): primero un círculo oscuro un poco
//      más grande (el contorno) y encima el de color. El viento desplaza más
//      la copa que la base.
//
// Coordenadas: (0,0) es la base del tronco. La Y negativa es hacia arriba
// (en pantalla, la Y crece hacia abajo).
// =============================================================================

#include "tree.h"
#include "util.h"

// Máximo de piezas por árbol (para reservar la memoria de antemano)
#define MAX_BRANCHES 64
#define MAX_BLOBS    128
#define MAX_DECOS    160

// Una rama es una curva suave (curva de Bézier cuadrática): va de (x0,y0) a
// (x1,y1) y se dobla hacia el "punto de control" (cx,cy). Su grosor pasa de
// r0 a r1 a lo largo de la rama.
typedef struct { float x0, y0, cx, cy, x1, y1, r0, r1; } Branch;

// Un racimo de hojas: un círculo de radio r con una paleta de color.
typedef struct { float x, y, r; int pal; } Blob;

// Adornos de la copa
enum { D_VINE, D_CRYSTAL, D_FLOWER, D_LIGHT }; // musgo, cristal, flor, lucecita
typedef struct { float x, y, len; int kind, var; } Deco; // var = variante (color, o si lleva farolillo)

// Todo lo que forma un árbol
typedef struct {
    Branch br[MAX_BRANCHES]; int nbr;   // ramas y cuántas hay
    Blob   bl[MAX_BLOBS];    int nbl;   // racimos
    Deco   de[MAX_DECOS];    int nde;   // adornos
    float  height, half_width;          // tamaño de la copa (para el viento y las luciérnagas)
    int    fireflies;                   // cuántas luciérnagas vuelan alrededor
    // Tronco: hebras que se enroscan desde (0,0) hasta (tx,ty)
    float  tx, ty, bend, tr0, tr1, turns;
    int    strands;
} Tree;

static Tree trees[TREE_STAGES]; // un árbol ya generado por cada etapa

// Paletas de las hojas: tono oscuro, medio y claro de cada color
typedef struct { uint32_t dark, mid, light; } Pal3;
static const Pal3 leaf_pals[] = {
    { 0x1f6e4a, 0x2fa36a, 0x7fe0a0 }, // esmeralda
    { 0x1b5e6e, 0x2a9fb0, 0x7fe6e0 }, // turquesa
    { 0x2a3f86, 0x3f72cc, 0x92c0ff }, // azul
    { 0x4a2a78, 0x7a48c0, 0xc89cff }, // morado
    { 0x7a5a1a, 0xc89a2a, 0xffe07a }, // dorado
};
static const Pal3 wilted_pals[] = {   // hojas mustias
    { 0x6a5a2a, 0x9a8a3a, 0xc8b860 },
    { 0x5a4422, 0x8a6a3a, 0xb8925a },
};
static const Pal3 flash_pal = { 0xffffff, 0xffffff, 0xffffff }; // todo blanco

#define C_TREE_OUT 0x1a1420  // contorno
#define C_BARK_D   0x4a2c1c  // corteza en sombra
#define C_BARK     0x74472a  // corteza
#define C_BARK_L   0xa8703f  // corteza iluminada
#define C_VINE     0x4f8a3a  // musgo
#define C_VINE_L   0x8ac860
#define C_VINE_DRY 0x8a7a40  // musgo seco
#define C_GLOW     0xfff6b0  // luz
#define C_GLOW_D   0xe0a040  // halo de la luz
#define C_WHITE    0xffffff

// Cómo es el árbol en cada etapa
typedef struct {
    float trunk_len, trunk_r, branch_len; // largo y grosor del tronco, largo de las ramas
    int   depth;        // cuántas veces se dividen las ramas
    float blob_r;       // tamaño de los racimos
    int   jewel;        // 0 solo verdes, 1 algo de color, 2 todos los tonos joya
    int   vines, crystals, flowers, lights, fireflies; // cuántos adornos de cada tipo
    int   strands;      // hebras del tronco
    float turns;        // vueltas que dan al enroscarse
} StageParams;

static const StageParams params[TREE_STAGES] = {
    //  tronco r   rama prof racimo joya musgo crist flor luz luciér hebras vueltas
    {  9, 1.0f,  5, 0, 3.0f, 0,  0,  0,  0,  0, 0, 1, 0    }, // brote
    { 12, 1.7f,  7, 1, 4.0f, 0,  2,  0,  0,  0, 0, 1, 0    }, // arbolito
    { 15, 2.7f,  9, 2, 4.6f, 1,  6,  4,  0,  6, 0, 2, 0.8f }, // árbol
    { 16, 3.8f, 10, 2, 5.4f, 2, 10,  8, 14, 12, 7, 3, 1.0f }, // árbol mágico
};

// -----------------------------------------------------------------------------
// GENERACIÓN
// -----------------------------------------------------------------------------

static uint32_t seed;
static float rnd(float a, float b) { return rng_range(&seed, a, b); }

// Añade una rama curva. "bend" dice cuánto se dobla hacia un lado.
static void add_branch(Tree *tr, float x0, float y0, float x1, float y1, float bend, float r0, float r1) {
    if (tr->nbr >= MAX_BRANCHES) return;
    // El punto de control es el centro de la rama empujado hacia un lado
    // (en perpendicular a la rama). Cuanto más lejos, más curva.
    float dx = x1 - x0, dy = y1 - y0;
    float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    float px = -dy, py = dx; // dirección perpendicular
    float n = (px < 0 ? -px : px) + (py < 0 ? -py : py);
    if (n > 0) { px /= n; py /= n; } // la dejamos más o menos de longitud 1
    tr->br[tr->nbr++] = (Branch){ x0, y0, mx + px * bend, my + py * bend, x1, y1, r0, r1 };
}

// Elige el color de un racimo. En las primeras etapas casi todo es verde;
// en el árbol mágico aparecen azules, morados y dorados.
static int pick_palette(int jewel) {
    float r = rnd(0, 1);
    // 0 esmeralda, 1 turquesa, 2 azul, 3 morado, 4 dorado
    switch (jewel) {
    case 0:  return r < 0.7f ? 0 : 1;
    case 1:  return r < 0.45f ? 0 : r < 0.85f ? 1 : r < 0.95f ? 3 : 4;
    default: return r < 0.36f ? 0 : r < 0.6f ? 1 : r < 0.67f ? 2 : r < 0.83f ? 3 : 4;
    }
}

static void add_blob(Tree *tr, float x, float y, float r, int pal) {
    if (tr->nbl >= MAX_BLOBS) return;
    tr->bl[tr->nbl++] = (Blob){ x, y, r, pal };
}

// Un racimo completo: un círculo grande y 2 o 3 más pequeños alrededor,
// para que la copa no parezca hecha de bolas perfectas.
static void add_cluster(Tree *tr, const StageParams *p, float x, float y) {
    int pal = pick_palette(p->jewel);
    float r = p->blob_r * rnd(0.85f, 1.15f);
    add_blob(tr, x, y, r, pal);
    int sats = p->jewel ? 3 : 2;
    for (int i = 0; i < sats; i++) {
        float a = rnd(0, 2 * PI);                                  // dirección al azar
        int sp = rnd(0, 1) < 0.6f ? pal : pick_palette(p->jewel);  // casi siempre del mismo color
        add_blob(tr, x + m_cos(a) * r * 0.8f, y + m_sin(a) * r * 0.7f, r * rnd(0.55f, 0.75f), sp);
    }
}

// Hace crecer una rama desde (x,y) en la dirección "ang".
// Es RECURSIVA: se llama a sí misma para crear las dos ramas hijas, que a su
// vez crean sus hijas... hasta que "depth" llega a 0. En ese momento, en la
// punta, pone un racimo de hojas.
static void grow(Tree *tr, const StageParams *p, float x, float y, float ang, float len, float r, int depth) {
    float x1 = x + m_cos(ang) * len, y1 = y + m_sin(ang) * len; // punta de la rama
    add_branch(tr, x, y, x1, y1, rnd(-len * 0.25f, len * 0.25f), r, r * 0.65f);
    if (depth == 0) {
        add_cluster(tr, p, x1, y1);
        return;
    }
    int kids = 2;
    float spread = rnd(0.9f, 1.3f); // cuánto se abren las dos hijas
    for (int k = 0; k < kids; k++) {
        float a = ang + (k - 0.5f) * spread + rnd(-0.15f, 0.15f); // una a cada lado
        grow(tr, p, x1, y1, a, len * rnd(0.65f, 0.8f), r * 0.65f, depth - 1); // más cortas y finas
    }
}

// Fabrica el árbol de una etapa.
static void generate(Tree *tr, int stage) {
    const StageParams *p = &params[stage];
    seed = 0x9e3779b9u + stage * 7919u; // semilla distinta (pero fija) para cada etapa
    tr->nbr = tr->nbl = tr->nde = 0;

    // Tronco: de la base (0,0) hasta un punto un poco desplazado hacia arriba
    float tx = rnd(-2, 2), ty = -p->trunk_len;
    tr->tx = tx; tr->ty = ty;
    tr->bend = rnd(-2.5f, 2.5f);
    tr->tr0 = p->trunk_r; tr->tr1 = p->trunk_r * 0.7f;  // más fino arriba
    tr->strands = p->strands; tr->turns = p->turns;

    // Ramas principales
    float r = p->trunk_r * 0.7f;
    if (stage == 0) {
        // Brote: dos ramitas finas con una hoja cada una, en forma de V
        add_branch(tr, tx, ty, tx - 4, ty - 4,  1, 0.8f, 0.6f);
        add_branch(tr, tx, ty, tx + 4, ty - 6, -1, 0.8f, 0.6f);
        add_blob(tr, tx - 5, ty - 5, 2.6f, 0);
        add_blob(tr, tx + 5, ty - 7, 2.8f, 1);
    } else {
        // 2 o 3 ramas abiertas en abanico desde lo alto del tronco.
        // -PI/2 es "hacia arriba" (recuerda que la Y negativa es arriba).
        int mains = stage >= 2 ? 3 : 2;
        for (int i = 0; i < mains; i++) {
            float a = -PI / 2 + (i - (mains - 1) / 2.0f) * 0.85f + rnd(-0.1f, 0.1f);
            grow(tr, p, tx, ty, a, p->branch_len * rnd(0.9f, 1.1f), r, p->depth);
        }
        // Racimos en el centro para rellenar el hueco sobre el tronco
        add_cluster(tr, p, tx, ty - p->branch_len * 0.7f);
        add_cluster(tr, p, tx, ty - p->branch_len * 1.4f);
    }

    // Ordenar los racimos de abajo a arriba (ordenación por inserción).
    // Así los de arriba se pintan después y su borde oscuro queda encima de
    // los de abajo, lo que separa los volúmenes y da sensación de relieve.
    for (int i = 1; i < tr->nbl; i++) {
        Blob b = tr->bl[i];
        int j = i - 1;
        while (j >= 0 && tr->bl[j].y < b.y) { tr->bl[j + 1] = tr->bl[j]; j--; }
        tr->bl[j + 1] = b;
    }

    // Medir la copa: lo más alto y lo más ancho
    float top = 0, half = 0;
    for (int i = 0; i < tr->nbl; i++) {
        Blob *b = &tr->bl[i];
        if (b->y - b->r < top) top = b->y - b->r;
        float e = (b->x < 0 ? -b->x : b->x) + b->r;
        if (e > half) half = e;
    }
    tr->height = -top;
    tr->half_width = half;
    tr->fireflies = p->fireflies;

    // Repartir los adornos sobre racimos elegidos al azar.
    // ADD_DECO es una macro: un atajo para no repetir la misma línea muchas veces.
    #define ADD_DECO(...) if (tr->nde < MAX_DECOS) tr->de[tr->nde++] = (Deco){ __VA_ARGS__ }
    for (int i = 0; i < p->vines; i++) {
        // El musgo cuelga del borde de abajo de un racimo
        Blob *b = &tr->bl[rng_next(&seed) % tr->nbl];
        ADD_DECO(b->x + rnd(-0.6f, 0.6f) * b->r, b->y + b->r * 0.5f, rnd(4, 6 + stage * 3), D_VINE,
                 stage == 3 && rnd(0, 1) < 0.35f); // var=1: lleva farolillo
    }
    int counts[3] = { p->crystals, p->flowers, p->lights };
    int kinds[3]  = { D_CRYSTAL, D_FLOWER, D_LIGHT };
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < counts[k]; i++) {
            // En un punto al azar dentro de un racimo
            Blob *b = &tr->bl[rng_next(&seed) % tr->nbl];
            float a = rnd(0, 2 * PI), d = rnd(0, 0.75f) * b->r;
            ADD_DECO(b->x + m_cos(a) * d, b->y + m_sin(a) * d, 0, kinds[k], (int)(rng_next(&seed) % 3));
        }
    }
    #undef ADD_DECO
}

void tree_init(void) {
    for (int s = 0; s < TREE_STAGES; s++) generate(&trees[s], s);
}

// -----------------------------------------------------------------------------
// DIBUJO
// -----------------------------------------------------------------------------

static Framebuffer *fb_; // lienzo donde pintamos ahora mismo

// Pinta un píxel (si está dentro del lienzo).
static void put(int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= fb_->width || y >= fb_->height) return;
    fb_->pixels[y * fb_->width + x] = c;
}

// Pinta un círculo relleno: recorre el cuadrado que lo contiene y pinta los
// píxeles cuya distancia al centro es menor que el radio (dx² + dy² <= r²).
// El "+ r * 0.5" redondea un poco el borde para que quede más bonito en pixel art.
static void disk(float fx, float fy, float r, uint32_t c) {
    int cx = iround(fx), cy = iround(fy);
    int ir = (int)(r + 1);
    float rr = r * r + r * 0.5f;
    for (int dy = -ir; dy <= ir; dy++)
        for (int dx = -ir; dx <= ir; dx++)
            if (dx * dx + dy * dy <= rr) put(cx + dx, cy + dy, c);
}

// "Hash": convierte unas coordenadas en un número que parece aleatorio pero
// siempre es el mismo para las mismas coordenadas. Sirve para la textura de
// hojitas: así no cambia de un frame a otro (no "hierve").
static uint32_t hash(int x, int y, int s) {
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + s * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Datos del árbol que se está dibujando ahora (para no pasarlos a cada función)
static struct {
    const Tree *tr;
    int bx, by;      // posición en pantalla de la base del tronco
    float t, sway;   // tiempo y viento
    int style;
} D;

// Cuánto empuja el viento a un punto según su altura: nada en la base y el
// máximo en lo alto de la copa (k*k hace que crezca de forma suave).
static float sway_at(float y) {
    float k = -y / (D.tr->height > 1 ? D.tr->height : 1);
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return D.sway * k * k;
}

// Punto de la curva de una rama: t va de 0 (inicio) a 1 (final).
// Fórmula de Bézier cuadrática: mezcla el inicio, el control y el final.
static void bezier(const Branch *b, float t, float *x, float *y) {
    float u = 1 - t;
    *x = u * u * b->x0 + 2 * u * t * b->cx + t * t * b->x1;
    *y = u * u * b->y0 + 2 * u * t * b->cy + t * t * b->y1;
}

// Dibuja una rama poniendo muchos círculos seguidos a lo largo de la curva.
// Se llama tres veces ("pasadas"):
//   pass 0 = contorno oscuro, 1 = madera, 2 = luz (izquierda) y sombra (derecha)
// Primero se hace el contorno de TODAS las ramas, luego la madera de todas...
// así las ramas se funden entre sí sin líneas negras en las uniones.
static void draw_branch(const Branch *b, int pass) {
    float dx = b->x1 - b->x0, dy = b->y1 - b->y0;
    float approx = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy); // largo aproximado
    int steps = (int)(approx * 2) + 2;                         // un círculo cada medio píxel
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps, x, y;
        bezier(b, t, &x, &y);
        float r = b->r0 + (b->r1 - b->r0) * t;                 // grosor en este punto
        float px = D.bx + x + sway_at(y), py = D.by + y;
        if (pass == 0) disk(px, py, r + 1, C_TREE_OUT);
        else if (pass == 1) disk(px, py, r, D.style == TREE_FLASH ? C_WHITE : C_BARK);
        else if (D.style != TREE_FLASH && r >= 1.4f) {
            disk(px + r * 0.45f, py, r * 0.35f, C_BARK_D);
            disk(px - r * 0.45f, py, r * 0.3f, C_BARK_L);
        }
    }
}

// -----------------------------------------------------------------------------
// Tronco trenzado
// Cada hebra da vueltas alrededor del centro del tronco (como una espiral
// vista de lado). Para que parezca que se cruzan por delante y por detrás,
// el tronco se pinta por tramos; en cada tramo se mira qué hebra está más
// "al fondo" (depth) y se pintan de la del fondo a la de delante.
// -----------------------------------------------------------------------------

// Posición de la hebra s a la altura t (0 = base, 1 = arriba) y lo cerca que está.
static void trunk_point(const Tree *tr, int s, float t, float *x, float *y, float *depth) {
    float ang = 2 * PI * (tr->turns * t + (float)s / tr->strands); // cada hebra empieza desfasada
    float amp = tr->strands > 1 ? tr->tr0 * 0.55f : 0;              // cuánto se separa del centro
    *y = tr->ty * t;
    *x = tr->tx * t + m_sin(PI * t) * tr->bend + m_sin(ang) * amp + sway_at(*y);
    *depth = m_cos(ang); // > 0: hacia delante; < 0: hacia atrás
}

static void draw_trunk(void) {
    const Tree *tr = D.tr;
    int n = tr->strands;
    float rscale = n > 1 ? 0.62f : 1.0f;      // si hay varias hebras, cada una es más fina
    int steps = (int)(-tr->ty * 2) + 2;
    int chunks = n > 1 ? (int)(2 * n * tr->turns + 0.999f) : 1; // tramos en los que cambia el orden

    for (int c = 0; c < chunks; c++) {
        int i0 = steps * c / chunks, i1 = steps * (c + 1) / chunks;

        // Ordenar hebras de atrás hacia delante según su profundidad en este tramo
        int order[4] = { 0, 1, 2, 3 };
        float dep[4];
        float tmid = (i0 + i1) * 0.5f / steps;
        for (int s = 0; s < n; s++) { float x, y; trunk_point(tr, s, tmid, &x, &y, &dep[s]); }
        for (int a = 1; a < n; a++)
            for (int b = a; b > 0 && dep[order[b]] < dep[order[b - 1]]; b--) {
                int tmp = order[b]; order[b] = order[b - 1]; order[b - 1] = tmp;
            }

        // Pintar cada hebra del tramo con sus tres pasadas (contorno, madera, luz)
        for (int k = 0; k < n; k++) {
            int s = order[k];
            for (int pass = 0; pass < 3; pass++) {
                for (int i = i0; i <= i1; i++) {
                    float t = (float)i / steps, x, y, d;
                    trunk_point(tr, s, t, &x, &y, &d);
                    float r = (tr->tr0 + (tr->tr1 - tr->tr0) * t) * rscale;
                    float px = D.bx + x, py = D.by + y;
                    if (pass == 0) {
                        disk(px, py, r + 1, C_TREE_OUT);
                    } else if (pass == 1) {
                        // Las hebras del fondo, un poco más oscuras
                        uint32_t c2 = D.style == TREE_FLASH ? C_WHITE : (d < -0.2f ? 0x5e3822 : C_BARK);
                        disk(px, py, r, c2);
                    } else if (D.style != TREE_FLASH && r >= 1.2f) {
                        disk(px + r * 0.45f, py, r * 0.35f, C_BARK_D);
                        disk(px - r * 0.4f, py, r * 0.3f, C_BARK_L);
                    }
                }
            }
        }
    }
}

// Un racimo de hojas: un círculo sombreado píxel a píxel.
static void draw_blob(const Blob *b, int index) {
    const Pal3 *pal = D.style == TREE_FLASH  ? &flash_pal
                    : D.style == TREE_WILTED ? &wilted_pals[index % 2]
                    : &leaf_pals[b->pal];
    int cx = iround(D.bx + b->x + sway_at(b->y)), cy = iround(D.by + b->y);
    int ir = (int)(b->r + 1);
    float rr = b->r * b->r + b->r * 0.5f;
    for (int dy = -ir; dy <= ir; dy++) {
        for (int dx = -ir; dx <= ir; dx++) {
            float d2 = (float)(dx * dx + dy * dy);
            if (d2 > rr) continue; // fuera del círculo
            // "lit" mide lo iluminado que está el píxel: la luz viene de arriba
            // a la izquierda, así que arriba-izquierda > 0 y abajo-derecha < 0.
            float lit = (-dy * 0.75f - dx * 0.35f) / b->r;
            uint32_t h = hash(dx, dy, index) % 13; // un poco de azar para la textura
            uint32_t c;
            if (dy > 0 && d2 > (b->r - 1.3f) * (b->r - 1.3f)) c = pal->dark;  // borde de abajo, en sombra
            else if (lit > 0.35f) c = h < 3 ? pal->mid : pal->light;           // zona iluminada
            else if (lit < -0.3f) c = h < 2 ? pal->mid : pal->dark;            // zona en sombra
            else c = h == 0 ? pal->light : h == 1 ? pal->dark : pal->mid;      // centro con motitas
            put(cx + dx, cy + dy, c);
        }
    }
}

// Musgo que cuelga y se balancea; algunos llevan un farolillo al final.
static void draw_vine(const Deco *d, int index) {
    int x0 = iround(D.bx + d->x + sway_at(d->y)), y0 = iround(D.by + d->y);
    float wobble = m_sin(D.t * 1.8f + index * 1.3f) * 1.2f; // la punta se mueve más que el principio
    int len = (int)d->len;
    int wilted = D.style == TREE_WILTED;
    int x = x0;
    for (int i = 0; i <= len; i++) {
        x = x0 + iround(wobble * i / (len > 0 ? len : 1));
        put(x, y0 + i, wilted ? C_VINE_DRY : (i % 3 == 0 ? C_VINE_L : C_VINE));
        if (i % 3 == 1 && !wilted) put(x + ((i / 3) % 2 ? 1 : -1), y0 + i, C_VINE); // hojitas a los lados
    }
    if (d->var && !wilted) {
        // Farolillo colgante
        int y = y0 + len + 1;
        put(x, y, C_TREE_OUT);
        put(x - 1, y + 1, C_GLOW_D); put(x, y + 1, C_GLOW); put(x + 1, y + 1, C_GLOW_D);
        put(x - 1, y + 2, C_GLOW_D); put(x, y + 2, C_GLOW_D); put(x + 1, y + 2, C_GLOW_D);
    }
}

// Cristal en forma de rombo, con un destello de vez en cuando.
static void draw_crystal(int x, int y, int var, int index) {
    static const uint32_t edge[3] = { 0x2a6ab0, 0x6a3ab0, 0x2a8a8a };
    static const uint32_t body[3] = { 0x6fd8ff, 0xd8a0ff, 0x7ff0d0 };
    put(x, y - 2, edge[var]);
    put(x - 1, y - 1, edge[var]); put(x, y - 1, C_WHITE);   put(x + 1, y - 1, edge[var]);
    put(x - 1, y,     body[var]); put(x, y,     body[var]); put(x + 1, y,     edge[var]);
    put(x - 1, y + 1, edge[var]); put(x, y + 1, body[var]); put(x + 1, y + 1, edge[var]);
    put(x, y + 2, edge[var]);
    // Destello: solo cuando el seno pasa de 0.93 (un ratito de cada vuelta)
    if (m_sin(D.t * 2.3f + index * 2.1f) > 0.93f) {
        put(x, y - 4, C_WHITE); put(x, y - 3, C_WHITE);
        put(x - 2, y - 1, C_WHITE); put(x + 2, y - 1, C_WHITE);
    }
}

// Florecilla en forma de cruz con el centro amarillo.
static void draw_flower(int x, int y, int var) {
    static const uint32_t petal[3] = { 0xff7ab8, 0xd890ff, 0xffd0e8 };
    static const uint32_t petal_d[3] = { 0xc04a88, 0x9a5ad0, 0xd890b0 };
    put(x, y - 1, petal[var]);
    put(x - 1, y, petal[var]); put(x, y, 0xffe07a); put(x + 1, y, petal_d[var]);
    put(x, y + 1, petal_d[var]);
}

// Lucecita que se enciende y apaga suavemente (sigue a un seno).
static void draw_light(int x, int y, int index) {
    float s = m_sin(D.t * 3 + index * 2.3f);
    if (s > 0) {
        put(x, y, C_GLOW);
        if (s > 0.5f) { // muy encendida: con halo en cruz
            put(x - 1, y, C_GLOW_D); put(x + 1, y, C_GLOW_D);
            put(x, y - 1, C_GLOW_D); put(x, y + 1, C_GLOW_D);
        }
    } else {
        put(x, y, C_GLOW_D);
    }
}

// Dibuja el árbol entero, de atrás hacia delante.
void tree_draw(Framebuffer *fb, int stage, int base_x, int base_y, float t, float sway, int style) {
    fb_ = fb;
    D.tr = &trees[stage];
    D.bx = base_x; D.by = base_y;
    D.t = t; D.sway = sway; D.style = style;
    const Tree *tr = D.tr;

    // 1) Ramas (contorno de todas primero para que se fundan) y luego el tronco
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < tr->nbr; i++) draw_branch(&tr->br[i], pass);
    draw_trunk();

    // 2) Copa: primero un contorno común a todos los racimos...
    for (int i = 0; i < tr->nbl; i++) {
        const Blob *b = &tr->bl[i];
        disk(D.bx + b->x + sway_at(b->y), D.by + b->y, b->r + 1, C_TREE_OUT);
    }
    // ...y después cada racimo con su sombreado
    for (int i = 0; i < tr->nbl; i++) draw_blob(&tr->bl[i], i);

    // 3) Adornos
    for (int i = 0; i < tr->nde; i++) {
        const Deco *d = &tr->de[i];
        if (d->kind == D_VINE) { draw_vine(d, i); continue; }
        if (style != TREE_NORMAL) continue; // mustio o destello: sin adornos
        int x = iround(D.bx + d->x + sway_at(d->y)), y = iround(D.by + d->y);
        switch (d->kind) {
            case D_CRYSTAL: draw_crystal(x, y, d->var, i); break;
            case D_FLOWER:  draw_flower(x, y, d->var); break;
            case D_LIGHT:   draw_light(x, y, i); break;
        }
    }

    // 4) Luciérnagas: se mueven en curvas suaves (senos) alrededor de la copa
    if (style == TREE_NORMAL) {
        for (int i = 0; i < tr->fireflies; i++) {
            float fx = m_sin(t * (0.35f + i * 0.07f) + i * 1.7f) * (tr->half_width + 6);
            float fy = -tr->height * 0.55f + m_sin(t * (0.6f + i * 0.05f) + i * 2.9f) * tr->height * 0.45f;
            draw_light(iround(base_x + fx), iround(base_y + fy), i + 50);
        }
    }
}
