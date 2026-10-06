// =============================================================================
// aventura.c  —  "La Leyenda del Bosque": la lógica del juego
// =============================================================================
//
// CÓMO SE JUEGA
//   Mueves al héroe por un valle (WASD), giras la cámara (Q/E o flechas) y
//   atacas con la espada (J o Espacio). Hay 8 limos repartidos por el valle;
//   al derrotarlos a todos aparece un cofre en las ruinas del centro.
//
// CÓMO ESTÁ ORGANIZADO EL JUEGO (cada archivo hace una cosa)
//   main_gl.c  -> ventana, teclado y bucle principal
//   world.c    -> el escenario: terreno, árboles, choques
//   mesh.c     -> construir modelos 3D con formas simples
//   gfx.c      -> dibujarlo todo con la tarjeta gráfica (shaders)
//   aventura.c -> ESTE: el héroe, los enemigos, el combate, la cámara y la interfaz
//
// CÓMO ESTÁ ORGANIZADO ESTE ARCHIVO
//   1. Ajustes y datos (estructuras del héroe, enemigos, objetos...)
//   2. Modelos 3D del héroe, limos, gemas, cofre
//   3. Partículas y ayudantes
//   4. Empezar / reiniciar partida
//   5. Animación del héroe (esqueleto)
//   6. Combate
//   7. Actualización de cada frame (game_update)
//   8. Dibujo 3D y de la interfaz (game_render_gl)
//
// Recuerda: X = derecha, Y = arriba, Z = adelante, 1 unidad = 1 metro.
// =============================================================================

#include "../game.h"
#include "../render.h"
#include "math3d.h"
#include "gfx.h"
#include "mesh.h"
#include "world.h"

const char *game_title = "La Leyenda del Bosque";

#define W SCREEN_W
#define H SCREEN_H

// -----------------------------------------------------------------------------
// 1. AJUSTES Y DATOS
// Cambiando estos números cambias cómo se juega.
// -----------------------------------------------------------------------------
#define PLAYER_SPEED 5.5f   // metros por segundo al correr
#define PLAYER_R     0.4f   // radio del héroe para chocar con las cosas
#define ATTACK_TIME  0.32f  // segundos que dura un espadazo
#define SWORD_REACH  2.4f   // hasta dónde llega la espada
#define MAX_HP       6      // vida máxima, en medios corazones (6 = 3 corazones)
#define NUM_ENEMIES  8      // limos en el valle
#define MAX_PICKUPS  48     // gemas y corazones que puede haber a la vez en el suelo
#define MAX_PARTS    800    // partículas a la vez
#ifndef CAM_DIST
#define CAM_DIST     6.5f   // distancia de la cámara al héroe
#endif
#ifndef CAM_HEIGHT
#define CAM_HEIGHT   2.4f   // altura de la cámara sobre la cabeza del héroe
#endif
#define CHEST_REACH  3.2f   // distancia al centro para poder abrir el cofre

// Estados de la partida (en qué "pantalla" estamos)
enum { ST_TITLE, ST_PLAY, ST_DEAD, ST_WIN }; // título, jugando, muerto, victoria
// Tipos de objetos que se recogen
enum { PK_GEM_G, PK_GEM_B, PK_HEART };       // gema verde (1), gema azul (5), corazón

// El héroe
typedef struct {
    V3 pos;              // posición de los pies
    float yaw, walk;     // hacia dónde mira, y "reloj" del paso al correr
    int moving, hp, gems;
    float run;           // 0 quieto .. 1 corriendo (suavizado, para mezclar animaciones)
    float turn;          // velocidad de giro (para que el gorro ondee)
    float cap_lift, cap_side;
    float blink_t, blink;
    float attack_t;      // >0 mientras dura el espadazo
    int hit_done;
    float inv_t;         // invulnerable tras recibir daño
    float kx, kz;        // empuje
} Player;

// Un limo
typedef struct {
    V3 pos, home;              // dónde está y alrededor de dónde pasea
    float vx, vy, vz, yaw;     // velocidad y hacia dónde mira
    float hop_t;               // tiempo hasta el próximo salto
    float hit_t;               // > 0 justo después de recibir un golpe
    float squash;              // aplastado (> 0) o estirado (< 0), para la animación
    int hp, alive, blue;       // vida, si sigue vivo, y si es azul (más fuerte)
} Enemy;

// Objeto que se recoge (gema o corazón)
typedef struct { V3 pos; float vy, t; int kind, alive; } Pickup;

// Partícula: punto de luz o trocito que vuela un rato y desaparece
typedef struct {
    V3 pos, vel;               // posición y velocidad
    float life, size;          // segundos que le quedan y tamaño
    uint32_t color;
    int gravity, glow;         // si cae, y si brilla (suma luz)
} Part;

// Todo el estado del juego en una sola variable "G"
static struct {
    int state;
    float t, state_t;
    Player p;
    Enemy en[NUM_ENEMIES];
    Pickup pk[MAX_PICKUPS];
    Part parts[MAX_PARTS];
    float cam_yaw;               // hacia dónde mira la cámara
    V3 cam;                      // posición de la cámara
    int chest_active, chest_open;
    float chest_appear, chest_t; // animaciones del cofre (aparecer y abrirse)
    const char *msg;             // mensaje de abajo
    float msg_t, hurt_flash, mote_acc; // tiempo del mensaje, destello rojo, reloj del polen
} G;

// Piezas del héroe: cada una gira alrededor de su articulación (origen del modelo)
enum { HP_TORSO, HP_HEAD, HP_EYE, HP_CAP1, HP_CAP2, HP_UPPER_ARM, HP_FOREARM_R, HP_FOREARM_L,
       HP_SWORD, HP_THIGH, HP_SHIN, HP_COUNT };
static Mesh m_hero[HP_COUNT];
static Mesh m_slime[2], m_gem[2], m_heart, m_chest, m_lid;

// Números de los modelos ya subidos a la tarjeta gráfica
static int id_hero[HP_COUNT], id_slime[2], id_gem[2], id_heart, id_chest, id_lid;
static int id_props[32];

// La interfaz (corazones, textos) se dibuja en pixel art en este lienzo de
// 320x180 y luego gfx.c lo pone encima de la imagen 3D.
static uint32_t hud_pixels[W * H];
static Framebuffer hud = { hud_pixels, W, H };

static uint32_t seed = 0x1234567u;
static float rnd(float a, float b) { return rng_range(&seed, a, b); }
static float minf(float a, float b) { return a < b ? a : b; }

// -----------------------------------------------------------------------------
// 2. MODELOS 3D
// -----------------------------------------------------------------------------

// El héroe está hecho por PIEZAS separadas (torso, cabeza, brazos...). Cada
// pieza tiene su origen (0,0,0) en su ARTICULACIÓN: el brazo en el hombro,
// el antebrazo en el codo, la pierna en la rodilla... Así, para animar basta
// con girar cada pieza alrededor de su origen (ver hero_pose).
//
// El héroe por piezas. Medidas en metros; mira hacia +Z, con X a su derecha.
static void build_hero(void) {
    const uint32_t tunic = 0x3c9a46, tunic_d = 0x2c7a3a, skin = 0xf5cfa8, hair = 0xf0c850;
    const uint32_t tights = 0xece6d6, leather = 0x6a4424, leather_d = 0x3e2614, gold = 0xe8c040;

    mesh_begin(); // torso (articulación: la cadera)
    add_ellipsoid(0, 0.24f, 0, 0.24f, 0.27f, 0.17f, 16, 10, tunic);
    add_cyl(0, -0.13f, 0, 0.31f, 0.22f, 0.24f, 16, tunic_d);          // falda de la túnica
    add_cyl(0, 0.04f, 0, 0.235f, 0.235f, 0.07f, 16, 0x5a3a22);        // cinturón
    add_box(0, 0.075f, 0.23f, 0.09f, 0.075f, 0.03f, gold);            // hebilla
    add_ellipsoid(0, 0.47f, 0, 0.11f, 0.05f, 0.1f, 10, 5, tights);    // cuello de la camisa
    mesh_xform(m_mul(m_translate(0, 0.27f, 0.155f), m_rot_z(0.75f)));
    add_box(0, 0, 0, 0.055f, 0.56f, 0.025f, 0x5a3a22);               // correa cruzada
    mesh_xform(m_mul(m_translate(0.04f, 0.22f, -0.18f), m_rot_z(-0.65f)));
    add_box(0, 0, 0, 0.08f, 0.5f, 0.05f, 0x2a3a6a);                  // vaina a la espalda
    add_box(0, 0.25f, 0, 0.09f, 0.03f, 0.06f, gold);
    m_hero[HP_TORSO] = mesh_end();

    mesh_begin(); // cabeza (articulación: el cuello)
    add_ellipsoid(0, 0.22f, 0.02f, 0.25f, 0.26f, 0.24f, 18, 12, skin);
    add_ellipsoid(0, 0.165f, 0.255f, 0.03f, 0.035f, 0.03f, 8, 5, 0xeab890);  // nariz
    add_ellipsoid(0, 0.32f, -0.03f, 0.27f, 0.18f, 0.26f, 16, 8, hair);       // pelo
    for (int i = -1; i <= 1; i++) {                                          // flequillo
        mesh_xform(m_mul(m_translate(i * 0.11f, 0.39f, 0.16f), m_mul(m_rot_z(-i * 0.25f), m_rot_x(2.6f))));
        add_cyl(0, 0, 0, 0.075f, 0, 0.17f, 8, hair);
    }
    for (int i = -1; i <= 1; i += 2) {
        mesh_xform(m_mul(m_translate(i * 0.21f, 0.3f, 0.07f), m_rot_x(2.9f)));  // patillas
        add_cyl(0, 0, 0, 0.055f, 0, 0.2f, 6, hair);
        mesh_xform(m_mul(m_translate(i * 0.23f, 0.22f, -0.02f), m_mul(m_rot_z(-i * 1.3f), m_rot_x(-0.4f))));
        add_cyl(0, 0, 0, 0.065f, 0, 0.24f, 8, skin);                        // orejas de elfo
        mesh_xform(m_mul(m_translate(i * 0.1f, 0.335f, 0.235f), m_rot_z(i * 0.15f)));
        add_box(0, 0, 0, 0.1f, 0.024f, 0.03f, 0xb8862a);                    // cejas
    }
    m_hero[HP_HEAD] = mesh_end();

    mesh_begin(); // ojo (se encoge en vertical para parpadear)
    add_ellipsoid(0, 0, 0, 0.062f, 0.082f, 0.03f, 12, 8, 0xffffff);
    add_ellipsoid(0, -0.01f, 0.02f, 0.04f, 0.056f, 0.018f, 12, 8, 0x1e3a8a);
    add_ellipsoid(0, -0.012f, 0.03f, 0.022f, 0.032f, 0.012f, 10, 6, 0x0a1430);  // pupila
    add_ellipsoid(-0.014f, 0.016f, 0.038f, 0.012f, 0.015f, 0.006f, 6, 4, 0xffffff); // brillo
    m_hero[HP_EYE] = mesh_end();

    mesh_begin(); // gorro, primer tramo (sale de la cabeza hacia atrás)
    add_cyl(0, -0.02f, 0, 0.275f, 0.17f, 0.28f, 16, tunic);
    m_hero[HP_CAP1] = mesh_end();
    mesh_begin(); // gorro, punta
    add_cyl(0, -0.01f, 0, 0.17f, 0, 0.38f, 14, tunic);
    m_hero[HP_CAP2] = mesh_end();

    mesh_begin(); // brazo (articulación: el hombro)
    add_ellipsoid(0, -0.1f, 0, 0.08f, 0.14f, 0.08f, 10, 7, tunic);
    m_hero[HP_UPPER_ARM] = mesh_end();

    for (int side = 0; side < 2; side++) { // antebrazos (articulación: el codo)
        mesh_begin();
        add_ellipsoid(0, -0.08f, 0, 0.06f, 0.1f, 0.06f, 10, 6, skin);
        add_cyl(0, -0.2f, 0, 0.072f, 0.066f, 0.1f, 10, 0x7a5232);         // brazalete
        add_ellipsoid(0, -0.25f, 0.01f, 0.068f, 0.062f, 0.07f, 10, 6, skin); // puño
        if (side == 1) { // el izquierdo lleva el escudo
            mesh_xform(m_translate(-0.085f, -0.13f, 0.03f));
            add_ellipsoid(0.008f, 0, 0, 0.03f, 0.28f, 0.23f, 16, 10, 0xb8c0cc); // borde
            add_ellipsoid(-0.006f, 0, 0, 0.032f, 0.255f, 0.205f, 16, 10, 0x2a50b0);
            add_ellipsoid(-0.03f, 0.06f, 0, 0.012f, 0.05f, 0.07f, 8, 5, gold);
            add_ellipsoid(-0.03f, -0.06f, 0, 0.012f, 0.07f, 0.045f, 8, 5, 0xc03030);
        }
        m_hero[side ? HP_FOREARM_L : HP_FOREARM_R] = mesh_end();
    }

    mesh_begin(); // espada (articulación: el puño; la hoja apunta hacia +Z)
    add_box(0, 0, -0.02f, 0.045f, 0.045f, 0.15f, 0x3a4a9a);              // empuñadura
    add_ellipsoid(0, 0, -0.11f, 0.035f, 0.035f, 0.035f, 8, 5, gold);       // pomo
    add_box(0, 0, 0.065f, 0.26f, 0.05f, 0.05f, 0x5a6ad0);                 // guarda
    add_ellipsoid(0, 0, 0.065f, 0.03f, 0.03f, 0.03f, 8, 5, gold);
    add_box(0, 0, 0.42f, 0.068f, 0.02f, 0.66f, 0xe4ebf4);                 // hoja
    add_box(0, 0.011f, 0.38f, 0.018f, 0.002f, 0.5f, 0xb8c4d4);            // acanaladura
    mesh_xform(m_mul(m_translate(0, 0, 0.75f), m_rot_x(PI / 2)));
    add_cyl(0, 0, 0, 0.034f, 0, 0.11f, 4, 0xe4ebf4);                      // punta
    m_hero[HP_SWORD] = mesh_end();

    mesh_begin(); // muslo (articulación: la cadera)
    add_ellipsoid(0, -0.15f, 0, 0.09f, 0.18f, 0.095f, 10, 7, tights);
    m_hero[HP_THIGH] = mesh_end();

    mesh_begin(); // pierna y bota (articulación: la rodilla)
    add_ellipsoid(0, -0.1f, 0, 0.075f, 0.13f, 0.08f, 10, 7, tights);
    add_cyl(0, -0.24f, 0, 0.1f, 0.105f, 0.06f, 12, 0x8a5a32);                // vuelta de la bota
    add_ellipsoid(0, -0.31f, 0.03f, 0.1f, 0.1f, 0.135f, 12, 8, leather);
    add_box(0, -0.385f, 0.04f, 0.18f, 0.04f, 0.29f, leather_d);              // suela
    m_hero[HP_SHIN] = mesh_end();
}

// Construye todos los modelos del juego (menos los del escenario, que están en world.c)
static void build_meshes(void) {
    build_hero();

    for (int k = 0; k < 2; k++) { // limo rojo y azul
        mesh_begin();
        add_ellipsoid(0, 0.45f, 0, 0.55f, 0.45f, 0.55f, 18, 12, k ? 0x3a6ad8 : 0xd8403a);
        add_ellipsoid(-0.16f, 0.58f, 0.46f, 0.07f, 0.1f, 0.05f, 8, 6, 0x101018);
        add_ellipsoid(0.16f, 0.58f, 0.46f, 0.07f, 0.1f, 0.05f, 8, 6, 0x101018);
        m_slime[k] = mesh_end();
    }

    for (int k = 0; k < 2; k++) { // gemas verde y azul
        mesh_begin();
        add_gem(0, 0, 0, 0.18f, 0.32f, 6, k ? 0x3a7aff : 0x2ee05a);
        m_gem[k] = mesh_end();
    }

    mesh_begin(); // corazón
    add_ellipsoid(-0.1f, 0.05f, 0, 0.13f, 0.12f, 0.1f, 10, 6, 0xe8304a);
    add_ellipsoid(0.1f, 0.05f, 0, 0.13f, 0.12f, 0.1f, 10, 6, 0xe8304a);
    mesh_xform(m_mul(m_translate(0, 0.05f, 0), m_rot_x(PI)));
    add_cyl(0, 0, 0, 0.22f, 0, 0.3f, 6, 0xe8304a);
    m_heart = mesh_end();

    mesh_begin(); // cofre
    add_box(0, 0.3f, 0, 0.9f, 0.6f, 0.6f, 0x8a5a2a);
    add_box(0, 0.3f, 0, 0.94f, 0.08f, 0.64f, 0xe8c040);
    add_box(-0.36f, 0.3f, 0, 0.08f, 0.62f, 0.64f, 0xe8c040);
    add_box(0.36f, 0.3f, 0, 0.08f, 0.62f, 0.64f, 0xe8c040);
    m_chest = mesh_end();

    mesh_begin(); // tapa del cofre (origen en la bisagra de atrás)
    add_box(0, 0.12f, 0.3f, 0.92f, 0.24f, 0.62f, 0x9a6632);
    add_box(-0.36f, 0.12f, 0.3f, 0.08f, 0.26f, 0.64f, 0xe8c040);
    add_box(0.36f, 0.12f, 0.3f, 0.08f, 0.26f, 0.64f, 0xe8c040);
    add_box(0, 0.08f, 0.62f, 0.12f, 0.14f, 0.04f, 0xe8c040);
    m_lid = mesh_end();
}

// Sube todos los modelos a la tarjeta gráfica y apunta su número.
static void upload_meshes(void) {
    for (int k = 0; k < HP_COUNT; k++) id_hero[k] = gfx_mesh(&m_hero[k]);
    for (int k = 0; k < 2; k++) {
        id_slime[k] = gfx_mesh(&m_slime[k]);
        id_gem[k] = gfx_mesh(&m_gem[k]);
    }
    id_heart = gfx_mesh(&m_heart);
    id_chest = gfx_mesh(&m_chest);
    id_lid = gfx_mesh(&m_lid);
    for (int i = 0; i < world_mesh_count() && i < 32; i++) id_props[i] = gfx_mesh(world_mesh(i));
}

// -----------------------------------------------------------------------------
// 3. PARTÍCULAS Y AYUDANTES
// -----------------------------------------------------------------------------

// Crea una partícula en el primer hueco libre.
static void spawn_part(V3 pos, V3 vel, float life, float size, uint32_t color, int gravity) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (G.parts[i].life > 0) continue;
        G.parts[i] = (Part){ pos, vel, life, size, color, gravity, !gravity }; // las que no caen, brillan
        return;
    }
}

// Explosión de n partículas en todas direcciones (y un poco hacia arriba).
static void burst(V3 pos, int n, uint32_t color, float speed, int gravity, float size) {
    for (int i = 0; i < n; i++) {
        V3 v = v3(rnd(-1, 1), rnd(0.2f, 1.2f), rnd(-1, 1));
        spawn_part(pos, v_mul(v, speed), rnd(0.4f, 0.8f), size, color, gravity);
    }
}

// Suelta una gema o corazón que salta un poco hacia arriba (vy = 4).
static void spawn_pickup(V3 pos, int kind) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (G.pk[i].alive) continue;
        G.pk[i] = (Pickup){ v_add(pos, v3(0, 0.3f, 0)), 4, 0, kind, 1 };
        return;
    }
}

// Muestra un mensaje abajo durante t segundos.
static void set_message(const char *m, float t) { G.msg = m; G.msg_t = t; }

// Mover algo respetando agua, acantilados y obstáculos.
// Se prueba a mover en X y en Z por separado: así, si chocas en diagonal
// contra una pared, te deslizas a lo largo de ella en vez de pararte en seco.
static void move(V3 *pos, float dx, float dz, float r) {
    float nx = pos->x + dx, nz = pos->z + dz;
    if (world_walkable(nx, pos->z)) pos->x = nx;
    if (world_walkable(pos->x, nz)) pos->z = nz;
    world_collide(&pos->x, &pos->z, r);
}

// -----------------------------------------------------------------------------
// 4. EMPEZAR / REINICIAR LA PARTIDA
// -----------------------------------------------------------------------------

static void reset_game(void) {
    world_reset();
    for (int i = 0; i < MAX_PICKUPS; i++) G.pk[i].alive = 0;
    for (int i = 0; i < MAX_PARTS; i++) G.parts[i].life = 0;

    Player *p = &G.p;
    *p = (Player){ 0 };
    p->pos = v3(0, world_height(0, 11), 11);
    p->yaw = PI; // mirando hacia las ruinas
    p->hp = MAX_HP;
    G.cam_yaw = PI;
    G.cam = v3(0, p->pos.y + 1.3f + CAM_HEIGHT, 11 + CAM_DIST);

    // Colocar los limos en sitios al azar (pero siempre los mismos, por la semilla):
    // en suelo firme, lejos del héroe, fuera de las ruinas y sin estar dentro de un árbol
    seed = 0xc0ffee11u;
    for (int i = 0; i < NUM_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        float x = 0, z = 0;
        for (int tries = 0; tries < 200; tries++) {
            x = rnd(-26, 26); z = rnd(-26, 26);
            float tx = x, tz = z;
            if (!world_walkable(x, z) || world_height(x, z) < WATER_Y + 0.3f) continue;
            if (x * x + (z - 11) * (z - 11) < 14 * 14 || x * x + z * z < 8 * 8) continue;
            if (world_collide(&tx, &tz, 0.6f)) continue;
            break;
        }
        *e = (Enemy){ 0 };
        e->pos = e->home = v3(x, world_height(x, z), z);
        e->blue = i % 3 == 0;          // uno de cada tres es azul
        e->hp = e->blue ? 3 : 2;
        e->alive = 1;
        e->hop_t = rnd(0, 1.5f);
        e->yaw = rnd(0, 2 * PI);
    }

#ifdef TEST_FIGHT // para pruebas: un limo justo delante del héroe
    G.en[0].pos = G.en[0].home = v3(0, world_height(0, 8.5f), 8.5f);
#endif
#ifdef TEST_CHEST // para pruebas: empezar sin enemigos y junto al pedestal
    for (int i = 0; i < NUM_ENEMIES; i++) G.en[i].alive = 0;
    p->pos = v3(0, world_height(0, 3), 3);
#endif

    G.chest_active = G.chest_open = 0;
    G.chest_appear = G.chest_t = 0;
    G.hurt_flash = 0;
    set_message("DERROTA A TODOS LOS LIMOS DEL VALLE", 4);
}

// Se llama una vez al arrancar.
void game_init(void) {
    world_init();
    build_meshes();
    if (!gfx_init()) return;
    upload_meshes();
    reset_game();
    G.state = ST_TITLE;
}

// Cuántos limos quedan vivos.
static int enemies_left(void) {
    int n = 0;
    for (int i = 0; i < NUM_ENEMIES; i++) n += G.en[i].alive;
    return n;
}

// -----------------------------------------------------------------------------
// 5. ANIMACIÓN DEL HÉROE (esqueleto)
//
// La posición de cada pieza se calcula ENCADENANDO articulaciones, como en
// un cuerpo de verdad: si giras el hombro, el codo, la mano y la espada se
// mueven con él. En código: matriz_del_codo = matriz_del_hombro * giro_del_codo.
//
//   mundo -> cadera -> torso -> hombro -> brazo -> codo -> antebrazo -> puño -> espada
//                            -> cuello -> cabeza -> ojos / gorro (2 tramos)
//                   -> cadera izq/der -> muslo -> rodilla -> pierna
//
// No hay animaciones grabadas: todo se calcula con senos y el estado del héroe
// (si corre, si ataca, si le han dado...). Por ejemplo, al correr, "walk"
// avanza y m_sin(walk) va de -1 a 1 y vuelve: eso balancea piernas y brazos.
// -----------------------------------------------------------------------------

typedef struct {
    M34 torso, head, eye[2], cap1, cap2;
    M34 upper_arm[2], forearm[2], sword;   // [0] derecho, [1] izquierdo
    M34 thigh[2], shin[2];
} Pose;

// Atajos para que las fórmulas de abajo sean más cortas
static M34 mm(M34 a, M34 b) { return m_mul(a, b); }
static M34 T(float x, float y, float z) { return m_translate(x, y, z); }
static float smooth01(float k) { return k * k * (3 - 2 * k); } // 0..1 con arranque y frenada suaves
static float maxf(float a, float b) { return a > b ? a : b; }

// Calcula la matriz de cada pieza del héroe para este instante.
static Pose hero_pose(const Player *p) {
    Pose P;
    float run = p->run;                              // 0 quieto, 1 corriendo
    float s = m_sin(p->walk), c = m_cos(p->walk);    // ritmo de los pasos
    float breathe = m_sin(G.t * 2.4f);               // ritmo de la respiración
    float hurt = p->inv_t > 0.9f ? (p->inv_t - 0.9f) / 0.3f : 0; // retroceso al recibir un golpe

    // Espadazo: preparación (gira a la derecha) y tajo (barre a la izquierda dando un paso)
    float atk = p->attack_t > 0 ? 1 - p->attack_t / ATTACK_TIME : -1;
    int attacking = atk >= 0;
    float wind = 0, slash = 0;
    if (attacking) {
        wind = atk < 0.25f ? smooth01(atk / 0.25f) : 1;
        slash = atk < 0.25f ? 0 : smooth01((atk - 0.25f) / 0.75f);
    }
    float twist = attacking ? 0.55f * wind - 1.25f * slash : 0;
    float lunge = attacking ? 0.22f * m_sin(slash * PI) : 0;

    // La base: en la posición del héroe y mirando hacia donde mira.
    // La cadera rebota un poco en cada paso y baja al dar el tajo.
    M34 base = mm(T(p->pos.x, p->pos.y, p->pos.z), m_rot_y(p->yaw));
    float hip_y = 0.72f + m_abs(s) * 0.07f * run - (1 - run) * 0.01f * (breathe + 1) - lunge * 0.25f;
    M34 pelvis = mm(base, mm(T(0, hip_y, lunge), m_rot_y(twist * 0.3f - s * 0.08f * run)));

    // Torso: se inclina hacia delante al correr y gira con el espadazo
    P.torso = mm(pelvis, mm(m_rot_x(0.2f * run + 0.15f * slash - 0.35f * hurt),
                            mm(m_rot_y(twist * 0.7f - s * 0.15f * run), m_rot_z(-s * 0.05f * run))));
    P.torso = mm(P.torso, m_scale(1, 1 + breathe * 0.012f, 1)); // respira

    // Cabeza: encima del torso; gira un poco al contrario que el cuerpo
    P.head = mm(P.torso, mm(T(0, 0.47f, 0), mm(m_rot_y(-twist * 0.45f + s * 0.1f * run),
                                               m_rot_x(-0.12f * run + breathe * 0.02f + 0.2f * hurt))));
    for (int i = 0; i < 2; i++) {
        float side = i ? -1.0f : 1.0f;
        // Ojos: al parpadear se aplastan en vertical (escala 0.12)
        P.eye[i] = mm(P.head, mm(T(side * 0.1f, 0.235f, 0.238f),
                                 mm(m_rot_y(side * 0.32f), m_scale(1, p->blink > 0 ? 0.12f : 1, 1))));
    }
    // El gorro se queda atrás al correr y ondea al girar
    P.cap1 = mm(P.head, mm(T(0, 0.36f, -0.05f), mm(m_rot_x(-1.95f - p->cap_lift), m_rot_z(p->cap_side))));
    P.cap2 = mm(P.cap1, mm(T(0, 0.26f, 0), mm(m_rot_x(-0.5f - p->cap_lift * 0.8f + s * 0.08f * run),
                                              m_rot_z(p->cap_side * 1.5f))));

    // Piernas: balanceo opuesto y rodillas que se doblan al pasar el pie
    for (int i = 0; i < 2; i++) {
        float side = i ? -1.0f : 1.0f;           // i=0 derecha (+X)
        float swing = (i ? -s : s) * 0.85f * run;
        float lift = maxf(0, i ? c : -c) * 1.3f * run;
        float step = attacking ? (i ? -0.5f : 0.35f) * slash : 0;
        P.thigh[i] = mm(pelvis, mm(T(side * 0.11f, 0, 0), mm(m_rot_x(swing - step - 0.05f), m_rot_z(side * 0.04f))));
        P.shin[i] = mm(P.thigh[i], mm(T(0, -0.32f, 0), m_rot_x(lift + 0.1f)));
    }

    // Brazos: balanceo contrario a las piernas; el derecho da el espadazo
    for (int i = 0; i < 2; i++) {
        float side = i ? -1.0f : 1.0f;
        float swing = (i ? s : -s) * 0.75f * run;
        M34 shoulder = mm(P.torso, T(side * 0.27f, 0.38f, 0));
        M34 up, fore;
        if (i == 0 && attacking) {
            float yaw = 1.4f * wind - 2.6f * slash;        // de la derecha hacia la izquierda
            float raise = -1.2f - 0.35f * wind;            // brazo hacia delante
            up = mm(m_rot_y(yaw), mm(m_rot_x(raise), m_rot_z(0.2f)));
            fore = m_rot_x(-0.25f * (1 - slash));
        } else if (i == 1) {                               // escudo delante del cuerpo
            up = mm(m_rot_x(-0.25f + swing * 0.6f), m_rot_z(-0.25f));
            fore = mm(m_rot_x(-1.1f - 0.2f * run), m_rot_y(0.5f));
        } else {
            up = mm(m_rot_x(-0.15f + swing + breathe * 0.02f), m_rot_z(0.18f));
            fore = m_rot_x(-0.45f - 0.6f * run);
        }
        P.upper_arm[i] = mm(shoulder, up);
        P.forearm[i] = mm(P.upper_arm[i], mm(T(0, -0.24f, 0), fore));
    }
    // La espada en el puño: en reposo apunta al frente; al atacar prolonga el brazo
    float tilt = attacking ? 0.95f + 0.62f * wind : 0.95f;
    P.sword = mm(P.forearm[0], mm(T(0, -0.25f, 0.02f), m_rot_x(tilt)));
    return P;
}

// -----------------------------------------------------------------------------
// 6. COMBATE
// -----------------------------------------------------------------------------

// Un limo muere: explosión de partículas y suelta un premio al azar.
static void kill_enemy(Enemy *e) {
    e->alive = 0;
    V3 c = v_add(e->pos, v3(0, 0.5f, 0));
    burst(c, 18, 0xffffff, 3, 0, 0.12f);
    burst(c, 10, e->blue ? 0x6a9aff : 0xff7060, 4, 1, 0.1f);
    float r = rnd(0, 1);
    if (r < 0.5f) spawn_pickup(e->pos, PK_GEM_G);
    else if (r < 0.75f) spawn_pickup(e->pos, PK_GEM_B);
    else spawn_pickup(e->pos, G.p.hp < MAX_HP ? PK_HEART : PK_GEM_G);
    if (enemies_left() > 0) set_message(enemies_left() == 1 ? "QUEDA UN LIMO" : "LIMO DERROTADO", 1.5f);
}

// El momento del golpe de la espada: ¿a quién le da?
static void do_attack(void) {
    Player *p = &G.p;
    float fx = m_sin(p->yaw), fz = m_cos(p->yaw);  // dirección en la que mira el héroe

    // A los limos cercanos que estén delante (no detrás).
    // El producto escalar entre "hacia dónde miro" y "hacia dónde está el
    // limo" es positivo si está delante y negativo si está detrás.

    for (int i = 0; i < NUM_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        if (!e->alive) continue;
        float dx = e->pos.x - p->pos.x, dz = e->pos.z - p->pos.z;
        float d = m_sqrt(dx * dx + dz * dz);
        if (d > SWORD_REACH || d < 0.001f || m_abs(e->pos.y - p->pos.y) > 1.5f) continue;
        if ((dx * fx + dz * fz) / d < -0.1f) continue; // está detrás
        e->hp--;
        e->hit_t = 0.3f;
        e->vx = dx / d * 6; e->vz = dz / d * 6; e->vy = 2.5f; // retroceso
        burst(v_add(e->pos, v3(0, 0.6f, 0)), 6, 0xfff0a0, 3, 0, 0.08f);
        if (e->hp <= 0) kill_enemy(e);
    }

    // A los arbustos que haya justo delante
    V3 cut[8];
    int n = world_cut_bushes(p->pos.x + fx * 1.1f, p->pos.z + fz * 1.1f, 1.2f, cut, 8);
    for (int i = 0; i < n; i++) {
        burst(v_add(cut[i], v3(0, 0.4f, 0)), 16, 0x5ab84a, 3, 1, 0.1f);
        burst(v_add(cut[i], v3(0, 0.4f, 0)), 6, 0x8ad86a, 2, 1, 0.08f);
        if (rnd(0, 1) < 0.35f) spawn_pickup(cut[i], rnd(0, 1) < 0.8f ? PK_GEM_G : PK_HEART);
    }

    // Y si estamos junto al cofre, se abre
    if (G.chest_active && !G.chest_open && G.chest_appear >= 1) {
        float d2 = p->pos.x * p->pos.x + p->pos.z * p->pos.z;
        if (d2 < CHEST_REACH * CHEST_REACH) {
            G.chest_open = 1;
            G.chest_t = 0;
            set_message("", 0);
        }
    }
}

// -----------------------------------------------------------------------------
// 7. ACTUALIZACIÓN (60 veces por segundo)
// -----------------------------------------------------------------------------

// El héroe: moverse, girar, animación y espadazos.
static void update_player(const Input *in, float dt) {
    Player *p = &G.p;
    // Teclas como números: mx = -1 (izquierda), 0 o 1 (derecha); mz igual para atrás/adelante
    float mx = (float)(in->down[KEY_D] - in->down[KEY_A]);
    float mz = (float)(in->down[KEY_W] - in->down[KEY_S]);
    float rot = (float)((in->down[KEY_LEFT] || in->down[KEY_Q]) - (in->down[KEY_RIGHT] || in->down[KEY_E]));

    // Los controles son relativos a la cámara: "adelante" es hacia donde mira
    // la cámara, no hacia el norte del mapa.
    G.cam_yaw -= rot * 2.2f * dt;
    V3 fwd = v3(m_sin(G.cam_yaw), 0, m_cos(G.cam_yaw));
    V3 right = v3(m_cos(G.cam_yaw), 0, -m_sin(G.cam_yaw));
    float dx = right.x * mx + fwd.x * mz, dz = right.z * mx + fwd.z * mz;
    float len = m_sqrt(dx * dx + dz * dz);

    float old_yaw = p->yaw;
    p->moving = len > 0.01f;
    if (p->moving) {
        dx /= len; dz /= len;   // dirección de longitud 1 (en diagonal no se va más rápido)
        float speed = PLAYER_SPEED * (p->attack_t > 0 ? 0.35f : 1);
        move(&p->pos, dx * speed * dt, dz * speed * dt, PLAYER_R);
        // Girar poco a poco hacia la dirección en la que nos movemos
        float target = m_atan2(dx, dz);
        p->yaw += wrap_angle(target - p->yaw) * minf(1, 14 * dt);
        float before = m_sin(p->walk);
        p->walk += dt * 11.5f;
        if ((before < 0) != (m_sin(p->walk) < 0) && p->run > 0.6f) { // polvo al pisar
            float side = m_sin(p->walk) < 0 ? 1.0f : -1.0f;
            V3 foot = v3(p->pos.x + m_cos(p->yaw) * 0.11f * side, p->pos.y + 0.05f, p->pos.z - m_sin(p->yaw) * 0.11f * side);
            for (int k = 0; k < 3; k++)
                spawn_part(foot, v3(rnd(-0.6f, 0.6f), rnd(0.3f, 0.8f), rnd(-0.6f, 0.6f)), 0.4f, 0.09f, 0xd8c8a0, 1);
        }
        // La cámara se recoloca poco a poco detrás del héroe
        if (rot == 0 && mz >= 0) G.cam_yaw += wrap_angle(p->yaw - G.cam_yaw) * 1.2f * dt;
    }
    // Empujón al recibir un golpe: se va frenando poco a poco
    if (m_abs(p->kx) + m_abs(p->kz) > 0.01f) {
        move(&p->pos, p->kx * dt, p->kz * dt, PLAYER_R);
        float k = 1 - 10 * dt;
        if (k < 0) k = 0;
        p->kx *= k; p->kz *= k;
    }
    p->pos.y = world_height(p->pos.x, p->pos.z); // pegado al suelo

    // Estado de la animación. Truco usado mucho aquí:
    //    valor += (objetivo - valor) * velocidad * dt
    // hace que "valor" se acerque al objetivo poco a poco, de forma suave.
    p->run += ((p->moving ? 1.0f : 0.0f) - p->run) * minf(1, 10 * dt);
    p->turn += (wrap_angle(p->yaw - old_yaw) / (dt > 0 ? dt : 1) - p->turn) * minf(1, 8 * dt);
    p->cap_lift += (0.45f * p->run - p->cap_lift) * minf(1, 4 * dt);
    p->cap_side += (-p->turn * 0.06f - p->cap_side) * minf(1, 5 * dt);
    if (p->cap_side > 0.6f) p->cap_side = 0.6f;
    if (p->cap_side < -0.6f) p->cap_side = -0.6f;
    p->blink_t -= dt;
    p->blink -= dt;
    if (p->blink_t <= 0) { p->blink = 0.12f; p->blink_t = rnd(2, 5); }

    // Espadazo: el golpe "cuenta" a mitad de la animación, no al principio
    if ((in->pressed[KEY_J] || in->pressed[KEY_SPACE]) && p->attack_t <= 0) {
        p->attack_t = ATTACK_TIME;
        p->hit_done = 0;
    }
    if (p->attack_t > 0) {
        p->attack_t -= dt;
        if (!p->hit_done && p->attack_t < ATTACK_TIME * 0.55f) {
            p->hit_done = 1;
            do_attack();
        }
        // Estela que sale de la hoja de la espada
        Pose P = hero_pose(p);
        for (int k = 0; k < 3; k++)
            spawn_part(m_apply(&P.sword, v3(0, 0, 0.35f + k * 0.2f)), v3(0, 0, 0), 0.14f, 0.07f + k * 0.025f, k == 2 ? 0xffffff : 0xd8e8ff, 0);
    }
    if (p->inv_t > 0) p->inv_t -= dt;
}

// El héroe recibe un golpe desde (from_x, from_z): pierde medio corazón,
// sale empujado y queda invulnerable 1,2 segundos (parpadea).
static void hurt_player(float from_x, float from_z) {
    Player *p = &G.p;
    float dx = p->pos.x - from_x, dz = p->pos.z - from_z;
    float d = m_sqrt(dx * dx + dz * dz);
    if (d < 0.001f) { dx = 1; dz = 0; d = 1; }
    p->hp--;
    p->inv_t = 1.2f;
    p->kx = dx / d * 10; p->kz = dz / d * 10;
    G.hurt_flash = 0.2f;
    burst(v_add(p->pos, v3(0, 1, 0)), 8, 0xff4040, 3, 1, 0.08f);
    if (p->hp <= 0) {
        G.state = ST_DEAD;
        G.state_t = 0;
    }
}

// Los limos: van a saltos. Si el héroe está cerca (< 11 m) saltan hacia él;
// si no, pasean cerca de su casa. Si tocan al héroe, le hacen daño.
static void update_enemies(float dt) {
    Player *p = &G.p;
    for (int i = 0; i < NUM_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        if (!e->alive) continue;
        float dx = p->pos.x - e->pos.x, dz = p->pos.z - e->pos.z;
        float d = m_sqrt(dx * dx + dz * dz);
        int chase = d < 11 && G.state == ST_PLAY;
        if (e->hit_t > 0) e->hit_t -= dt;

        float gy = world_height(e->pos.x, e->pos.z);
        if (e->pos.y <= gy + 0.001f && e->vy <= 0) { // en el suelo
            float k = 1 - 12 * dt;
            if (k < 0) k = 0;
            e->vx *= k; e->vz *= k;
            e->hop_t -= dt;
            if (e->hop_t <= 0 && e->hit_t <= 0) {
                float hx, hz;
                if (chase && d > 0.001f) {
                    hx = dx / d; hz = dz / d;
                } else { // pasea cerca de su casa
                    float a = rnd(0, 2 * PI);
                    hx = m_cos(a) + (e->home.x - e->pos.x) * 0.15f;
                    hz = m_sin(a) + (e->home.z - e->pos.z) * 0.15f;
                    float l = m_sqrt(hx * hx + hz * hz);
                    if (l > 0.001f) { hx /= l; hz /= l; }
                }
                float sp = chase ? (e->blue ? 3.8f : 3.0f) : 1.3f;
                e->vx = hx * sp; e->vz = hz * sp;
                e->vy = chase ? 4.5f : 3.2f;
                e->hop_t = chase ? rnd(0.35f, 0.6f) : rnd(1.0f, 2.2f);
                e->yaw = m_atan2(hx, hz);
                e->squash = -0.6f; // se estira al saltar
            }
        }

        // Física del salto: la gravedad resta velocidad hacia arriba cada instante
        e->vy -= 16 * dt;
        move(&e->pos, e->vx * dt, e->vz * dt, 0.5f);
        e->pos.y += e->vy * dt;
        gy = world_height(e->pos.x, e->pos.z);
        if (e->pos.y < gy) {
            if (e->vy < -2) e->squash = 1; // se aplasta al caer
            e->pos.y = gy;
            e->vy = 0;
        }
        e->squash += (0 - e->squash) * minf(1, 8 * dt);

        if (G.state == ST_PLAY && d < 0.95f && p->inv_t <= 0 && e->hit_t <= 0 && m_abs(e->pos.y - p->pos.y) < 1)
            hurt_player(e->pos.x, e->pos.z);
    }
}

// Gemas y corazones: caen al suelo y se recogen al pasar por encima.
static void update_pickups(float dt) {
    Player *p = &G.p;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &G.pk[i];
        if (!k->alive) continue;
        k->t += dt;
        k->vy -= 12 * dt;
        k->pos.y += k->vy * dt;
        float gy = world_height(k->pos.x, k->pos.z);
        if (k->pos.y < gy) { k->pos.y = gy; k->vy = 0; }

        float dx = p->pos.x - k->pos.x, dz = p->pos.z - k->pos.z;
        if (G.state == ST_PLAY && k->t > 0.3f && dx * dx + dz * dz < 1.0f) {
            k->alive = 0;
            V3 c = v_add(k->pos, v3(0, 0.5f, 0));
            if (k->kind == PK_HEART) {
                p->hp += 2;
                if (p->hp > MAX_HP) p->hp = MAX_HP;
                burst(c, 10, 0xff6080, 2, 0, 0.08f);
            } else {
                p->gems += k->kind == PK_GEM_B ? 5 : 1;
                burst(c, 8, k->kind == PK_GEM_B ? 0x80b0ff : 0x80ff90, 2, 0, 0.07f);
            }
        }
    }
}

// Mover partículas: posición += velocidad * tiempo (y gravedad si caen).
static void update_particles(float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *q = &G.parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        if (q->gravity) q->vel.y -= 9 * dt;
        q->pos = v_add(q->pos, v_mul(q->vel, dt));
    }
}

// El cofre: aparece cuando no quedan limos, suelta una columna de luz y,
// al abrirlo, el tesoro sube; poco después, pantalla de victoria.
static void update_chest(float dt) {
    if (!G.chest_active && G.state == ST_PLAY && enemies_left() == 0) {
        G.chest_active = 1;
        G.chest_appear = 0;
        set_message("ALGO HA APARECIDO EN LAS RUINAS...", 4);
    }
    if (!G.chest_active) return;
    if (G.chest_appear < 1) G.chest_appear += dt * 1.5f;
    if (G.chest_appear > 1) G.chest_appear = 1;

    float top = world_pedestal_top();
    if (!G.chest_open && rnd(0, 1) < dt * 25) // columna de luz
        spawn_part(v3(rnd(-0.6f, 0.6f), top + rnd(0, 0.5f), rnd(-0.6f, 0.6f)),
                   v3(0, rnd(1.5f, 3), 0), rnd(0.8f, 1.6f), 0.07f, rnd(0, 1) < 0.5f ? 0xfff0a0 : 0xffffff, 0);
    if (G.chest_open) {
        G.chest_t += dt;
        if (G.chest_t < 1.2f && rnd(0, 1) < dt * 40)
            spawn_part(v3(rnd(-0.4f, 0.4f), top + 0.7f, rnd(-0.4f, 0.4f)),
                       v3(rnd(-1, 1), rnd(2, 4), rnd(-1, 1)), 0.8f, 0.08f, 0xffe060, 0);
        if (G.chest_t > 2.2f && G.state == ST_PLAY) {
            G.state = ST_WIN;
            G.state_t = 0;
        }
    }
}

// La cámara: quiere estar detrás del héroe, a CAM_DIST metros y un poco
// por encima. No salta allí de golpe: cada frame recorre una parte del
// camino, así se mueve con suavidad. Nunca baja del suelo ni se mete en árboles.
static void update_camera(float dt) {
    V3 target = v_add(G.p.pos, v3(0, 1.3f, 0));
    V3 want = v3(target.x - m_sin(G.cam_yaw) * CAM_DIST, target.y + CAM_HEIGHT, target.z - m_cos(G.cam_yaw) * CAM_DIST);
    float gy = world_height(want.x, want.z) + 0.8f;
    if (want.y < gy) want.y = gy;
    G.cam = v_add(G.cam, v_mul(v_sub(want, G.cam), minf(1, 7 * dt)));
    world_collide(&G.cam.x, &G.cam.z, 1.0f); // que no se meta dentro de árboles y columnas
}

// Se llama 60 veces por segundo: actualiza todo según la pantalla en la que estemos.
int game_update(const Input *in, float dt) {
    if (in->pressed[KEY_ESCAPE]) return 0;
    G.t += dt;
    G.state_t += dt;
    if (G.msg_t > 0) G.msg_t -= dt;
    if (G.hurt_flash > 0) G.hurt_flash -= dt;
    int start = in->pressed[KEY_ENTER] || in->pressed[KEY_SPACE] || in->pressed[KEY_J];

    switch (G.state) {
    case ST_TITLE:
        if (start) { reset_game(); G.state = ST_PLAY; G.state_t = 0; }
        break;
    case ST_PLAY:
        update_player(in, dt);
        break;
    case ST_DEAD:
    case ST_WIN:
        if (start && G.state_t > 1.0f) { reset_game(); G.state = ST_PLAY; G.state_t = 0; }
        break;
    }
    update_enemies(dt);
    update_pickups(dt);
    update_particles(dt);
    update_chest(dt);

    // Polen y lucecitas flotando en el aire
    G.mote_acc += dt;
    while (G.mote_acc > 0.04f) {
        G.mote_acc -= 0.04f;
        V3 c = G.state == ST_TITLE ? v3(0, 0, 0) : G.p.pos;
        float x = c.x + rnd(-14, 14), z = c.z + rnd(-14, 14);
        spawn_part(v3(x, world_height(x, z) + rnd(0.3f, 3.0f), z), v3(rnd(-0.3f, 0.3f), rnd(-0.05f, 0.15f), rnd(-0.3f, 0.3f)),
                   rnd(2, 4), rnd(0.02f, 0.035f), rnd(0, 1) < 0.8f ? 0xfff2c0 : 0xc8ffb0, 0);
    }
    if (G.state != ST_TITLE) update_camera(dt);
    return 1;
}

// -----------------------------------------------------------------------------
// 8. DIBUJO
// Aquí no se pinta nada directamente: se le dice a gfx.c qué modelos hay y
// dónde (gfx_draw), y al final gfx_end lo pinta todo con la tarjeta gráfica.
// gfx_draw(modelo, dónde, color_de_destello, brillo, ¿da sombra?)
// -----------------------------------------------------------------------------

// Árboles, rocas, flores, ruinas...
static void draw_world(void) {
    for (int i = 0; i < world_prop_count(); i++) {
        const Prop *pr = world_prop(i);
        if (!pr->alive) continue;
        M34 m = world_prop_matrix(pr);
        gfx_draw(id_props[world_prop_mesh(pr)], &m, 0, 0, pr->kind != PROP_FLOWER);
    }
}

// El héroe: cada pieza con la matriz que calculó hero_pose.
static void draw_player(void) {
    Player *p = &G.p;
    if (p->inv_t > 0 && ((int)(p->inv_t * 15)) % 2) return; // parpadeo tras recibir daño

    Pose P = hero_pose(p);
    gfx_draw(id_hero[HP_TORSO], &P.torso, 0, 0.1f, 1);
    gfx_draw(id_hero[HP_HEAD], &P.head, 0, 0.15f, 1);
    gfx_draw(id_hero[HP_EYE], &P.eye[0], 0, 0.8f, 0);
    gfx_draw(id_hero[HP_EYE], &P.eye[1], 0, 0.8f, 0);
    gfx_draw(id_hero[HP_CAP1], &P.cap1, 0, 0.1f, 1);
    gfx_draw(id_hero[HP_CAP2], &P.cap2, 0, 0.1f, 1);
    for (int i = 0; i < 2; i++) {
        gfx_draw(id_hero[HP_UPPER_ARM], &P.upper_arm[i], 0, 0.1f, 1);
        gfx_draw(id_hero[i ? HP_FOREARM_L : HP_FOREARM_R], &P.forearm[i], 0, i ? 0.6f : 0.1f, 1);
        gfx_draw(id_hero[HP_THIGH], &P.thigh[i], 0, 0.1f, 1);
        gfx_draw(id_hero[HP_SHIN], &P.shin[i], 0, 0.2f, 1);
    }
    gfx_draw(id_hero[HP_SWORD], &P.sword, 0, 2.0f, 1);
}

// Los limos, estirados o aplastados según el salto, y blancos si les acaban de dar.
static void draw_enemies(void) {
    for (int i = 0; i < NUM_ENEMIES; i++) {
        Enemy *e = &G.en[i];
        if (!e->alive) continue;
        float sxz = 1 + e->squash * 0.3f, sy = 1 - e->squash * 0.35f + m_sin(G.t * 6 + i) * 0.04f;
        M34 m = m_mul(m_translate(e->pos.x, e->pos.y, e->pos.z), m_mul(m_rot_y(e->yaw), m_scale(sxz, sy, sxz)));
        gfx_draw(id_slime[e->blue], &m, e->hit_t > 0.15f ? 0xffffff : 0, 1.2f, 1); // limos brillantes
    }
}

// Gemas y corazones flotando y girando.
static void draw_pickups(void) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *k = &G.pk[i];
        if (!k->alive) continue;
        float y = k->pos.y + 0.45f + m_sin(G.t * 3 + i) * 0.1f;
        M34 m = m_mul(m_translate(k->pos.x, y, k->pos.z), m_rot_y(G.t * 3 + i));
        gfx_draw(k->kind == PK_HEART ? id_heart : id_gem[k->kind == PK_GEM_B], &m, 0, 1.5f, 1);
        // halo de luz alrededor
        uint32_t c = k->kind == PK_HEART ? 0xff6080 : k->kind == PK_GEM_B ? 0x6090ff : 0x60ff80;
        gfx_particle(v3(k->pos.x, y, k->pos.z), 0.9f, c, 0.35f, 1);
    }
}

// El cofre: crece al aparecer, la tapa gira al abrirse y el tesoro sube.
static void draw_chest(void) {
    if (!G.chest_active) return;
    float top = world_pedestal_top();
    float s = G.chest_appear * G.chest_appear * (3 - 2 * G.chest_appear);
    M34 base = m_mul(m_translate(0, top, 0), m_scale(s, s, s));
    gfx_draw(id_chest, &base, 0, 0.3f, 1);
    float open = G.chest_open ? minf(1, G.chest_t * 1.5f) : 0;
    M34 lid = m_mul(base, m_mul(m_translate(0, 0.6f, -0.3f), m_rot_x(-open * 1.9f)));
    gfx_draw(id_lid, &lid, 0, 0.3f, 1);
    if (G.chest_open) { // el tesoro sube girando
        float rise = minf(1.2f, G.chest_t * 0.9f);
        V3 gp = v3(0, top + 0.5f + rise, 0);
        M34 g = m_mul(m_translate(gp.x, gp.y, gp.z), m_mul(m_rot_y(G.t * 4), m_scale(2, 2, 2)));
        gfx_draw(id_gem[0], &g, 0xffd040, 2.0f, 1);
        gfx_particle(gp, 3.0f, 0xffd860, 0.6f, 1);
    }
}

// Las partículas se van desvaneciendo en su último tercio de segundo.
static void draw_particles(void) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *q = &G.parts[i];
        if (q->life <= 0) continue;
        float alpha = minf(1, q->life * 3);
        gfx_particle(q->pos, q->size * (q->glow ? 2.5f : 1.6f), q->color, alpha, q->glow);
    }
}

// --- INTERFAZ en pixel art (dibujada con render.c en el lienzo de 320x180) ---

// Dibujo del corazón de la vida, 7x6 píxeles
static const char *heart_rows[6] = { ".xx.xx.", "xxxxxxx", "xxxxxxx", ".xxxxx.", "..xxx..", "...x..." };

static void draw_heart(Framebuffer *fb, int x, int y, int fill) { // fill: 0 vacío, 1 medio, 2 lleno
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 7; c++) {
            if (heart_rows[r][c] != 'x') continue;
            // contorno: los vecinos vacíos se pintan oscuros
            for (int k = 0; k < 4; k++) {
                int nr = r + (k == 0) - (k == 1), nc = c + (k == 2) - (k == 3);
                int empty = nr < 0 || nr > 5 || nc < 0 || nc > 6 || heart_rows[nr][nc] != 'x';
                if (empty) fill_rect(fb, x + nc * 2, y + nr * 2, 2, 2, 0x201018);
            }
        }
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 7; c++) {
            if (heart_rows[r][c] != 'x') continue;
            int red = fill == 2 || (fill == 1 && c < 4);
            uint32_t col = red ? (r == 1 && c == 1 ? 0xffb0c0 : 0xe8304a) : 0x5a3a48;
            fill_rect(fb, x + c * 2, y + r * 2, 2, 2, col);
        }
}

// Icono de gema: un rombo hecho de líneas horizontales cada vez más anchas y luego más estrechas.
static void draw_gem_icon(Framebuffer *fb, int x, int y, uint32_t color) {
    for (int r = 0; r < 9; r++) {
        int half = r < 4 ? r : 8 - r;
        fill_rect(fb, x + 4 - half - 1, y + r, half * 2 + 3, 1, 0x101820);
    }
    for (int r = 1; r < 8; r++) {
        int half = r < 4 ? r - 1 : 7 - r;
        fill_rect(fb, x + 4 - half, y + r, half * 2 + 1, 1, color);
    }
    fill_rect(fb, x + 3, y + 3, 1, 2, 0xffffff);
}

// Recuadro oscuro con borde dorado (para los mensajes).
static void panel(Framebuffer *fb, int x, int y, int w, int h) {
    fill_rect(fb, x - 1, y - 1, w + 2, h + 2, 0xe8d8a0);
    fill_rect(fb, x, y, w, h, 0x141a2c);
}

// Texto centrado con sombra (la misma frase en negro, desplazada un poco).
static void text_center(Framebuffer *fb, const char *s, int y, int scale, uint32_t color) {
    int tw = text_width(s, scale);
    draw_text(fb, s, (W - tw) / 2 + scale, y + scale, scale, 0x101018); // sombra
    draw_text(fb, s, (W - tw) / 2, y, scale, color);
}

// Convierte un número en texto (123 -> "123"): saca las cifras de la última a la
// primera con % 10 y / 10, y luego les da la vuelta.
static void int_to_str(int n, char *buf) {
    char tmp[12];
    int i = 0;
    do { tmp[i++] = (char)('0' + n % 10); n /= 10; } while (n > 0);
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = 0;
}

// Toda la interfaz: título, corazones, gemas, mensajes y pantallas de fin.
static void draw_hud(Framebuffer *fb) {
    Player *p = &G.p;
    int blink = ((int)(G.t * 2)) % 2 == 0;

    if (G.state == ST_TITLE) {
        fill_rect(fb, 0, 34, W, 44, 0x141a2c);
        fill_rect(fb, 0, 34, W, 1, 0xe8d8a0);
        fill_rect(fb, 0, 77, W, 1, 0xe8d8a0);
        text_center(fb, "LA LEYENDA", 40, 3, 0xffe070);
        text_center(fb, "DEL BOSQUE", 60, 3, 0xffe070);
        if (blink) text_center(fb, "PULSA ENTER PARA EMPEZAR", 120, 1, 0xffffff);
        text_center(fb, "WASD: MOVER   J / ESPACIO: ESPADA   Q E / FLECHAS: CAMARA", 160, 1, 0xe8f0ff);
        return;
    }

    // Corazones
    for (int i = 0; i < MAX_HP / 2; i++) {
        int fill = p->hp >= (i + 1) * 2 ? 2 : p->hp == i * 2 + 1 ? 1 : 0;
        draw_heart(fb, 6 + i * 16, 6, fill);
    }
    // Gemas
    char buf[16];
    int_to_str(p->gems, buf);
    draw_gem_icon(fb, W - 34, 6, 0x2ee05a);
    draw_text(fb, buf, W - 23, 9, 2, 0x101018);
    draw_text(fb, buf, W - 24, 8, 2, 0xffffff);
    // Limos que quedan
    char line[32] = "LIMOS: ";
    int_to_str(enemies_left(), line + 7);
    draw_text(fb, line, 7, 23, 1, 0x101018);
    draw_text(fb, line, 6, 22, 1, 0xffffff);

    // Mensajes (el aviso del cofre tiene prioridad)
    if (G.state == ST_PLAY && G.chest_active && !G.chest_open && G.chest_appear >= 1 &&
        p->pos.x * p->pos.x + p->pos.z * p->pos.z < CHEST_REACH * CHEST_REACH) {
        const char *s = "PULSA J PARA ABRIR EL COFRE";
        int tw = text_width(s, 1);
        panel(fb, (W - tw) / 2 - 6, H - 22, tw + 12, 13);
        draw_text(fb, s, (W - tw) / 2, H - 18, 1, blink ? 0xffe070 : 0xffffff);
    } else if (G.msg_t > 0 && G.msg && G.msg[0]) {
        int tw = text_width(G.msg, 1);
        panel(fb, (W - tw) / 2 - 6, H - 22, tw + 12, 13);
        draw_text(fb, G.msg, (W - tw) / 2, H - 18, 1, 0xffffff);
    }

    // Destello rojo al recibir daño
    if (G.hurt_flash > 0) {
        fill_rect(fb, 0, 0, W, 3, 0xd02020);
        fill_rect(fb, 0, H - 3, W, 3, 0xd02020);
        fill_rect(fb, 0, 0, 3, H, 0xd02020);
        fill_rect(fb, W - 3, 0, 3, H, 0xd02020);
    }

    if (G.state == ST_DEAD) {
        fill_rect(fb, 0, 60, W, 50, 0x141a2c);
        text_center(fb, "HAS CAIDO...", 68, 3, 0xff6060);
        if (G.state_t > 1 && blink) text_center(fb, "PULSA ENTER PARA REINTENTAR", 96, 1, 0xffffff);
    } else if (G.state == ST_WIN) {
        fill_rect(fb, 0, 50, W, 66, 0x141a2c);
        fill_rect(fb, 0, 50, W, 1, 0xe8d8a0);
        fill_rect(fb, 0, 115, W, 1, 0xe8d8a0);
        text_center(fb, "HAS ENCONTRADO", 58, 2, 0xffe070);
        text_center(fb, "EL TESORO DEL BOSQUE!", 76, 2, 0xffe070);
        char g[32] = "GEMAS: ";
        int_to_str(p->gems, g + 7);
        text_center(fb, g, 96, 1, 0xffffff);
        if (G.state_t > 1 && blink) text_center(fb, "PULSA ENTER PARA JUGAR OTRA VEZ", 106, 1, 0xc8d0e0);
    }
}

// Dibuja la imagen completa. La llama main_gl.c en cada frame.
void game_render_gl(int width, int height) {
    V3 cam, target;
    if (G.state == ST_TITLE) { // la cámara gira alrededor de las ruinas
        float a = G.t * 0.12f;
        cam = v3(m_sin(a) * 17, 8.5f, m_cos(a) * 17);
        target = v3(0, 1.5f, 0);
    } else {
        cam = G.cam;
        target = v_add(G.p.pos, v3(0, 1.3f, 0));
    }
    // Ángulos para que la cámara mire hacia el objetivo
    V3 d = v_sub(target, cam);
    float yaw = m_atan2(d.x, d.z);
    float pitch = m_atan2(-d.y, m_sqrt(d.x * d.x + d.z * d.z));

    gfx_begin(width, height, cam, yaw, pitch, G.state == ST_TITLE ? v3(0, 0, 0) : G.p.pos, G.t);
    draw_world();
    draw_chest();
    if (G.state != ST_TITLE) draw_player();
    draw_enemies();
    draw_pickups();
    draw_particles();

    // La interfaz se pinta en pixel art y va por encima de todo
    for (int i = 0; i < W * H; i++) hud_pixels[i] = GFX_OVERLAY_KEY;
    draw_hud(&hud);
    gfx_end(hud_pixels, W, H);
}
