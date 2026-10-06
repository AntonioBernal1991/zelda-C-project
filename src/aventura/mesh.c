// =============================================================================
// mesh.c  —  Construcción de modelos 3D con formas simples
// =============================================================================
//
// Todos los modelos se guardan en un "almacén" común: un array grande de
// vértices y otro de triángulos. Cada triángulo apunta a 3 vértices por su
// número de posición en el almacén.
//
// LA NORMAL: para iluminar un triángulo hay que saber hacia dónde "mira" su
// cara (su normal). Si mira hacia el sol, se pinta claro; si no, oscuro.
//   - En formas con aristas (cajas, conos) se usa la normal de cada cara:
//     se ven las facetas, como una gema.
//   - En las esferas se usa una normal distinta en cada vértice, calculada
//     con la forma de la esfera: la luz cambia de forma gradual y se ven lisas.
// =============================================================================

#include "mesh.h"

#define MAX_VERTS 32768
#define MAX_TRIS  32768

// El almacén común
static V3       pool_v[MAX_VERTS];      // posición de cada vértice
static V3       pool_n[MAX_VERTS];      // normal suave (solo si el vértice la tiene)
static uint8_t  pool_smooth[MAX_VERTS]; // 1 si usa la normal suave
static int      pool_t[MAX_TRIS][3];    // los 3 vértices de cada triángulo
static uint32_t pool_c[MAX_TRIS];       // color de cada triángulo
static V3       pool_center[MAX_TRIS];  // centro de la pieza a la que pertenece (ver mesh_export)
static int      nverts, ntris;          // cuántos hay ocupados

static Mesh cur;           // el modelo que estamos construyendo
static M34  xf;            // transformación que se aplica a las piezas nuevas
static int  smooth;        // ¿esferas suaves?
static V3   shape_center;  // centro de la pieza que se está añadiendo

void mesh_begin(void) {
    cur.v0 = nverts;   // el modelo empieza donde acaba el anterior
    cur.t0 = ntris;
    xf = m_identity();
    smooth = 1;
}

void mesh_xform(M34 m) { xf = m; }
void mesh_smooth(int on) { smooth = on; }

Mesh mesh_end(void) {
    cur.nv = nverts - cur.v0;
    cur.nt = ntris - cur.t0;
    // Radio: la distancia del vértice más lejano al origen
    float r = 0;
    for (int i = cur.v0; i < nverts; i++) {
        float l = v_len(pool_v[i]);
        if (l > r) r = l;
    }
    cur.radius = r;
    return cur;
}

static void set_center(float x, float y, float z) { shape_center = m_apply(&xf, v3(x, y, z)); }

// Añade un vértice (ya movido/girado con xf) y devuelve su número.
static int vert(float x, float y, float z) {
    if (nverts >= MAX_VERTS) return nverts - 1; // almacén lleno: no rompemos nada
    pool_v[nverts] = m_apply(&xf, v3(x, y, z));
    pool_smooth[nverts] = 0;
    return nverts++;
}

// Añade un triángulo con tres vértices ya creados.
static void tri(int a, int b, int c, uint32_t col) {
    if (ntris >= MAX_TRIS) return;
    pool_t[ntris][0] = a;
    pool_t[ntris][1] = b;
    pool_t[ntris][2] = c;
    pool_c[ntris] = col;
    pool_center[ntris] = shape_center;
    ntris++;
}

// Un cuadrilátero = dos triángulos.
static void quad(int a, int b, int c, int d, uint32_t col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}

// Caja: 8 esquinas y 6 caras (12 triángulos).
// Truco: el número de esquina i (0 a 7) en binario dice hacia qué lado está:
//   bit 1 -> derecha o izquierda, bit 2 -> arriba o abajo, bit 4 -> delante o detrás.
void add_box(float cx, float cy, float cz, float sx, float sy, float sz, uint32_t c) {
    set_center(cx, cy, cz);
    int v[8];
    for (int i = 0; i < 8; i++)
        v[i] = vert(cx + (i & 1 ? sx : -sx) / 2, cy + (i & 2 ? sy : -sy) / 2, cz + (i & 4 ? sz : -sz) / 2);
    quad(v[1], v[3], v[7], v[5], c); // cara derecha
    quad(v[0], v[4], v[6], v[2], c); // izquierda
    quad(v[2], v[6], v[7], v[3], c); // arriba
    quad(v[0], v[1], v[5], v[4], c); // abajo
    quad(v[4], v[5], v[7], v[6], c); // delante
    quad(v[0], v[2], v[3], v[1], c); // detrás
}

// Cilindro: un anillo de puntos abajo y otro arriba, unidos por los lados,
// más dos "tapas". Si el radio de arriba es 0, se convierte en cono.
void add_cyl(float cx, float cy, float cz, float rb, float rt, float h, int sides, uint32_t c) {
    set_center(cx, cy + h / 2, cz);
    int base = nverts;
    for (int i = 0; i < sides; i++) {           // anillo de abajo
        float a = 2 * PI * i / sides;
        vert(cx + m_cos(a) * rb, cy, cz + m_sin(a) * rb);
    }
    int bottom = vert(cx, cy, cz);              // centro de la tapa de abajo
    if (rt > 0) {
        int ring = nverts;
        for (int i = 0; i < sides; i++) {       // anillo de arriba
            float a = 2 * PI * i / sides;
            vert(cx + m_cos(a) * rt, cy + h, cz + m_sin(a) * rt);
        }
        int top = vert(cx, cy + h, cz);
        for (int i = 0; i < sides; i++) {
            int j = (i + 1) % sides;            // el siguiente (el último se une con el primero)
            quad(base + i, base + j, ring + j, ring + i, c); // lado
            tri(top, ring + i, ring + j, c);                 // tapa de arriba
            tri(bottom, base + j, base + i, c);              // tapa de abajo
        }
    } else {
        int top = vert(cx, cy + h, cz);         // punta del cono
        for (int i = 0; i < sides; i++) {
            int j = (i + 1) % sides;
            tri(base + i, base + j, top, c);
            tri(bottom, base + j, base + i, c);
        }
    }
}

// Crea un vértice de elipsoide y le calcula su normal suave.
// En una esfera la normal es simplemente "del centro hacia el punto"; en una
// esfera estirada hay que dividir por los radios al cuadrado para que salga bien.
static int ellipsoid_vert(float cx, float cy, float cz, float rx, float ry, float rz, float x, float y, float z) {
    int i = vert(x, y, z);
    V3 n = v3((x - cx) / (rx * rx), (y - cy) / (ry * ry), (z - cz) / (rz * rz));
    pool_n[i] = v_norm(m_apply_dir(&xf, n));
    pool_smooth[i] = (uint8_t)smooth;
    return i;
}

// Elipsoide (esfera estirada) como un globo terráqueo: un polo arriba, otro
// abajo, y entre medias "rings - 1" anillos horizontales de "seg" puntos.
// Más divisiones = más redonda (y más triángulos).
void add_ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, int seg, int rings, uint32_t c) {
    set_center(cx, cy, cz);
    int top = ellipsoid_vert(cx, cy, cz, rx, ry, rz, cx, cy + ry, cz);    // polo norte
    int bottom = ellipsoid_vert(cx, cy, cz, rx, ry, rz, cx, cy - ry, cz); // polo sur
    int first = nverts;
    for (int r = 1; r < rings; r++) {
        float phi = PI * r / rings;               // "latitud": de arriba (0) a abajo (PI)
        float y = m_cos(phi) * ry, k = m_sin(phi);
        for (int s = 0; s < seg; s++) {
            float th = 2 * PI * s / seg;          // "longitud": vuelta alrededor
            ellipsoid_vert(cx, cy, cz, rx, ry, rz, cx + m_cos(th) * k * rx, cy + y, cz + m_sin(th) * k * rz);
        }
    }
    // IDX(r, s): número del vértice del anillo r, posición s
    #define IDX(r, s) (first + ((r) - 1) * seg + ((s) % seg))
    for (int s = 0; s < seg; s++) {
        tri(top, IDX(1, s), IDX(1, s + 1), c);                       // casquete de arriba
        tri(bottom, IDX(rings - 1, s + 1), IDX(rings - 1, s), c);    // casquete de abajo
    }
    for (int r = 1; r < rings - 1; r++)                               // franjas intermedias
        for (int s = 0; s < seg; s++)
            quad(IDX(r, s), IDX(r, s + 1), IDX(r + 1, s + 1), IDX(r + 1, s), c);
    #undef IDX
}

// Gema: un anillo de puntos con una punta arriba y otra abajo.
void add_gem(float cx, float cy, float cz, float r, float h, int sides, uint32_t c) {
    set_center(cx, cy, cz);
    int top = vert(cx, cy + h, cz);
    int bottom = vert(cx, cy - h, cz);
    int ring = nverts;
    for (int i = 0; i < sides; i++) {
        float a = 2 * PI * i / sides;
        vert(cx + m_cos(a) * r, cy, cz + m_sin(a) * r);
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        tri(top, ring + i, ring + j, c);
        tri(bottom, ring + j, ring + i, c);
    }
}

// Disco plano tumbado (como una moneda en el suelo).
void add_disk(float cx, float cy, float cz, float r, int sides, uint32_t c) {
    set_center(cx, cy - 1, cz); // centro "por debajo": así la normal sale hacia arriba
    int center = vert(cx, cy, cz);
    int ring = nverts;
    for (int i = 0; i < sides; i++) {
        float a = 2 * PI * i / sides;
        vert(cx + m_cos(a) * r, cy, cz + m_sin(a) * r);
    }
    for (int i = 0; i < sides; i++) tri(center, ring + i, ring + (i + 1) % sides, c);
}

// Pasa el modelo al formato de la tarjeta gráfica (10 números por vértice).
int mesh_export(const Mesh *m, float *out, int max_verts) {
    int n = 0;
    for (int t = m->t0; t < m->t0 + m->nt && n + 3 <= max_verts; t++) {
        const int *ix = pool_t[t];
        V3 a = pool_v[ix[0]], b = pool_v[ix[1]], c = pool_v[ix[2]];

        // Normal de la cara: perpendicular a dos de sus lados...
        V3 fn = v_norm(v_cross(v_sub(b, a), v_sub(c, a)));
        // ...pero puede salir hacia dentro o hacia fuera según el orden de los
        // vértices. Lo arreglamos mirando el centro de la pieza: la normal
        // debe apuntar desde ese centro hacia el triángulo (hacia fuera).
        V3 mid = v_mul(v_add(v_add(a, b), c), 1.0f / 3);
        if (v_dot(fn, v_sub(mid, pool_center[t])) < 0) fn = v_mul(fn, -1);

        // Color de 0..255 a 0..1, que es como lo quiere la tarjeta gráfica
        uint32_t col = pool_c[t];
        float r = ((col >> 16) & 255) / 255.0f, g = ((col >> 8) & 255) / 255.0f, bl = (col & 255) / 255.0f;
        for (int k = 0; k < 3; k++) {
            int v = ix[k];
            V3 p = pool_v[v], nn = pool_smooth[v] ? pool_n[v] : fn;
            float *o = out + n * MESH_VERTEX_FLOATS;
            o[0] = p.x;  o[1] = p.y;  o[2] = p.z;   // posición
            o[3] = nn.x; o[4] = nn.y; o[5] = nn.z;  // normal
            o[6] = r;    o[7] = g;    o[8] = bl;  o[9] = 1; // color (a = 1: opaco)
            n++;
        }
    }
    return n;
}
