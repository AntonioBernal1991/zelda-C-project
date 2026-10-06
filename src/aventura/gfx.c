// =============================================================================
// gfx.c  —  El renderizador: dibuja el mundo 3D con la tarjeta gráfica
// =============================================================================
//
// IDEA GENERAL
//   La tarjeta gráfica (GPU) es muy rápida pintando triángulos, pero hay que
//   decirle CÓMO pintarlos. Eso se hace con SHADERS: pequeños programas en un
//   lenguaje llamado GLSL (parecido a C) que se ejecutan dentro de la GPU.
//     - El VERTEX SHADER se ejecuta una vez por cada vértice: decide en qué
//       punto de la pantalla cae (y puede moverlo, como la hierba con viento).
//     - El FRAGMENT SHADER se ejecuta una vez por cada píxel del triángulo:
//       decide su color (luz, sombra, niebla...).
//   Los shaders de este archivo están escritos como textos de C y se
//   compilan al arrancar el juego.
//
// CÓMO SE DIBUJA CADA IMAGEN (en gfx_end)
//   1. SOMBRAS: se dibuja la escena "vista desde el sol" guardando solo la
//      distancia de cada punto al sol (el "mapa de sombras"). Luego, al pintar
//      un píxel, si hay algo más cerca del sol que él, está en sombra.
//   2. ESCENA: cielo de fondo, terreno, modelos y hierba con luz, sombras y
//      niebla. Se pinta en una imagen con ANTIALIASING (MSAA: varias muestras
//      por píxel para que los bordes no tengan "dientes de sierra").
//   3. TRANSPARENTES: el agua y las partículas, encima.
//   4. Se copia esa imagen a la ventana.
//   5. INTERFAZ: los corazones, textos, etc. en pixel art, por encima de todo.
//
// Durante el frame, aventura.c no dibuja directamente: va apuntando lo que
// quiere dibujar (gfx_draw, gfx_particle) y gfx_end lo pinta todo de golpe.
// Así podemos dibujar la escena dos veces (para las sombras y para la vista).
// =============================================================================

#include "glload.h"
#include "gfx.h"
#include "world.h"

#define SHADOW_SIZE    2048    // resolución del mapa de sombras (2048 x 2048)
#define SHADOW_HALF    30.0f   // mitad del área que cubre la sombra (60 x 60 metros)
#define MSAA_SAMPLES   4       // muestras por píxel del antialiasing
#define MAX_GPU_MESHES 128     // modelos que se pueden subir a la GPU
#define MAX_ITEMS      2048    // cosas que se pueden dibujar en un frame
#define MAX_PARTICLES  4096
#define MAX_GRASS      90000   // briznas de hierba
#define TAN_HALF_VFOV  0.4663f // campo de visión vertical de 50 grados (tangente de 25°)
#define Z_NEAR         0.1f    // lo que esté a menos de 10 cm de la cámara no se ve
#define Z_FAR          220.0f  // ni lo que esté a más de 220 m
#define FOG_DENSITY    0.0105f // cuánta niebla hay

// -----------------------------------------------------------------------------
// MATRICES 4x4
// La GPU necesita matrices de 4x4 (nuestras M34 de 3x4 más una fila extra).
// Esa cuarta fila es la que permite hacer la PERSPECTIVA: que lo lejano se vea
// más pequeño. OpenGL las quiere guardadas por columnas: m[columna*4 + fila].
// -----------------------------------------------------------------------------

typedef struct { float m[16]; } Mat4;

// Convierte una M34 nuestra en Mat4 (añade la fila 0 0 0 1)
static Mat4 m4_from34(const M34 *a) {
    Mat4 r;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++)
            r.m[col * 4 + row] = a->m[row][col];
    r.m[3] = r.m[7] = r.m[11] = 0;
    r.m[15] = 1;
    return r;
}

// Multiplicar matrices 4x4 (encadenar transformaciones: primero b, luego a)
static Mat4 m4_mul(Mat4 a, Mat4 b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

// Matriz de PERSPECTIVA: convierte lo que ve la cámara en coordenadas de
// pantalla. Divide x e y por la distancia (lo lejano se encoge) y convierte
// la distancia en un valor de profundidad de -1 (cerca) a 1 (lejos).
// fx, fy = zoom horizontal y vertical; n, f = distancia mínima y máxima.
static Mat4 m4_perspective(float fx, float fy, float n, float f) {
    Mat4 r = {{ 0 }};
    r.m[0] = fx;
    r.m[5] = fy;
    r.m[10] = (f + n) / (f - n);
    r.m[11] = 1;
    r.m[14] = -2 * f * n / (f - n);
    return r;
}

// Matriz ORTOGRÁFICA: como la de perspectiva pero sin encoger lo lejano.
// La usamos para las sombras, porque el sol está tan lejos que sus rayos
// llegan paralelos. hw, hh = mitad del ancho y del alto que abarca.
static Mat4 m4_ortho(float hw, float hh, float n, float f) {
    Mat4 r = {{ 0 }};
    r.m[0] = 1 / hw;
    r.m[5] = 1 / hh;
    r.m[10] = 2 / (f - n);
    r.m[14] = -(f + n) / (f - n);
    r.m[15] = 1;
    return r;
}

// Matriz de VISTA: pasa del mundo a "lo que ve la cámara". Es como mover el
// mundo entero al revés de la cámara: si la cámara está en pos y gira yaw,
// movemos el mundo -pos y lo giramos -yaw. Así la cámara queda en el origen
// mirando hacia +Z.
static M34 view_matrix(V3 pos, float yaw, float pitch) {
    return m_mul(m_rot_x(-pitch), m_mul(m_rot_y(-yaw), m_translate(-pos.x, -pos.y, -pos.z)));
}

// -----------------------------------------------------------------------------
// SHADERS (programas que se ejecutan en la tarjeta gráfica, en lenguaje GLSL)
//
// Palabras de GLSL que verás:
//   vec2/vec3/vec4 = vector de 2/3/4 números      mat4 = matriz 4x4
//   uniform = dato que le pasamos desde C y es igual para todos los píxeles
//   in / out = datos que pasan del vertex shader al fragment shader
//   layout(location=N) in = dato de cada vértice (posición, normal, color)
//   mix(a, b, k) = mezcla: a si k=0, b si k=1, y a medias entre medias
//   smoothstep(a, b, x) = 0 antes de a, 1 después de b, transición suave entre medias
//   dot = producto escalar, normalize = longitud 1, pow = potencia
//   gl_Position = dónde cae el vértice en pantalla (lo que debe calcular el vertex shader)
// -----------------------------------------------------------------------------

#define GLSL_HEADER "#version 330 core\n" // primera línea obligatoria: versión de GLSL

// Funciones comunes que se añaden a todos los fragment shaders:
//   hash/noise/fbm = "ruido": valores aleatorios suaves para hacer texturas
//                    naturales (manchas de hierba, nubes, olas...) sin imágenes.
//                    fbm suma varias capas de ruido de distinto tamaño.
//   skyColor  = color del cielo mirando en una dirección (con el sol).
//   applyFog  = mezcla un color con el del cielo según lo lejos que esté (niebla).
//   finish    = ajuste final de color (como el de las cámaras de cine) y gamma.
//   toLinear  = los colores se guardan "comprimidos" para el ojo (gamma);
//               para calcular luz bien hay que "descomprimirlos" primero.
static const char *GLSL_COMMON =
    "uniform float uTime;\n"
    "uniform vec3 uSunDir;\n"
    "uniform vec3 uCam;\n"
    "uniform float uFogDensity;\n"
    "float hash(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p+45.32); return fract(p.x*p.y); }\n"
    "float noise(vec2 p){ vec2 i = floor(p), f = fract(p); f = f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i), hash(i+vec2(1,0)), f.x), mix(hash(i+vec2(0,1)), hash(i+vec2(1,1)), f.x), f.y); }\n"
    "float fbm(vec2 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a*noise(p); p = p*2.03 + vec2(1.7, 9.2); a *= 0.5; } return s; }\n"
    "vec3 skyColor(vec3 d){\n"
    "  vec3 L = normalize(uSunDir);\n"
    "  float y = clamp(d.y, 0.0, 1.0);\n"
    "  vec3 horizon = vec3(0.48, 0.64, 0.84);\n"
    "  vec3 zenith  = vec3(0.07, 0.20, 0.58);\n"
    "  vec3 c = mix(horizon, zenith, pow(y, 0.45));\n"
    "  float s = max(dot(d, L), 0.0);\n"
    "  c += vec3(1.0, 0.82, 0.55) * (pow(s, 900.0)*25.0 + pow(s, 10.0)*0.35);\n"
    "  if (d.y < 0.0) c = mix(horizon, vec3(0.42, 0.50, 0.44), clamp(-d.y*3.0, 0.0, 1.0));\n"
    "  return c;\n"
    "}\n"
    "vec3 applyFog(vec3 col, vec3 world){\n"
    "  vec3 d = world - uCam; float dist = length(d);\n"
    "  float f = 1.0 - exp(-pow(dist*uFogDensity, 1.6));\n"
    "  vec3 fc = skyColor(normalize(vec3(d.x, max(d.y, 0.0)*0.2, d.z)));\n"
    "  return mix(col, fc, f);\n"
    "}\n"
    "vec3 finish(vec3 c){\n"                       // tono cinematográfico (ACES) y gamma
    "  c *= 0.72;\n"
    "  c = (c*(2.51*c + 0.03)) / (c*(2.43*c + 0.59) + 0.14);\n"
    "  return pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2));\n"
    "}\n"
    "vec3 toLinear(vec3 c){ return pow(c, vec3(2.2)); }\n";

// --- ESCENA: un mismo programa pinta modelos (modo 0), terreno (1) y hierba (2).
// Vertex shader: coloca cada vértice en el mundo (con uModel), lo proyecta a
// la pantalla (uViewProj) y calcula dónde cae en el mapa de sombras (uLightVP).
// Para la hierba (modo 2) además la mueve con el viento: la punta de cada
// brizna (aColor.a = 1) se mueve y la base (aColor.a = 0) se queda quieta.
static const char *SCENE_VS =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform mat4 uModel, uViewProj, uLightVP;\n"
    "uniform int uMode;\n"
    "uniform float uTime;\n"
    "uniform vec3 uPlayer;\n"
    "out vec3 vWorld; out vec3 vNormal; out vec4 vColor; out vec4 vLight;\n"
    "void main(){\n"
    "  vec4 w = uModel * vec4(aPos, 1.0);\n"
    "  if (uMode == 2) {\n"                       // hierba: viento y el héroe la aparta
    "    float k = aColor.a * aColor.a;\n"
    "    float gust = sin(uTime*0.7 + w.x*0.08 + w.z*0.05)*0.5 + 0.5;\n"
    "    float wave = sin(uTime*2.2 + w.x*0.45 + w.z*0.3) * (0.5 + gust);\n"
    "    w.xz += vec2(0.16, 0.07) * wave * k;\n"
    "    vec2 d = w.xz - uPlayer.xz; float dist = length(d);\n"
    "    if (dist < 1.1) { w.xz += normalize(d + vec2(0.0001)) * (1.1 - dist) * 0.55 * k; w.y -= (1.1 - dist) * 0.25 * k; }\n"
    "  }\n"
    "  vWorld = w.xyz;\n"
    "  vNormal = mat3(uModel) * aNormal;\n"
    "  vColor = aColor;\n"
    "  vLight = uLightVP * w;\n"
    "  gl_Position = uViewProj * w;\n"
    "}\n";

// Fragment shader de la escena: el color final de cada píxel.
//   shadowAt     = mira el mapa de sombras (9 muestras alrededor para que el
//                  borde de la sombra sea suave).
//   terrainColor = color del suelo calculado píxel a píxel: hierba con manchas,
//                  arena cerca del agua, roca en las cuestas y baldosas con
//                  musgo en las ruinas.
//   main         = luz del sol (más fuerte cuanto más de frente le da) +
//                  luz ambiente del cielo + brillo de borde + reflejo (gloss)
//                  + destello al recibir un golpe + niebla.
static const char *SCENE_FS =
    "in vec3 vWorld; in vec3 vNormal; in vec4 vColor; in vec4 vLight;\n"
    "uniform sampler2DShadow uShadow;\n"
    "uniform int uMode;\n"
    "uniform vec4 uFlash;\n"
    "uniform float uGloss;\n"
    "uniform float uWater;\n"
    "out vec4 frag;\n"
    "float shadowAt(vec3 n, vec3 L){\n"
    "  vec3 p = vLight.xyz / vLight.w * 0.5 + 0.5;\n"
    "  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n"
    "  float bias = 0.0008 + 0.0015*(1.0 - max(dot(n, L), 0.0));\n"
    "  vec2 texel = 1.0 / vec2(textureSize(uShadow, 0));\n"
    "  float s = 0.0;\n"
    "  for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)\n"
    "    s += texture(uShadow, vec3(p.xy + vec2(x, y)*texel*1.2, p.z - bias));\n"
    "  return s / 9.0;\n"
    "}\n"
    "vec3 terrainColor(vec3 n){\n"
    "  vec2 p = vWorld.xz; float h = vWorld.y; float slope = 1.0 - n.y;\n"
    "  float v = fbm(p*0.45);\n"
    "  vec3 grass = mix(vec3(0.24, 0.50, 0.15), vec3(0.38, 0.62, 0.20), v);\n"
    "  grass = mix(grass, vec3(0.26, 0.50, 0.19), smoothstep(0.5, 0.75, fbm(p*0.07 + 3.0)));\n"
    "  grass = mix(grass, vec3(0.55, 0.62, 0.30), smoothstep(0.62, 0.8, fbm(p*0.12 + 7.0))*0.6);\n"
    "  vec3 sand = mix(vec3(0.83, 0.75, 0.52), vec3(0.74, 0.66, 0.45), fbm(p*1.7));\n"
    "  vec3 rock = mix(vec3(0.55, 0.52, 0.47), vec3(0.40, 0.38, 0.35), fbm(p*1.1 + vec2(0.0, h*0.6)));\n"
    "  vec3 c = grass;\n"
    "  c = mix(c, sand, 1.0 - smoothstep(uWater + 0.08, uWater + 0.45, h));\n"
    "  c = mix(c, rock, smoothstep(0.32, 0.5, slope + (v - 0.5)*0.15));\n"
    "  float r = length(p);\n"
    "  if (r < 8.0) {\n"                           // baldosas de las ruinas
    "    vec2 t = p / 1.25; vec2 f = fract(t); vec2 id = floor(t);\n"
    "    float gap = smoothstep(0.015, 0.04, f.x) * smoothstep(0.015, 0.04, f.y) * smoothstep(0.015, 0.04, 1.0-f.x) * smoothstep(0.015, 0.04, 1.0-f.y);\n"
    "    vec3 stone = mix(vec3(0.66, 0.62, 0.53), vec3(0.54, 0.51, 0.45), hash(id));\n"
    "    stone *= 0.82 + 0.25*fbm(p*2.5);\n"
    "    vec3 moss = vec3(0.30, 0.48, 0.20);\n"
    "    vec3 tile = mix(vec3(0.30, 0.29, 0.26), stone, gap);\n"
    "    tile = mix(tile, moss, smoothstep(0.66, 0.85, fbm(p*0.9 + 11.0))*0.6);\n"
    "    float blend = smoothstep(7.8, 6.6, r + (fbm(p*0.7) - 0.5)*2.2);\n"
    "    c = mix(c, tile, blend);\n"
    "  }\n"
    "  return c;\n"
    "}\n"
    "void main(){\n"
    "  vec3 n = normalize(vNormal);\n"
    "  vec3 L = normalize(uSunDir);\n"
    "  vec3 V = normalize(uCam - vWorld);\n"
    "  vec3 albedo = uMode == 1 ? toLinear(terrainColor(n)) : toLinear(vColor.rgb);\n"
    "  float sh = shadowAt(n, L);\n"
    "  float ndl = max(dot(n, L), 0.0);\n"
    "  if (uMode == 2) {\n"                        // la hierba es fina: deja pasar la luz
    "    albedo *= mix(0.45, 1.15, vColor.a);\n"
    "    ndl = 0.55 + 0.45*ndl;\n"
    "  }\n"
    "  vec3 sun = vec3(1.0, 0.92, 0.78) * 2.3;\n"
    "  vec3 ambient = mix(vec3(0.10, 0.08, 0.06), vec3(0.24, 0.32, 0.48), n.y*0.5 + 0.5);\n"
    "  vec3 col = albedo * (ambient + sun * ndl * sh);\n"
    "  if (uMode == 0) {\n"
    "    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0);\n"
    "    col += albedo * rim * (0.25 + 0.5*sh) * vec3(0.9, 0.95, 1.0);\n"
    "  }\n"
    "  if (uGloss > 0.0) {\n"
    "    vec3 H = normalize(L + V);\n"
    "    col += vec3(1.0, 0.95, 0.85) * pow(max(dot(n, H), 0.0), 60.0) * uGloss * sh * 2.0;\n"
    "  }\n"
    "  col = mix(col, toLinear(uFlash.rgb) * 2.0, uFlash.a);\n"
    "  col = applyFog(col, vWorld);\n"
    "  frag = vec4(finish(col), 1.0);\n"
    "}\n";

// --- SOMBRAS: solo nos interesa la distancia al sol, no el color.
//     Por eso el fragment shader está vacío.
static const char *SHADOW_VS =
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uModel, uLightVP;\n"
    "void main(){ gl_Position = uLightVP * uModel * vec4(aPos, 1.0); }\n";
static const char *SHADOW_FS = "void main(){}\n";

// --- CIELO: un único triángulo gigante que tapa toda la pantalla (los 3
// vértices se inventan a partir de gl_VertexID, sin datos). Para cada píxel
// se calcula hacia dónde mira la cámara en ese punto y se pide el color del
// cielo en esa dirección. Las nubes son ruido (fbm) proyectado en el cielo y
// desplazado con el tiempo.
static const char *SKY_VS =
    "out vec2 vNdc;\n"
    "void main(){\n"
    "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  vNdc = p; gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";
static const char *SKY_FS =
    "in vec2 vNdc;\n"
    "uniform vec3 uRight, uUp, uFwd;\n"
    "uniform vec2 uScale;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec3 d = normalize(uFwd + uRight*vNdc.x*uScale.x + uUp*vNdc.y*uScale.y);\n"
    "  vec3 c = skyColor(d);\n"
    "  if (d.y > 0.0) {\n"                          // nubes que se mueven
    "    vec2 uv = d.xz / (d.y + 0.12) * 0.55 + vec2(uTime*0.010, uTime*0.004);\n"
    "    float n = fbm(uv*1.3);\n"
    "    float cl = smoothstep(0.56, 0.82, n);\n"
    "    vec3 cc = mix(vec3(0.78, 0.82, 0.92), vec3(1.15, 1.10, 1.02), smoothstep(0.5, 0.95, n));\n"
    "    c = mix(c, cc, cl * smoothstep(0.0, 0.25, d.y) * 0.9);\n"
    "  }\n"
    "  frag = vec4(finish(c), 1.0);\n"
    "}\n";

// --- AGUA: superficie plana con pequeñas olas.
//   Vertex shader: sube y baja los vértices con senos.
//   Fragment shader: inventa la inclinación de las olas con ruido, y con ella
//   mezcla el color del fondo del lago con el reflejo del cielo (efecto
//   Fresnel: mirando de lado se refleja más, mirando desde arriba se ve el
//   fondo). Añade el brillo del sol y espuma en la orilla (donde hay poca
//   profundidad). aColor.a trae la profundidad del agua en cada punto.
static const char *WATER_VS =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "uniform float uTime;\n"
    "out vec3 vWorld; out float vDepth;\n"
    "void main(){\n"
    "  vec3 p = aPos;\n"
    "  p.y += (sin(p.x*0.8 + uTime*1.3) + sin(p.z*0.6 - uTime*1.1)) * 0.025;\n"
    "  vWorld = p; vDepth = aColor.a;\n"
    "  gl_Position = uViewProj * vec4(p, 1.0);\n"
    "}\n";
static const char *WATER_FS =
    "in vec3 vWorld; in float vDepth;\n"
    "out vec4 frag;\n"
    "float waves(vec2 p){ return fbm(p*0.9 + vec2(uTime*0.25, uTime*0.18)) + 0.5*fbm(p*2.3 - vec2(uTime*0.35, 0.0)); }\n"
    "void main(){\n"
    "  vec2 p = vWorld.xz; float e = 0.05;\n"
    "  float h0 = waves(p);\n"
    "  vec3 n = normalize(vec3(-(waves(p + vec2(e, 0.0)) - h0)/e*0.12, 1.0, -(waves(p + vec2(0.0, e)) - h0)/e*0.12));\n"
    "  vec3 V = normalize(uCam - vWorld);\n"
    "  vec3 L = normalize(uSunDir);\n"
    "  float fres = 0.03 + 0.97*pow(1.0 - max(dot(n, V), 0.0), 5.0);\n"
    "  vec3 refl = skyColor(reflect(-V, n));\n"
    "  vec3 body = mix(vec3(0.07, 0.40, 0.42), vec3(0.01, 0.08, 0.18), smoothstep(0.0, 1.0, vDepth));\n"
    "  vec3 col = mix(body, refl, fres);\n"
    "  col += vec3(1.0, 0.9, 0.7) * pow(max(dot(reflect(-L, n), V), 0.0), 220.0) * 4.0;\n"
    "  float foam = smoothstep(0.14, 0.0, vDepth) * (0.55 + 0.45*noise(p*7.0 + uTime*0.8));\n"
    "  col = mix(col, vec3(0.95, 0.97, 1.0), foam*0.75);\n"
    "  float alpha = max(mix(0.55, 0.93, smoothstep(0.0, 0.7, vDepth)), foam);\n"
    "  col = applyFog(col, vWorld);\n"
    "  frag = vec4(finish(col), alpha);\n"
    "}\n";

// --- PARTÍCULAS: cada una es un solo punto que la GPU dibuja como un
// cuadrado; el fragment shader lo recorta en círculo. Las "brillantes" (glow)
// suman su luz a lo que hay detrás, por eso parecen chispas de luz. El tamaño
// se divide por la distancia (gl_Position.w) para que lo lejano sea pequeño.
static const char *PART_VS =
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in float aSize;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "uniform float uPointScale;\n"
    "out vec4 vColor;\n"
    "void main(){\n"
    "  gl_Position = uViewProj * vec4(aPos, 1.0);\n"
    "  gl_PointSize = max(aSize * uPointScale / gl_Position.w, 1.5);\n"
    "  vColor = aColor;\n"
    "}\n";
static const char *PART_FS =
    "in vec4 vColor;\n"
    "uniform int uGlow;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec2 d = gl_PointCoord*2.0 - 1.0; float r = dot(d, d);\n"
    "  if (r > 1.0) discard;\n"
    "  if (uGlow == 1) { float a = 1.0 - r; a = a*a; frag = vec4(vColor.rgb * a * vColor.a * 1.3, 1.0); }\n"
    "  else frag = vec4(vColor.rgb * (1.0 - 0.3*r), vColor.a);\n"
    "}\n";

// --- INTERFAZ: un triángulo que tapa la pantalla con la imagen de la
// interfaz (320x180) estirada. Los píxeles "transparentes" se descartan.
static const char *OVERLAY_VS =
    "out vec2 vUv;\n"
    "void main(){\n"
    "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  vUv = vec2(p.x*0.5 + 0.5, 0.5 - p.y*0.5);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";
static const char *OVERLAY_FS =
    "in vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 frag;\n"
    "void main(){ vec4 c = texture(uTex, vUv); if (c.a < 0.5) discard; frag = vec4(c.rgb, 1.0); }\n";

// -----------------------------------------------------------------------------
// ESTADO
//
// Palabras de OpenGL:
//   VBO (vertex buffer object) = memoria DENTRO de la tarjeta gráfica donde
//        guardamos los vértices de un modelo. Se sube una vez y se reutiliza.
//   VAO (vertex array object)  = la "ficha" que explica cómo leer ese VBO
//        (dónde está la posición, la normal y el color de cada vértice).
//   FBO (framebuffer object)   = una imagen donde dibujar que no es la
//        ventana (la usamos para el mapa de sombras y para el antialiasing).
//   Textura = imagen guardada en la GPU que los shaders pueden leer.
// Todos estos objetos se identifican con un número (GLuint).
// -----------------------------------------------------------------------------

typedef struct { GLuint vao, vbo; int count; } GMesh; // un modelo ya subido a la GPU

// Una cosa que dibujar este frame: qué modelo, dónde (model), si brilla
// blanco por un golpe (flash), cuánto reflejo tiene (gloss) y si da sombra.
typedef struct { int mesh; Mat4 model; float flash[4]; float gloss; int shadow; } Item;

static char errbuf[2048];
static GMesh meshes[MAX_GPU_MESHES];
static int nmeshes;
static GMesh terrain, water, grass;
static GLuint vao_empty, part_vao, part_vbo, overlay_tex;
static GLuint shadow_fbo, shadow_tex;
static GLuint msaa_fbo, msaa_color, msaa_depth;
static int msaa_w, msaa_h;

static Item items[MAX_ITEMS];
static int nitems;
static float parts_glow[MAX_PARTICLES * 8], parts_solid[MAX_PARTICLES * 8];
static int nglow, nsolid;

static GLuint prog_scene, prog_shadow, prog_sky, prog_water, prog_part, prog_overlay; // los shaders ya compilados

// Datos del frame actual (cámara, sol, matrices...)
static struct {
    int w, h;
    float fx, fy, time;
    Mat4 vp, lvp;
    V3 cam, focus, right, up, fwd, sun;
} F;

static float scratch[MAX_GRASS * 3 * MESH_VERTEX_FLOATS]; // memoria temporal para preparar geometría antes de subirla

const char *gfx_error(void) { return errbuf[0] ? errbuf : 0; }

static void set_error(const char *prefix, const char *log) {
    int n = 0;
    while (prefix[n] && n < (int)sizeof errbuf - 1) { errbuf[n] = prefix[n]; n++; }
    for (int i = 0; log[i] && n < (int)sizeof errbuf - 1; i++) errbuf[n++] = log[i];
    errbuf[n] = 0;
}

// Compila un shader. Si tiene un error, guarda el mensaje para enseñarlo.
// Se le pasan tres trozos de texto: la versión, las funciones comunes y el shader.
static GLuint compile(GLenum type, const char *common, const char *src) {
    const char *parts[3] = { GLSL_HEADER, common, src };
    GLuint s = glCreateShader(type);
    glShaderSource(s, 3, parts, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1500];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        set_error(type == GL_VERTEX_SHADER ? "Error en vertex shader:\n" : "Error en fragment shader:\n", log);
        return 0;
    }
    return s;
}

// Une un vertex shader y un fragment shader en un "programa" listo para usar.
static GLuint program(const char *vs, const char *fs) {
    GLuint v = compile(GL_VERTEX_SHADER, "", vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, GLSL_COMMON, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1500];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        set_error("Error al enlazar shaders:\n", log);
        return 0;
    }
    return p;
}

// Busca un "uniform" (dato que le pasamos al shader) por su nombre.
static GLint U(GLuint p, const char *name) { return glGetUniformLocation(p, name); }

// Sube una lista de vértices a la GPU (VBO) y explica su formato (VAO):
//   atributo 0 = posición (3 números), 1 = normal (3), 2 = color (4).
static GMesh upload(const float *data, int nverts) {
    GMesh g;
    glGenVertexArrays(1, &g.vao);
    glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nverts * MESH_VERTEX_FLOATS * sizeof(float), data, GL_STATIC_DRAW);
    GLsizei stride = MESH_VERTEX_FLOATS * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    g.count = nverts;
    return g;
}

// Sube un modelo de mesh.c y devuelve su número para dibujarlo luego.
int gfx_mesh(const Mesh *m) {
    if (nmeshes >= MAX_GPU_MESHES) return 0;
    int n = mesh_export(m, scratch, MAX_GRASS * 3);
    meshes[nmeshes] = upload(scratch, n);
    return nmeshes++;
}

// -----------------------------------------------------------------------------
// GEOMETRÍA DEL MUNDO (se construye una vez al empezar)
// -----------------------------------------------------------------------------

// Escribe un vértice (10 números) y devuelve dónde va el siguiente.
static float *put_vertex(float *o, V3 p, V3 n, float r, float g, float b, float a) {
    o[0] = p.x; o[1] = p.y; o[2] = p.z;
    o[3] = n.x; o[4] = n.y; o[5] = n.z;
    o[6] = r;   o[7] = g;   o[8] = b;   o[9] = a;
    return o + MESH_VERTEX_FLOATS;
}

static V3 grid_point(int i, int j) {
    float c = world_grid_cell(), o = world_grid_origin();
    return v3(o + i * c, world_grid_height(i, j), o + j * c);
}

// Normal suave del terreno en una esquina de la rejilla: se mira la altura
// de los vecinos para saber la inclinación.
static V3 grid_normal(int i, int j) {
    int n = world_grid_n();
    int i0 = i > 0 ? i - 1 : i, i1 = i < n ? i + 1 : i;
    int j0 = j > 0 ? j - 1 : j, j1 = j < n ? j + 1 : j;
    float c = world_grid_cell();
    float hx = (world_grid_height(i1, j) - world_grid_height(i0, j)) / ((i1 - i0) * c);
    float hz = (world_grid_height(i, j1) - world_grid_height(i, j0)) / ((j1 - j0) * c);
    return v_norm(v3(-hx, 1, -hz));
}

// Terreno: dos triángulos por casilla de la rejilla de world.c. El color no
// va aquí: lo calcula el shader píxel a píxel (terrainColor).
static void build_terrain(void) {
    int n = world_grid_n();
    float *o = scratch;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            // mismos dos triángulos por celda que usa world_height
            int ids[6][2] = { {i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j}, {i + 1, j + 1}, {i, j + 1} };
            for (int k = 0; k < 6; k++)
                o = put_vertex(o, grid_point(ids[k][0], ids[k][1]), grid_normal(ids[k][0], ids[k][1]), 1, 1, 1, 1);
        }
    terrain = upload(scratch, (int)((o - scratch) / MESH_VERTEX_FLOATS));
}

// Agua: cuadrados planos a la altura del lago, solo en las casillas donde
// el suelo queda por debajo del agua (más un margen). En el canal "a" del
// color guardamos la profundidad, que el shader usa para el color y la espuma.
static void build_water(void) {
    int n = world_grid_n();
    float *o = scratch;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            float lo = 1e9f;
            for (int a = -1; a <= 2; a++)       // celda con un margen alrededor
                for (int b = -1; b <= 2; b++) {
                    int ii = i + a, jj = j + b;
                    if (ii < 0 || jj < 0 || ii > n || jj > n) continue;
                    float h = world_grid_height(ii, jj);
                    if (h < lo) lo = h;
                }
            if (lo > WATER_Y) continue;
            int ids[6][2] = { {i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j}, {i + 1, j + 1}, {i, j + 1} };
            for (int k = 0; k < 6; k++) {
                V3 p = grid_point(ids[k][0], ids[k][1]);
                float depth = (WATER_Y - p.y) / 1.5f; // profundidad: color y espuma de la orilla
                p.y = WATER_Y;
                o = put_vertex(o, p, v3(0, 1, 0), 1, 1, 1, depth);
            }
        }
    water = upload(scratch, (int)((o - scratch) / MESH_VERTEX_FLOATS));
}

// Hierba: miles de briznas, cada una un triángulo fino (dos puntos en la
// base y uno en la punta). Se colocan al azar sobre el suelo, evitando agua,
// cuestas, ruinas y obstáculos. Hay zonas de pradera más densas y altas.
static void build_grass(void) {
    uint32_t s = 0xabcdef12u;
    float *o = scratch;
    int count = 0;
    for (int tries = 0; tries < MAX_GRASS * 6 && count < MAX_GRASS; tries++) {
        float x = rng_range(&s, -29.5f, 29.5f), z = rng_range(&s, -29.5f, 29.5f);
        float h = world_height(x, z);
        if (h < WATER_Y + 0.22f || x * x + z * z < 7.6f * 7.6f) continue;
        V3 n = world_normal(x, z);
        if (n.y < 0.82f) continue;
        float tx = x, tz = z;
        if (world_collide(&tx, &tz, 0.02f)) continue;
        // praderas más densas y más altas en unas zonas que en otras
        float dens = 0.5f + 0.5f * m_sin(x * 0.21f + 1.0f) * m_cos(z * 0.17f + 0.3f);
        if (rng_range(&s, 0, 1) > 0.25f + 0.75f * dens) continue;

        float a = rng_range(&s, 0, 2 * PI), w = rng_range(&s, 0.035f, 0.065f);
        float hgt = rng_range(&s, 0.18f, 0.42f) * (0.7f + 0.7f * dens);
        float la = rng_range(&s, 0, 2 * PI), lean = rng_range(&s, 0.02f, 0.12f);
        V3 b1 = v3(x + m_cos(a) * w, h - 0.03f, z + m_sin(a) * w);
        V3 b2 = v3(x - m_cos(a) * w, h - 0.03f, z - m_sin(a) * w);
        V3 tip = v3(x + m_cos(la) * lean, h + hgt, z + m_sin(la) * lean);

        float t = rng_range(&s, 0, 1);
        float r = 0.24f + 0.12f * t, g = 0.50f + 0.12f * t, b = 0.12f + 0.06f * t;
        float tr = r, tg = g, tb = b;
        if (rng_range(&s, 0, 1) < 0.12f) { tr = 0.62f; tg = 0.64f; tb = 0.26f; } // puntas secas
        o = put_vertex(o, b1, n, r, g, b, 0);
        o = put_vertex(o, b2, n, r, g, b, 0);
        o = put_vertex(o, tip, n, tr, tg, tb, 1);
        count++;
    }
    grass = upload(scratch, count * 3);
}

// Prepara (o rehace si cambió el tamaño de la ventana) la imagen con
// antialiasing donde se pinta la escena.
static void ensure_msaa(int w, int h) {
    if (w == msaa_w && h == msaa_h) return;
    if (msaa_fbo) {
        glDeleteRenderbuffers(1, &msaa_color);
        glDeleteRenderbuffers(1, &msaa_depth);
    } else {
        glGenFramebuffers(1, &msaa_fbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
    glGenRenderbuffers(1, &msaa_color);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_RGBA8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color);
    glGenRenderbuffers(1, &msaa_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth);
    msaa_w = w;
    msaa_h = h;
}

// Se llama una vez: compila los shaders y prepara todo en la GPU.
int gfx_init(void) {
    prog_scene   = program(SCENE_VS, SCENE_FS);
    prog_shadow  = program(SHADOW_VS, SHADOW_FS);
    prog_sky     = program(SKY_VS, SKY_FS);
    prog_water   = program(WATER_VS, WATER_FS);
    prog_part    = program(PART_VS, PART_FS);
    prog_overlay = program(OVERLAY_VS, OVERLAY_FS);
    if (!prog_scene || !prog_shadow || !prog_sky || !prog_water || !prog_part || !prog_overlay) return 0;

    build_terrain();
    build_water();
    build_grass();

    glGenVertexArrays(1, &vao_empty);

    // Partículas: buffer dinámico con posición (3), tamaño (1) y color (4)
    glGenVertexArrays(1, &part_vao);
    glBindVertexArray(part_vao);
    glGenBuffers(1, &part_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, part_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    // Mapa de sombras: textura de profundidad con comparación por hardware
    glGenTextures(1, &shadow_tex);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        set_error("No se pudo crear el mapa de sombras", "");
        return 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glGenTextures(1, &overlay_tex);
    glBindTexture(GL_TEXTURE_2D, overlay_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return 1;
}

// -----------------------------------------------------------------------------
// CADA FRAME
// -----------------------------------------------------------------------------

// Empieza un frame: calcula la cámara, la del sol y vacía las listas.
void gfx_begin(int width, int height, V3 cam, float yaw, float pitch, V3 focus, float time) {
    F.w = width;
    F.h = height;
    F.time = time;
    F.cam = cam;
    F.focus = focus;
    F.fy = 1 / TAN_HALF_VFOV;
    F.fx = F.fy * height / width;
    F.sun = v_norm(v3(-0.55f, 0.8f, -0.42f));

    M34 view = view_matrix(cam, yaw, pitch);
    // Las filas de la rotación de la cámara son sus ejes en el mundo
    F.right = v3(view.m[0][0], view.m[0][1], view.m[0][2]);
    F.up    = v3(view.m[1][0], view.m[1][1], view.m[1][2]);
    F.fwd   = v3(view.m[2][0], view.m[2][1], view.m[2][2]);
    F.vp = m4_mul(m4_perspective(F.fx, F.fy, Z_NEAR, Z_FAR), m4_from34(&view));

    // Cámara del sol para las sombras, centrada en el héroe y ajustada a la
    // rejilla de texels para que los bordes no tiemblen al moverse
    V3 fwd = v_mul(F.sun, -1);
    float lyaw = m_atan2(fwd.x, fwd.z);
    float lpitch = m_atan2(-fwd.y, m_sqrt(fwd.x * fwd.x + fwd.z * fwd.z));
    M34 lrot = m_mul(m_rot_x(-lpitch), m_rot_y(-lyaw));
    V3 lf = m_apply(&lrot, focus);
    float texel = 2 * SHADOW_HALF / SHADOW_SIZE;
    lrot.m[0][3] = -(float)(int)(lf.x / texel) * texel;
    lrot.m[1][3] = -(float)(int)(lf.y / texel) * texel;
    lrot.m[2][3] = -lf.z + 70;
    F.lvp = m4_mul(m4_ortho(SHADOW_HALF, SHADOW_HALF, 1, 160), m4_from34(&lrot));

    nitems = 0;
    nglow = nsolid = 0;
}

// Apunta un modelo para dibujarlo en este frame.
void gfx_draw(int mesh, const M34 *model, uint32_t flash, float gloss, int casts_shadow) {
    if (nitems >= MAX_ITEMS) return;
    Item *it = &items[nitems++];
    it->mesh = mesh;
    it->model = m4_from34(model);
    it->flash[0] = ((flash >> 16) & 255) / 255.0f;
    it->flash[1] = ((flash >> 8) & 255) / 255.0f;
    it->flash[2] = (flash & 255) / 255.0f;
    it->flash[3] = flash ? 0.85f : 0;
    it->gloss = gloss;
    it->shadow = casts_shadow;
}

// Apunta una partícula (en la lista de brillantes o en la de normales).
void gfx_particle(V3 pos, float size, uint32_t color, float alpha, int glow) {
    float *buf = glow ? parts_glow : parts_solid;
    int *n = glow ? &nglow : &nsolid;
    if (*n >= MAX_PARTICLES) return;
    float *o = buf + *n * 8;
    o[0] = pos.x; o[1] = pos.y; o[2] = pos.z; o[3] = size;
    o[4] = ((color >> 16) & 255) / 255.0f;
    o[5] = ((color >> 8) & 255) / 255.0f;
    o[6] = (color & 255) / 255.0f;
    o[7] = alpha;
    (*n)++;
}

static const float IDENTITY[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

// Datos que necesitan casi todos los shaders.
static void common_uniforms(GLuint p) {
    glUniform1f(U(p, "uTime"), F.time);
    glUniform3f(U(p, "uSunDir"), F.sun.x, F.sun.y, F.sun.z);
    glUniform3f(U(p, "uCam"), F.cam.x, F.cam.y, F.cam.z);
    glUniform1f(U(p, "uFogDensity"), FOG_DENSITY);
}

// Sube las partículas del frame a la GPU y las dibuja como puntos.
static void draw_particles(const float *data, int n, int glow) {
    if (n == 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, part_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * 8 * sizeof(float), data, GL_STREAM_DRAW);
    glUniform1i(U(prog_part, "uGlow"), glow);
    if (glow) glBlendFunc(GL_ONE, GL_ONE);                       // suman luz
    else      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_POINTS, 0, n);
}

// Termina el frame: dibuja de verdad todo lo apuntado, paso a paso.
void gfx_end(const uint32_t *overlay, int ow, int oh) {
    // 1) Sombras: la escena vista desde el sol, solo profundidad
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    glUseProgram(prog_shadow);
    glUniformMatrix4fv(U(prog_shadow, "uLightVP"), 1, GL_FALSE, F.lvp.m);
    GLint su_model = U(prog_shadow, "uModel");
    glUniformMatrix4fv(su_model, 1, GL_FALSE, IDENTITY);
    glBindVertexArray(terrain.vao);
    glDrawArrays(GL_TRIANGLES, 0, terrain.count);
    for (int i = 0; i < nitems; i++) {
        if (!items[i].shadow) continue;
        glUniformMatrix4fv(su_model, 1, GL_FALSE, items[i].model.m);
        glBindVertexArray(meshes[items[i].mesh].vao);
        glDrawArrays(GL_TRIANGLES, 0, meshes[items[i].mesh].count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // 2) Escena en un buffer con antialiasing
    ensure_msaa(F.w, F.h);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
    glViewport(0, 0, F.w, F.h);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Cielo de fondo
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(prog_sky);
    common_uniforms(prog_sky);
    glUniform3f(U(prog_sky, "uRight"), F.right.x, F.right.y, F.right.z);
    glUniform3f(U(prog_sky, "uUp"), F.up.x, F.up.y, F.up.z);
    glUniform3f(U(prog_sky, "uFwd"), F.fwd.x, F.fwd.y, F.fwd.z);
    glUniform2f(U(prog_sky, "uScale"), 1 / F.fx, 1 / F.fy);
    glBindVertexArray(vao_empty);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // Terreno, modelos y hierba
    glUseProgram(prog_scene);
    common_uniforms(prog_scene);
    glUniformMatrix4fv(U(prog_scene, "uViewProj"), 1, GL_FALSE, F.vp.m);
    glUniformMatrix4fv(U(prog_scene, "uLightVP"), 1, GL_FALSE, F.lvp.m);
    glUniform3f(U(prog_scene, "uPlayer"), F.focus.x, F.focus.y, F.focus.z);
    glUniform1f(U(prog_scene, "uWater"), WATER_Y);
    glUniform1i(U(prog_scene, "uShadow"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    GLint u_model = U(prog_scene, "uModel"), u_mode = U(prog_scene, "uMode");
    GLint u_flash = U(prog_scene, "uFlash"), u_gloss = U(prog_scene, "uGloss");

    glUniformMatrix4fv(u_model, 1, GL_FALSE, IDENTITY);
    glUniform4f(u_flash, 0, 0, 0, 0);
    glUniform1f(u_gloss, 0);
    glUniform1i(u_mode, 1);
    glBindVertexArray(terrain.vao);
    glDrawArrays(GL_TRIANGLES, 0, terrain.count);

    glUniform1i(u_mode, 0);
    for (int i = 0; i < nitems; i++) {
        Item *it = &items[i];
        glUniformMatrix4fv(u_model, 1, GL_FALSE, it->model.m);
        glUniform4f(u_flash, it->flash[0], it->flash[1], it->flash[2], it->flash[3]);
        glUniform1f(u_gloss, it->gloss);
        glBindVertexArray(meshes[it->mesh].vao);
        glDrawArrays(GL_TRIANGLES, 0, meshes[it->mesh].count);
    }

    glUniformMatrix4fv(u_model, 1, GL_FALSE, IDENTITY);
    glUniform4f(u_flash, 0, 0, 0, 0);
    glUniform1f(u_gloss, 0);
    glUniform1i(u_mode, 2);
    glBindVertexArray(grass.vao);
    glDrawArrays(GL_TRIANGLES, 0, grass.count);

    // Agua transparente
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(prog_water);
    common_uniforms(prog_water);
    glUniformMatrix4fv(U(prog_water, "uViewProj"), 1, GL_FALSE, F.vp.m);
    glBindVertexArray(water.vao);
    glDrawArrays(GL_TRIANGLES, 0, water.count);

    // Partículas
    glEnable(GL_PROGRAM_POINT_SIZE);
    glUseProgram(prog_part);
    glUniformMatrix4fv(U(prog_part, "uViewProj"), 1, GL_FALSE, F.vp.m);
    glUniform1f(U(prog_part, "uPointScale"), F.fy * F.h * 0.5f);
    glBindVertexArray(part_vao);
    draw_particles(parts_solid, nsolid, 0);
    draw_particles(parts_glow, nglow, 1);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // 3) Pasar la imagen con antialiasing a la ventana
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, F.w, F.h, 0, 0, F.w, F.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 4) Interfaz en pixel art, escalada con un factor entero y centrada
    static uint32_t rgba[640 * 360];
    if (overlay && ow * oh <= 640 * 360) {
        for (int i = 0; i < ow * oh; i++)
            rgba[i] = overlay[i] == GFX_OVERLAY_KEY ? 0 : (overlay[i] | 0xff000000u);
        glBindTexture(GL_TEXTURE_2D, overlay_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_BGRA, GL_UNSIGNED_BYTE, rgba);
        int k = F.w / ow < F.h / oh ? F.w / ow : F.h / oh;
        if (k < 1) k = 1;
        glViewport((F.w - ow * k) / 2, (F.h - oh * k) / 2, ow * k, oh * k);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(prog_overlay);
        glUniform1i(U(prog_overlay, "uTex"), 0);
        glBindVertexArray(vao_empty);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
    }
}
