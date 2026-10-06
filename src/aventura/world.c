// =============================================================================
// world.c  —  El valle: terreno, vegetación, ruinas y choques
// =============================================================================
//
// EL TERRENO es una REJILLA de alturas: imagina una cuadrícula de 96 x 96
// casillas vista desde arriba; en cada esquina guardamos a qué altura está el
// suelo. Cada casilla se dibuja con dos triángulos. La altura sale de una
// fórmula con senos (colinas suaves) más tres retoques: una explanada en el
// centro para las ruinas, un hoyo para el lago y paredes en los bordes.
//
// LOS PROPS (árboles, rocas, flores...) se colocan al azar pero con semilla
// fija, así el bosque es siempre el mismo. Se evitan las ruinas, el lago, la
// salida del jugador y que se pisen unos a otros.
//
// LOS CHOQUES son sencillos: cada prop es un círculo en el suelo. Si el
// héroe se mete dentro, se le empuja hacia fuera.
// =============================================================================

#include "world.h"

#define N      96              // casillas por lado
#define CELL   0.75f           // tamaño de cada casilla en metros
#define ORIGIN (-N * CELL / 2) // esquina del mapa (el centro del mapa es 0,0)

#define MAX_PROPS 512

static float hgrid[N + 1][N + 1]; // altura de cada esquina de la rejilla
static Prop  props[MAX_PROPS];
static int   nprops;

// Un modelo por cada tipo de prop (las flores tienen 4 colores)
enum { M_TREE, M_TREE2, M_PINE, M_ROCK, M_BUSH, M_FLOWER0, M_PILLAR = M_FLOWER0 + 4, M_PILLAR_BROKEN, M_PEDESTAL, M_COUNT };
static Mesh meshes[M_COUNT];

static uint32_t seed;
static float rnd(float a, float b) { return rng_range(&seed, a, b); }

// -----------------------------------------------------------------------------
// TERRENO
// -----------------------------------------------------------------------------

// La "receta" de la forma del valle: altura en cualquier punto (x, z).
static float shape_height(float x, float z) {
    // Colinas: varias ondas de distinto tamaño sumadas
    float h = 0.9f * m_sin(x * 0.17f) * m_cos(z * 0.15f)
            + 0.6f * m_sin(x * 0.07f + 1.3f) * m_sin(z * 0.09f + 0.4f)
            + 0.3f * m_sin((x + z) * 0.31f)
            + 0.08f * m_sin(x * 0.9f + 0.5f) * m_sin(z * 0.8f);

    // Explanada plana para las ruinas del centro.
    // d2 = distancia al centro al cuadrado. Hasta 8 m (64) es totalmente
    // plano; de 8 a 12 m (144) se mezcla poco a poco con las colinas.
    float d2 = x * x + z * z;
    float flat = d2 < 64 ? 1 : d2 < 144 ? 1 - (d2 - 64) / 80 : 0;
    h = h * (1 - flat) + 0.2f * flat;

    // Lago: un hoyo redondo de 9 m de radio centrado en (17, -13)
    float lx = x - 17, lz = z + 13;
    float l2 = (lx * lx + lz * lz) / 81;   // 0 en el centro, 1 en el borde
    if (l2 < 1) h -= 2.6f * (1 - l2) * (1 - l2);

    // Acantilados que cierran el valle: a partir de 28 m del centro el
    // suelo sube cada vez más deprisa (crece con la distancia al cuadrado)
    float e = m_abs(x) > m_abs(z) ? m_abs(x) : m_abs(z);
    if (e > 28) h += (e - 28) * (e - 28) * 0.35f;
    return h;
}

// Altura exacta del suelo en (x, z), calculada sobre los mismos triángulos
// que se dibujan. Así los pies del héroe siempre tocan el suelo que se ve.
float world_height(float x, float z) {
    // ¿En qué casilla estamos? fx, fz = posición en "casillas" (con decimales)
    float fx = (x - ORIGIN) / CELL, fz = (z - ORIGIN) / CELL;
    if (fx < 0) fx = 0;
    if (fz < 0) fz = 0;
    if (fx > N - 0.001f) fx = N - 0.001f;
    if (fz > N - 0.001f) fz = N - 0.001f;
    int i = (int)fx, j = (int)fz;          // casilla
    float u = fx - i, v = fz - j;          // posición dentro de la casilla (0 a 1)
    float h00 = hgrid[i][j], h10 = hgrid[i + 1][j], h01 = hgrid[i][j + 1], h11 = hgrid[i + 1][j + 1];
    // La casilla está partida en dos triángulos por la diagonal; según de qué
    // lado estemos, mezclamos las alturas de sus tres esquinas.
    if (u >= v) return h00 + u * (h10 - h00) + v * (h11 - h10);
    return h00 + v * (h01 - h00) + u * (h11 - h01);
}

// Normal del suelo: miramos la altura un poco a cada lado para saber la cuesta.
V3 world_normal(float x, float z) {
    float e = 0.3f;
    float hx = world_height(x + e, z) - world_height(x - e, z);
    float hz = world_height(x, z + e) - world_height(x, z - e);
    return v_norm(v3(-hx, 2 * e, -hz));
}

// Se puede pisar si no es acantilado ni la parte honda del lago.
int world_walkable(float x, float z) {
    if (m_abs(x) > WORLD_LIM || m_abs(z) > WORLD_LIM) return 0;
    return world_height(x, z) > WATER_Y - 0.15f;
}

int   world_grid_n(void)                { return N; }
float world_grid_cell(void)             { return CELL; }
float world_grid_origin(void)           { return ORIGIN; }
float world_grid_height(int i, int j)   { return hgrid[i][j]; }

// -----------------------------------------------------------------------------
// MODELOS DE LA VEGETACIÓN Y LAS RUINAS
// (medidas en metros; el origen de cada modelo es el punto donde toca el suelo)
// -----------------------------------------------------------------------------

static void build_meshes(void) {
    for (int k = 0; k < 2; k++) { // árbol redondo de copa frondosa, dos tonos de verde
        uint32_t c1 = k ? 0x3a8f3c : 0x5a9e2e, c2 = k ? 0x52ad4a : 0x86bf3c, c3 = k ? 0x2f7a35 : 0x4a8a28;
        mesh_begin();
        add_cyl(0, 0, 0, 0.32f, 0.18f, 1.9f, 8, 0x6e4a2e);                 // tronco
        mesh_xform(m_mul(m_translate(0, 1.4f, 0), m_rot_z(0.7f)));
        add_cyl(0, 0, 0, 0.12f, 0.07f, 0.9f, 6, 0x6e4a2e);                 // rama inclinada
        mesh_xform(m_identity());
        add_ellipsoid(0, 2.55f, 0, 1.45f, 1.15f, 1.45f, 14, 9, c1);         // copa: varias esferas
        add_ellipsoid(0.7f, 3.15f, 0.3f, 0.9f, 0.8f, 0.9f, 12, 8, c2);
        add_ellipsoid(-0.75f, 2.9f, -0.35f, 0.85f, 0.75f, 0.85f, 12, 8, c2);
        add_ellipsoid(0.15f, 3.55f, -0.45f, 0.75f, 0.65f, 0.75f, 12, 8, c2);
        add_ellipsoid(-0.3f, 2.1f, 0.8f, 0.8f, 0.6f, 0.7f, 12, 8, c3);
        meshes[k == 0 ? M_TREE : M_TREE2] = mesh_end();
    }

    mesh_begin(); // pino: tronco y cuatro conos apilados
    add_cyl(0, 0, 0, 0.25f, 0.16f, 1.0f, 8, 0x5e3e24);
    add_cyl(0, 0.7f, 0, 1.3f, 0, 1.7f, 9, 0x2a6e42);
    add_cyl(0, 1.5f, 0, 1.05f, 0, 1.55f, 9, 0x307a48);
    add_cyl(0, 2.3f, 0, 0.8f, 0, 1.4f, 9, 0x37874f);
    add_cyl(0, 3.0f, 0, 0.5f, 0, 1.1f, 9, 0x3e9457);
    meshes[M_PINE] = mesh_end();

    mesh_begin(); // roca facetada (esferas sin suavizar) con un poco de musgo
    mesh_smooth(0);
    add_ellipsoid(0, 0.3f, 0, 0.85f, 0.6f, 0.75f, 7, 4, 0x8e8c92);
    add_ellipsoid(0.45f, 0.2f, 0.35f, 0.45f, 0.35f, 0.4f, 6, 3, 0x7c7a80);
    add_ellipsoid(0.1f, 0.62f, -0.1f, 0.25f, 0.06f, 0.25f, 6, 3, 0x5a9a48); // musgo
    meshes[M_ROCK] = mesh_end();

    mesh_begin(); // arbusto con bayas
    add_ellipsoid(0, 0.4f, 0, 0.62f, 0.46f, 0.62f, 12, 8, 0x45a83e);
    add_ellipsoid(0.28f, 0.62f, 0.1f, 0.36f, 0.3f, 0.36f, 10, 6, 0x62c050);
    add_ellipsoid(-0.22f, 0.58f, -0.15f, 0.32f, 0.26f, 0.32f, 10, 6, 0x56b848);
    add_ellipsoid(0.05f, 0.45f, 0.4f, 0.25f, 0.2f, 0.25f, 8, 5, 0xe84a5a); // bayas
    meshes[M_BUSH] = mesh_end();

    static const uint32_t petals[4] = { 0xf05060, 0xf0d040, 0xf4f0f0, 0xb070f0 };
    for (int k = 0; k < 4; k++) { // florecillas de 4 colores: tallo + pétalos + centro
        mesh_begin();
        add_cyl(0, 0, 0, 0.025f, 0.02f, 0.3f, 4, 0x3f8a30);
        add_gem(0, 0.32f, 0, 0.15f, 0.05f, 6, petals[k]);
        add_ellipsoid(0, 0.35f, 0, 0.045f, 0.04f, 0.045f, 6, 4, 0xf8e070);
        meshes[M_FLOWER0 + k] = mesh_end();
    }

    mesh_begin(); // columna con hiedra
    add_box(0, 0.15f, 0, 1.1f, 0.3f, 1.1f, 0xb8b09a);         // base
    add_cyl(0, 0.3f, 0, 0.42f, 0.38f, 2.8f, 10, 0xcac2aa);    // fuste
    add_box(0, 3.2f, 0, 1.1f, 0.35f, 1.1f, 0xb8b09a);         // capitel
    mesh_xform(m_translate(0.3f, 1.6f, 0.36f));
    add_ellipsoid(0, 0, 0, 0.2f, 0.5f, 0.12f, 8, 5, 0x4f9a40);   // hiedra
    mesh_xform(m_translate(-0.25f, 0.9f, 0.38f));
    add_ellipsoid(0, 0, 0, 0.22f, 0.45f, 0.12f, 8, 5, 0x5aa848);
    meshes[M_PILLAR] = mesh_end();

    mesh_begin(); // columna rota con un trozo caído en el suelo
    add_box(0, 0.15f, 0, 1.1f, 0.3f, 1.1f, 0xb8b09a);
    add_cyl(0, 0.3f, 0, 0.42f, 0.4f, 1.1f, 10, 0xcac2aa);
    mesh_xform(m_mul(m_translate(0.9f, 0.25f, 0.4f), m_rot_z(1.4f)));
    add_cyl(0, 0, 0, 0.4f, 0.38f, 1.3f, 10, 0xbcb49c); // trozo caído (tumbado con rot_z)
    meshes[M_PILLAR_BROKEN] = mesh_end();

    mesh_begin(); // pedestal central donde aparece el cofre
    add_box(0, 0.25f, 0, 2.4f, 0.5f, 2.4f, 0xb0a890);
    add_box(0, 0.65f, 0, 1.7f, 0.3f, 1.7f, 0xc4bca4);
    add_box(0, 0.25f, 1.21f, 0.6f, 0.2f, 0.02f, 0x6a8ab0); // inscripción
    meshes[M_PEDESTAL] = mesh_end();
}

int world_mesh_count(void) { return M_COUNT; }
const Mesh *world_mesh(int i) { return &meshes[i]; }

int world_prop_mesh(const Prop *p) {
    switch (p->kind) {
        case PROP_TREE:          return M_TREE;
        case PROP_TREE2:         return M_TREE2;
        case PROP_PINE:          return M_PINE;
        case PROP_ROCK:          return M_ROCK;
        case PROP_BUSH:          return M_BUSH;
        case PROP_FLOWER:        return M_FLOWER0 + p->var;
        case PROP_PILLAR:        return M_PILLAR;
        case PROP_PILLAR_BROKEN: return M_PILLAR_BROKEN;
        default:                 return M_PEDESTAL;
    }
}

int world_prop_count(void) { return nprops; }
const Prop *world_prop(int i) { return &props[i]; }

// Dónde se dibuja un prop: en su sitio, a la altura del suelo, girado y escalado.
// Se hunde 10 cm (menos las flores) para que no parezca que flota en las cuestas.
M34 world_prop_matrix(const Prop *p) {
    float y = world_height(p->x, p->z) - (p->kind == PROP_FLOWER ? 0 : 0.1f);
    return m_mul(m_translate(p->x, y, p->z), m_mul(m_rot_y(p->yaw), m_scale(p->scale, p->scale, p->scale)));
}

// -----------------------------------------------------------------------------
// COLOCACIÓN DE LOS PROPS
// -----------------------------------------------------------------------------

static void add_prop(int kind, float x, float z, float r, float scale) {
    if (nprops >= MAX_PROPS) return;
    Prop *p = &props[nprops++];
    p->kind = kind; p->x = x; p->z = z; p->r = r * scale;
    p->scale = scale; p->yaw = rnd(0, 2 * PI);
    p->var = (int)(rng_next(&seed) % 4);
    p->alive = 1;
}

// ¿Se puede poner algo de radio r en (x, z)?
static int spot_free(float x, float z, float r) {
    if (!world_walkable(x, z) || world_height(x, z) < WATER_Y + 0.35f) return 0; // ni agua ni acantilado
    if (x * x + z * z < 10 * 10) return 0;                // ni en las ruinas
    if (x * x + (z - 11) * (z - 11) < 4 * 4) return 0;    // ni donde aparece el jugador
    for (int i = 0; i < nprops; i++) {                    // ni encima de otro prop
        float dx = props[i].x - x, dz = props[i].z - z;
        float m = props[i].r + r + 0.8f;
        if (dx * dx + dz * dz < m * m) return 0;
    }
    return 1;
}

// Reparte "count" props de un tipo probando sitios al azar hasta que quepan.
static void scatter(int kind, int count, float r, float smin, float smax) {
    for (int n = 0, tries = 0; n < count && tries < count * 60; tries++) {
        float x = rnd(-27.5f, 27.5f), z = rnd(-27.5f, 27.5f), s = rnd(smin, smax);
        if (!spot_free(x, z, r * s)) continue;
        add_prop(kind, x, z, r, s);
        n++;
    }
}

void world_init(void) {
    // 1) Calcular la altura de cada esquina de la rejilla
    for (int i = 0; i <= N; i++)
        for (int j = 0; j <= N; j++)
            hgrid[i][j] = shape_height(ORIGIN + i * CELL, ORIGIN + j * CELL);

    // 2) Construir los modelos
    build_meshes();

    // 3) Colocar las ruinas: el pedestal en el centro y 7 columnas en círculo
    seed = 0x51ed270bu;
    nprops = 0;
    add_prop(PROP_PEDESTAL, 0, 0, 1.3f, 1);
    props[0].yaw = 0;
    for (int k = 0; k < 7; k++) { // una de cada tres, rota
        float a = 2 * PI * k / 7 + 0.3f;
        add_prop(k % 3 == 1 ? PROP_PILLAR_BROKEN : PROP_PILLAR, m_cos(a) * 6.5f, m_sin(a) * 6.5f, 0.55f, 1);
        props[nprops - 1].yaw = -a; // la hiedra mira hacia fuera
    }

    // 4) La vegetación: tipo, cantidad, radio de choque y tamaños mínimo/máximo
    scatter(PROP_TREE,  26, 0.45f, 0.8f, 1.35f);
    scatter(PROP_TREE2, 16, 0.45f, 0.8f, 1.25f);
    scatter(PROP_PINE,  22, 0.4f,  0.9f, 1.5f);
    scatter(PROP_ROCK,  18, 0.75f, 0.6f, 1.6f);
    scatter(PROP_BUSH,  32, 0.5f,  0.85f, 1.15f);
    // Flores: sin choque, pueden ir casi donde sea
    for (int n = 0; n < 220; n++) {
        float x = rnd(-27.5f, 27.5f), z = rnd(-27.5f, 27.5f);
        if (!world_walkable(x, z) || world_height(x, z) < WATER_Y + 0.3f || x * x + z * z < 8 * 8) continue;
        add_prop(PROP_FLOWER, x, z, 0, rnd(0.8f, 1.3f));
    }
}

void world_reset(void) {
    for (int i = 0; i < nprops; i++) props[i].alive = 1;
}

float world_pedestal_top(void) { return world_height(0, 0) + 0.8f; }

// Choques: si el círculo (x, z, radius) se mete dentro del círculo de algún
// prop, lo sacamos empujándolo en línea recta desde el centro del prop.
int world_collide(float *x, float *z, float radius) {
    int hit = 0;
    for (int i = 0; i < nprops; i++) {
        Prop *p = &props[i];
        if (!p->alive || p->r <= 0) continue;
        float dx = *x - p->x, dz = *z - p->z;
        float m = p->r + radius, d2 = dx * dx + dz * dz;
        if (d2 >= m * m) continue;            // no se tocan
        float d = m_sqrt(d2);
        if (d < 0.001f) { dx = 1; dz = 0; d = 1; } // justo en el centro: empujar hacia cualquier lado
        *x = p->x + dx / d * m;               // colocarlo justo en el borde
        *z = p->z + dz / d * m;
        hit = 1;
    }
    return hit;
}

// Corta los arbustos que haya dentro del círculo (x, z, radius).
// Guarda en "cut" dónde estaban (para soltar hojas y premios ahí).
int world_cut_bushes(float x, float z, float radius, V3 *cut, int max) {
    int n = 0;
    for (int i = 0; i < nprops && n < max; i++) {
        Prop *p = &props[i];
        if (!p->alive || p->kind != PROP_BUSH) continue;
        float dx = p->x - x, dz = p->z - z, m = radius + p->r;
        if (dx * dx + dz * dz > m * m) continue;
        p->alive = 0;
        cut[n++] = v3(p->x, world_height(p->x, p->z), p->z);
    }
    return n;
}
