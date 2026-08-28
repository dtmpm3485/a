#include <jni.h>
#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <shadowhook.h>

#define LOG_TAG "MCESP2644A7"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LEVEL_RENDERER_CAMERA_RENDER_RVA ((uintptr_t)0x0ae1b6e0ULL)
#define QUEUE_RENDER_ENTITIES_RVA        ((uintptr_t)0x0ae2219cULL)
#define LOCAL_PLAYER_VPTR_RVA            ((uintptr_t)0x11f74f58ULL)

#define MAX_ESP 512
#define STALE_MS 400ULL

#define AC_PLAYER  (1u << 0)
#define AC_MOB     (1u << 1)
#define AC_MONSTER (1u << 2)
#define AC_ANIMAL  (1u << 4)

enum { CAT_NONE=0, CAT_PLAYER=1, CAT_MOB=2, CAT_ANIMAL=3 };

typedef struct { float x,y,z; } V3;
typedef struct {
    uintptr_t actor;
    float x;
    float top;
    float bottom;
    int category;
    uint64_t seen_ms;
} EspEntry;

typedef void (*level_render_fn)(void*, void*, void*, void*);
typedef void (*queue_entities_fn)(void*, void*);

static level_render_fn original_level_render = NULL;
static queue_entities_fn original_queue_entities = NULL;

static uintptr_t minecraft_load_bias = 0;
static uintptr_t discovered_builtins_offset = 0;
static int hook_state = 0;
static int esp_enabled = 1;
static int install_attempts = 0;

static uint64_t render_calls = 0;
static uint64_t queue_calls = 0;
static uint64_t actors_seen = 0;
static uint64_t classified_calls = 0;
static uint64_t position_ok = 0;
static uint64_t projected_calls = 0;
static uint64_t camera_ok = 0;
static uint32_t last_candidate_count = 0;
static uint32_t last_categories = 0;
static char last_status[160] = "not installed";

static pthread_mutex_t esp_mutex = PTHREAD_MUTEX_INITIALIZER;
static EspEntry esp_entries[MAX_ESP];

static pthread_mutex_t candidate_mutex = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t candidate_actors[MAX_ESP];
static uint32_t candidate_count = 0;

static const uintptr_t PLAYER_VPTRS[] = {
    0x11f75760ULL,0x121dcde0ULL,0x121dd608ULL,0x1228cbf8ULL
};
static const uintptr_t ANIMAL_VPTRS[] = {
    0x1220b440ULL,0x1220cab0ULL,0x12214448ULL,0x12214f68ULL,0x1220e680ULL,
    0x122170e0ULL,0x122a5670ULL,0x122149d8ULL,0x1220b9d0ULL,0x1220c520ULL,
    0x1220db60ULL,0x12210250ULL,0x12211fc8ULL,0x12212e08ULL,0x1220d5d0ULL,
    0x1220e0f0ULL,0x1220f730ULL,0x122a4b50ULL,0x12213928ULL,0x122154f8ULL,
    0x122107e0ULL,0x12211308ULL,0x12213398ULL,0x1220ec10ULL,0x12213eb8ULL,
    0x122165b8ULL,0x12210d70ULL,0x12216018ULL,0x122a50e0ULL,0x122a45c0ULL,
    0x1220fcc0ULL,0x1220f1a0ULL,0x12211898ULL,0x12216b48ULL,0x1220d040ULL
};
static const uintptr_t MOB_VPTRS[] = {
    0x12223408ULL,0x1229bdc8ULL,0x1229a748ULL,0x1229c368ULL,0x12292948ULL,
    0x1229d690ULL,0x12215a88ULL,0x12294db8ULL,0x12299688ULL,0x122974e0ULL,
    0x122a1ed0ULL,0x122923a0ULL,0x122911b0ULL,0x12296f48ULL,0x12292ed8ULL,
    0x12295e80ULL,0x1228fb50ULL,0x122177c8ULL,0x12224bd8ULL,0x122937e8ULL,
    0x12291790ULL,0x1229c918ULL,0x12221750ULL,0x12212878ULL,0x122a2460ULL,
    0x12296418ULL,0x1229b278ULL,0x12290680ULL,0x122a29f8ULL,0x122990f0ULL,
    0x1229d0e0ULL,0x1229b828ULL,0x122900e8ULL,0x12290c18ULL,0x122943c0ULL,
    0x12298028ULL,0x122a31d8ULL,0x122985c0ULL,0x1229a1b8ULL,0x12293e28ULL,
    0x12295350ULL,0x122969b0ULL,0x12299c20ULL,0x1229ace0ULL,0x12291e10ULL,
    0x122958e8ULL,0x1220bf90ULL,0x12297a78ULL,0x12298b58ULL
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static uintptr_t strip_ptr(uintptr_t p) {
#if defined(__aarch64__)
    return p & 0x00ffffffffffffffULL;
#else
    return p;
#endif
}

static int readable(uintptr_t target, size_t size) {
    if (!target || size == 0 || target + size < target) return 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[768];
    uintptr_t end_target = target + size;
    int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] == 'r' &&
            target >= (uintptr_t)start &&
            end_target <= (uintptr_t)end) {
            ok = 1;
            break;
        }
    }
    fclose(f);
    return ok;
}

static uintptr_t find_minecraft_bias(void) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    uintptr_t best = UINTPTR_MAX;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, off = 0;
        char perms[8] = {0};
        int n = 0;
        if (sscanf(line, "%llx-%*llx %7s %llx %*s %*s %n",
                   &start, perms, &off, &n) < 3) continue;
        char* mapped = line + n;
        mapped[strcspn(mapped, "\r\n")] = 0;
        if (!strstr(mapped, "libminecraftpe.so")) continue;
        uintptr_t bias = (uintptr_t)start - (uintptr_t)off;
        if (bias < best) best = bias;
    }
    fclose(f);
    return best == UINTPTR_MAX ? 0 : best;
}

static int sane_v3(V3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) &&
           fabsf(v.x) < 10000000.0f &&
           fabsf(v.y) < 10000000.0f &&
           fabsf(v.z) < 10000000.0f;
}

static float dot3(V3 a, V3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static V3 sub3(V3 a, V3 b) {
    V3 r = {a.x-b.x, a.y-b.y, a.z-b.z};
    return r;
}

static int in_list(uintptr_t v, const uintptr_t* list, size_t count) {
    for (size_t i = 0; i < count; ++i) if (list[i] == v) return 1;
    return 0;
}

static int project_rel(
        V3 rel, V3 right, V3 up, V3 forward,
        float aspect, float fov,
        float* nx, float* ny) {
    float z = dot3(rel, forward);
    if (z < 0.0f) z = -z;
    if (!isfinite(z) || z < 0.05f || z > 8192.0f) return 0;

    float fr = fov;
    if (fr > 3.2f) fr *= 0.01745329251994329577f;
    if (!(fr > 0.15f && fr < 3.05f)) return 0;
    if (!(aspect > 0.35f && aspect < 4.0f)) return 0;

    float t = tanf(fr * 0.5f);
    if (!(t > 0.01f && t < 100.0f)) return 0;

    float x = dot3(rel, right) / (z * t * aspect);
    float y = dot3(rel, up) / (z * t);
    if (!isfinite(x) || !isfinite(y) ||
        fabsf(x) > 8.0f || fabsf(y) > 8.0f) return 0;

    *nx = 0.5f * (1.0f + x);
    *ny = 0.5f * (1.0f - y);
    return 1;
}

static int read_camera(void* render_context,
                       V3* right, V3* up, V3* forward, V3* camera_pos,
                       float* aspect, float* fov) {
    if (!render_context ||
        !readable((uintptr_t)render_context + 0x28, sizeof(void*))) return 0;

    void* screen_context = *(void**)((uint8_t*)render_context + 0x28);
    if (!screen_context ||
        !readable((uintptr_t)screen_context + 0x18, sizeof(void*))) return 0;

    void* camera = *(void**)((uint8_t*)screen_context + 0x18);
    if (!camera || !readable((uintptr_t)camera + 0x138, 4)) return 0;

    memcpy(right,      (uint8_t*)camera + 0x100, sizeof(V3));
    memcpy(up,         (uint8_t*)camera + 0x10c, sizeof(V3));
    memcpy(forward,    (uint8_t*)camera + 0x118, sizeof(V3));
    memcpy(camera_pos, (uint8_t*)camera + 0x124, sizeof(V3));
    memcpy(aspect,     (uint8_t*)camera + 0x130, sizeof(float));
    memcpy(fov,        (uint8_t*)camera + 0x134, sizeof(float));

    int ok = sane_v3(*right) && sane_v3(*up) &&
             sane_v3(*forward) && sane_v3(*camera_pos) &&
             isfinite(*aspect) && isfinite(*fov);
    if (ok) __atomic_add_fetch(&camera_ok, 1, __ATOMIC_RELAXED);
    return ok;
}

static int valid_aabb(V3 mn, V3 mx) {
    if (!sane_v3(mn) || !sane_v3(mx)) return 0;
    float dx = mx.x - mn.x;
    float dy = mx.y - mn.y;
    float dz = mx.z - mn.z;
    return dx >= 0.0f && dy >= 0.0f && dz >= 0.0f &&
           dx < 20.0f && dy < 30.0f && dz < 20.0f;
}

static uintptr_t discover_builtins(void* actor) {
    if (discovered_builtins_offset) return discovered_builtins_offset;
    if (!actor || !readable((uintptr_t)actor, 0x700)) return 0;

    for (uintptr_t off = 0x100; off <= 0x600; off += 8) {
        void* state = NULL;
        void* shape = NULL;
        memcpy(&state, (uint8_t*)actor + off, sizeof(void*));
        memcpy(&shape, (uint8_t*)actor + off + 8, sizeof(void*));
        state = (void*)strip_ptr((uintptr_t)state);
        shape = (void*)strip_ptr((uintptr_t)shape);

        if (!state || !shape) continue;
        if (!readable((uintptr_t)state, 12) ||
            !readable((uintptr_t)shape, 24)) continue;

        V3 p, mn, mx;
        memcpy(&p, state, sizeof(V3));
        memcpy(&mn, shape, sizeof(V3));
        memcpy(&mx, (uint8_t*)shape + 12, sizeof(V3));

        if (!sane_v3(p) || !valid_aabb(mn, mx)) continue;

        float cx = (mn.x + mx.x) * 0.5f;
        float cz = (mn.z + mx.z) * 0.5f;
        if (fabsf(p.x - cx) > 8.0f ||
            fabsf(p.z - cz) > 8.0f ||
            p.y < mn.y - 8.0f ||
            p.y > mx.y + 8.0f) continue;

        discovered_builtins_offset = off;
        LOGI("Discovered BuiltInActorComponents offset 0x%llx",
             (unsigned long long)off);
        return off;
    }

    return 0;
}

static int actor_box(void* actor, V3* bottom, V3* top, uintptr_t* builtins_off) {
    if (!actor || !bottom || !top) return 0;

    uintptr_t off = discover_builtins(actor);
    if (!off) {
        /* fallback for nearby layouts */
        off = 0x218;
    }
    if (builtins_off) *builtins_off = off;

    if (!readable((uintptr_t)actor + off, 16)) return 0;

    void* state = NULL;
    void* shape = NULL;
    memcpy(&state, (uint8_t*)actor + off, sizeof(void*));
    memcpy(&shape, (uint8_t*)actor + off + 8, sizeof(void*));
    state = (void*)strip_ptr((uintptr_t)state);
    shape = (void*)strip_ptr((uintptr_t)shape);

    if (shape && readable((uintptr_t)shape, 24)) {
        V3 mn, mx;
        memcpy(&mn, shape, sizeof(V3));
        memcpy(&mx, (uint8_t*)shape + 12, sizeof(V3));
        if (valid_aabb(mn, mx)) {
            float cx = (mn.x + mx.x) * 0.5f;
            float cz = (mn.z + mx.z) * 0.5f;
            *bottom = (V3){cx, mn.y, cz};
            *top = (V3){cx, mx.y, cz};
            __atomic_add_fetch(&position_ok, 1, __ATOMIC_RELAXED);
            return 1;
        }
    }

    if (state && readable((uintptr_t)state, 12)) {
        V3 p;
        memcpy(&p, state, sizeof(V3));
        if (sane_v3(p)) {
            *bottom = p;
            *top = p;
            top->y += 1.8f;
            __atomic_add_fetch(&position_ok, 1, __ATOMIC_RELAXED);
            return 1;
        }
    }

    return 0;
}

static int classify_actor(void* actor, uintptr_t builtins_off) {
    if (!actor || !minecraft_load_bias || !readable((uintptr_t)actor, 8)) return CAT_NONE;

    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    if (vptr >= minecraft_load_bias) {
        uintptr_t rva = vptr - minecraft_load_bias;
        if (rva == LOCAL_PLAYER_VPTR_RVA) return CAT_NONE;
        if (in_list(rva, PLAYER_VPTRS, sizeof(PLAYER_VPTRS)/sizeof(PLAYER_VPTRS[0])))
            return CAT_PLAYER;
        if (in_list(rva, ANIMAL_VPTRS, sizeof(ANIMAL_VPTRS)/sizeof(ANIMAL_VPTRS[0])))
            return CAT_ANIMAL;
        if (in_list(rva, MOB_VPTRS, sizeof(MOB_VPTRS)/sizeof(MOB_VPTRS[0])))
            return CAT_MOB;
    }

    if (builtins_off >= 8 && readable((uintptr_t)actor + builtins_off - 8, 4)) {
        uint32_t categories = 0;
        memcpy(&categories, (uint8_t*)actor + builtins_off - 8, sizeof(categories));
        last_categories = categories;
        if ((categories & ~0x000fffffu) == 0) {
            if (categories & AC_PLAYER) return CAT_PLAYER;
            if (categories & AC_ANIMAL) return CAT_ANIMAL;
            if (categories & (AC_MONSTER | AC_MOB)) return CAT_MOB;
        }
    }

    return CAT_NONE;
}

static int actor_pointer_plausible(uintptr_t actor) {
    actor = strip_ptr(actor);
    if (!actor || !readable(actor, 8)) return 0;
    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    if (!vptr || !minecraft_load_bias) return 0;
    return vptr >= minecraft_load_bias &&
           vptr < minecraft_load_bias + 0x13000000ULL;
}

static int already_added(uintptr_t* arr, uint32_t count, uintptr_t actor) {
    for (uint32_t i = 0; i < count; ++i) if (arr[i] == actor) return 1;
    return 0;
}

static void snapshot_candidate_vectors(void* self) {
    if (!self || !minecraft_load_bias) return;

    uintptr_t temp[MAX_ESP];
    uint32_t out_count = 0;

    /*
     * 26.44.3 queueRenderEntities clears/populates several vector<Actor*>
     * fields in the 0x1c0..0x280 region. Scan the exact object range rather
     * than relying on the older mActorRenderQueue offset.
     */
    for (uintptr_t off = 0x1c0; off <= 0x280 && out_count < MAX_ESP; off += 8) {
        uintptr_t vec = (uintptr_t)self + off;
        if (!readable(vec, 24)) continue;

        uintptr_t begin = 0, end = 0, cap = 0;
        memcpy(&begin, (void*)(vec + 0), 8);
        memcpy(&end,   (void*)(vec + 8), 8);
        memcpy(&cap,   (void*)(vec + 16), 8);

        begin = strip_ptr(begin);
        end = strip_ptr(end);
        cap = strip_ptr(cap);

        if (!begin || end <= begin || cap < end) continue;
        uintptr_t bytes = end - begin;
        if (bytes % 8 != 0) continue;

        uint64_t count = bytes / 8;
        if (count == 0 || count > 1024) continue;
        if (!readable(begin, (size_t)bytes)) continue;

        for (uint64_t i = 0; i < count && out_count < MAX_ESP; ++i) {
            uintptr_t actor = 0;
            memcpy(&actor, (void*)(begin + i * 8), 8);
            actor = strip_ptr(actor);
            if (!actor_pointer_plausible(actor)) continue;
            if (already_added(temp, out_count, actor)) continue;
            temp[out_count++] = actor;
        }
    }

    pthread_mutex_lock(&candidate_mutex);
    candidate_count = out_count;
    if (out_count) memcpy(candidate_actors, temp, out_count * sizeof(uintptr_t));
    pthread_mutex_unlock(&candidate_mutex);

    last_candidate_count = out_count;
}

static void queue_render_entities_hook(void* self, void* params) {
    __atomic_add_fetch(&queue_calls, 1, __ATOMIC_RELAXED);

    if (original_queue_entities) {
        original_queue_entities(self, params);
    }

    if (__atomic_load_n(&esp_enabled, __ATOMIC_RELAXED)) {
        snapshot_candidate_vectors(self);
    }
}

static void update_esp(uintptr_t actor, int category,
                       float x, float top, float bottom) {
    uint64_t t = now_ms();
    pthread_mutex_lock(&esp_mutex);

    int free_slot = -1;
    int oldest = 0;
    uint64_t oldest_time = UINT64_MAX;

    for (int i = 0; i < MAX_ESP; ++i) {
        if (esp_entries[i].actor == actor) {
            esp_entries[i] = (EspEntry){actor,x,top,bottom,category,t};
            pthread_mutex_unlock(&esp_mutex);
            return;
        }
        if (!esp_entries[i].actor && free_slot < 0) free_slot = i;
        if (esp_entries[i].seen_ms < oldest_time) {
            oldest_time = esp_entries[i].seen_ms;
            oldest = i;
        }
    }

    int slot = free_slot >= 0 ? free_slot : oldest;
    esp_entries[slot] = (EspEntry){actor,x,top,bottom,category,t};
    pthread_mutex_unlock(&esp_mutex);
}

static void project_candidate_snapshot(void* render_context) {
    if (!render_context || !__atomic_load_n(&esp_enabled, __ATOMIC_RELAXED)) return;

    uintptr_t local[MAX_ESP];
    uint32_t count = 0;

    pthread_mutex_lock(&candidate_mutex);
    count = candidate_count;
    if (count > MAX_ESP) count = MAX_ESP;
    if (count) memcpy(local, candidate_actors, count * sizeof(uintptr_t));
    pthread_mutex_unlock(&candidate_mutex);

    last_candidate_count = count;
    if (!count) return;

    V3 right, up, forward, camera_pos;
    float aspect = 0.0f, fov = 0.0f;
    if (!read_camera(render_context, &right, &up, &forward,
                     &camera_pos, &aspect, &fov)) return;

    for (uint32_t i = 0; i < count; ++i) {
        void* actor = (void*)local[i];
        if (!actor_pointer_plausible((uintptr_t)actor)) continue;

        __atomic_add_fetch(&actors_seen, 1, __ATOMIC_RELAXED);

        V3 bottom_world, top_world;
        uintptr_t builtins_off = 0;
        if (!actor_box(actor, &bottom_world, &top_world, &builtins_off)) continue;

        int category = classify_actor(actor, builtins_off);
        if (category == CAT_NONE) continue;
        __atomic_add_fetch(&classified_calls, 1, __ATOMIC_RELAXED);

        V3 rb = sub3(bottom_world, camera_pos);
        V3 rt = sub3(top_world, camera_pos);
        float bx, by, tx, ty;

        if (!project_rel(rb, right, up, forward, aspect, fov, &bx, &by) ||
            !project_rel(rt, right, up, forward, aspect, fov, &tx, &ty)) continue;

        float top_n = fminf(by, ty);
        float bottom_n = fmaxf(by, ty);
        if (!isfinite(bx) || !isfinite(top_n) || !isfinite(bottom_n)) continue;
        if (bottom_n - top_n < 0.002f || bottom_n - top_n > 3.0f) continue;
        if (bx < -0.5f || bx > 1.5f || bottom_n < -0.5f || top_n > 1.5f) continue;

        __atomic_add_fetch(&projected_calls, 1, __ATOMIC_RELAXED);
        update_esp((uintptr_t)actor, category, bx, top_n, bottom_n);
    }
}

static void level_renderer_camera_render_hook(
        void* self, void* render_context, void* render_obj, void* client_instance) {
    __atomic_add_fetch(&render_calls, 1, __ATOMIC_RELAXED);

    project_candidate_snapshot(render_context);

    if (original_level_render) {
        original_level_render(self, render_context, render_obj, client_instance);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeInstallEsp(
        JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;

    if (__atomic_load_n(&hook_state, __ATOMIC_ACQUIRE) == 2) return JNI_TRUE;
    __atomic_add_fetch(&install_attempts, 1, __ATOMIC_RELAXED);

    minecraft_load_bias = find_minecraft_bias();
    if (!minecraft_load_bias) {
        snprintf(last_status, sizeof(last_status), "waiting for libminecraftpe");
        return JNI_FALSE;
    }

    uintptr_t render_target = minecraft_load_bias + LEVEL_RENDERER_CAMERA_RENDER_RVA;
    uintptr_t queue_target  = minecraft_load_bias + QUEUE_RENDER_ENTITIES_RVA;

    static const uint8_t render_fp[] = {
        0xff,0x43,0x04,0xd1,0xea,0x53,0x00,0xfd,
        0xe9,0xa3,0x0a,0x6d,0xfd,0xfb,0x0b,0xa9
    };
    static const uint8_t queue_fp[] = {
        0xff,0xc3,0x07,0xd1,0xe8,0xc3,0x00,0xfd,
        0xfd,0x7b,0x19,0xa9,0xfc,0x6f,0x1a,0xa9
    };

    if (!readable(render_target, sizeof(render_fp)) ||
        memcmp((void*)render_target, render_fp, sizeof(render_fp)) != 0) {
        snprintf(last_status, sizeof(last_status), "render fingerprint mismatch");
        return JNI_FALSE;
    }

    if (!readable(queue_target, sizeof(queue_fp)) ||
        memcmp((void*)queue_target, queue_fp, sizeof(queue_fp)) != 0) {
        snprintf(last_status, sizeof(last_status), "queue fingerprint mismatch");
        return JNI_FALSE;
    }

    int init = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (init != SHADOWHOOK_ERRNO_OK) {
        LOGI("shadowhook_init returned %d; continuing", init);
    }

    void* rstub = shadowhook_hook_func_addr(
        (void*)render_target,
        (void*)level_renderer_camera_render_hook,
        (void**)&original_level_render);
    if (!rstub || !original_level_render) {
        int e = shadowhook_get_errno();
        snprintf(last_status, sizeof(last_status), "render hook failed:%d", e);
        return JNI_FALSE;
    }

    void* qstub = shadowhook_hook_func_addr(
        (void*)queue_target,
        (void*)queue_render_entities_hook,
        (void**)&original_queue_entities);
    if (!qstub || !original_queue_entities) {
        int e = shadowhook_get_errno();
        snprintf(last_status, sizeof(last_status), "queue hook failed:%d", e);
        return JNI_FALSE;
    }

    snprintf(last_status, sizeof(last_status), "render+queue hooks installed");
    __atomic_store_n(&hook_state, 2, __ATOMIC_RELEASE);
    LOGI("A7 hooks installed");
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeFillEspSnapshot(
        JNIEnv* env, jclass cls, jfloatArray output) {
    (void)cls;
    if (!__atomic_load_n(&esp_enabled, __ATOMIC_RELAXED)) return 0;
    if (!output) return 0;

    jsize capacity = (*env)->GetArrayLength(env, output);
    if (capacity < 4) return 0;

    jfloat* out = (*env)->GetFloatArrayElements(env, output, NULL);
    if (!out) return 0;

    int max_count = capacity / 4;
    int count = 0;
    uint64_t t = now_ms();

    pthread_mutex_lock(&esp_mutex);
    for (int i = 0; i < MAX_ESP && count < max_count; ++i) {
        EspEntry* e = &esp_entries[i];
        if (!e->actor) continue;
        if (t - e->seen_ms > STALE_MS) {
            memset(e, 0, sizeof(*e));
            continue;
        }
        out[count*4 + 0] = e->x;
        out[count*4 + 1] = e->top;
        out[count*4 + 2] = e->bottom;
        out[count*4 + 3] = (float)e->category;
        ++count;
    }
    pthread_mutex_unlock(&esp_mutex);

    (*env)->ReleaseFloatArrayElements(env, output, out, 0);
    return count;
}

JNIEXPORT jstring JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeGetDebugStatus(
        JNIEnv* env, jclass cls) {
    (void)cls;
    char buf[480];
    snprintf(buf, sizeof(buf),
        "A7 %s | render=%llu | queue=%llu | cand=%u | seen=%llu | pos=%llu | class=%llu | proj=%llu | cam=%llu | layout=0x%llx",
        last_status,
        (unsigned long long)__atomic_load_n(&render_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&queue_calls, __ATOMIC_RELAXED),
        last_candidate_count,
        (unsigned long long)__atomic_load_n(&actors_seen, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&position_ok, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&classified_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&projected_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&camera_ok, __ATOMIC_RELAXED),
        (unsigned long long)discovered_builtins_offset);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeSetEspEnabled(
        JNIEnv* env, jclass cls, jboolean enabled) {
    (void)env;
    (void)cls;
    __atomic_store_n(&esp_enabled, enabled ? 1 : 0, __ATOMIC_RELEASE);
    if (!enabled) {
        pthread_mutex_lock(&esp_mutex);
        memset(esp_entries, 0, sizeof(esp_entries));
        pthread_mutex_unlock(&esp_mutex);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeIsEspEnabled(
        JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return __atomic_load_n(&esp_enabled, __ATOMIC_ACQUIRE) ? JNI_TRUE : JNI_FALSE;
}
