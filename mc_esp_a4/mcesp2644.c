#include <jni.h>
#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <shadowhook.h>

#define LOG_TAG "MCESP2644A6"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LEVEL_RENDERER_CAMERA_RENDER_RVA ((uintptr_t)0x0ae1b6e0ULL)
#define LOCAL_PLAYER_VPTR_RVA            ((uintptr_t)0x11f74f58ULL)

#define LRC_ACTOR_QUEUE_OFFSET ((uintptr_t)0x280ULL)
#define ACTOR_CATEGORY_OFFSET  ((uintptr_t)0x210ULL)
#define ACTOR_BUILTINS_OFFSET  ((uintptr_t)0x218ULL)

#define MAX_ESP 512
#define STALE_MS 350ULL

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

static level_render_fn original_level_render = NULL;
static uintptr_t minecraft_load_bias = 0;
static int hook_state = 0;
static int esp_enabled = 1;
static int install_attempts = 0;
static uint64_t hook_calls = 0;
static uint64_t queue_scans = 0;
static uint64_t actors_seen = 0;
static uint64_t classified_calls = 0;
static uint64_t projected_calls = 0;
static uint32_t last_queue_count = 0;
static uint32_t last_categories = 0;
static char last_status[128] = "not installed";

static pthread_mutex_t esp_mutex = PTHREAD_MUTEX_INITIALIZER;
static EspEntry esp_entries[MAX_ESP];

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
                   &start, perms, &off, &n) < 3) {
            continue;
        }

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

static int project_rel(
        V3 rel, V3 right, V3 up, V3 forward,
        float aspect, float fov,
        float* nx, float* ny) {
    float z = dot3(rel, forward);

    /* Bedrock camera conventions can flip forward depending on render path.
       Use magnitude, but reject points almost on the camera plane. */
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
        fabsf(x) > 8.0f || fabsf(y) > 8.0f) {
        return 0;
    }

    *nx = 0.5f * (1.0f + x);
    *ny = 0.5f * (1.0f - y);
    return 1;
}

static int read_camera(void* render_context,
                       V3* right, V3* up, V3* forward, V3* camera_pos,
                       float* aspect, float* fov) {
    if (!render_context ||
        !readable((uintptr_t)render_context + 0x28, sizeof(void*))) {
        return 0;
    }

    /* BaseActorRenderContext has an implicit vptr at +0x0.
       mScreenContext is therefore +0x28 in the real object. */
    void* screen_context = *(void**)((uint8_t*)render_context + 0x28);
    if (!screen_context ||
        !readable((uintptr_t)screen_context + 0x18, sizeof(void*))) {
        return 0;
    }

    /* ScreenContext: UIScreenContext (0x10) + MeshContext.
       MeshContext.camera is the second pointer => ScreenContext + 0x18. */
    void* camera = *(void**)((uint8_t*)screen_context + 0x18);
    if (!camera || !readable((uintptr_t)camera + 0x138, 4)) return 0;

    memcpy(right,      (uint8_t*)camera + 0x100, sizeof(V3));
    memcpy(up,         (uint8_t*)camera + 0x10c, sizeof(V3));
    memcpy(forward,    (uint8_t*)camera + 0x118, sizeof(V3));
    memcpy(camera_pos, (uint8_t*)camera + 0x124, sizeof(V3));
    memcpy(aspect,     (uint8_t*)camera + 0x130, sizeof(float));
    memcpy(fov,        (uint8_t*)camera + 0x134, sizeof(float));

    return sane_v3(*right) && sane_v3(*up) &&
           sane_v3(*forward) && sane_v3(*camera_pos) &&
           isfinite(*aspect) && isfinite(*fov);
}

static int classify_actor(void* actor) {
    if (!actor || !minecraft_load_bias) return CAT_NONE;
    if (!readable((uintptr_t)actor, ACTOR_BUILTINS_OFFSET + 0x20)) return CAT_NONE;

    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    if (vptr >= minecraft_load_bias &&
        vptr - minecraft_load_bias == LOCAL_PLAYER_VPTR_RVA) {
        return CAT_NONE;
    }

    uint32_t categories = 0;
    memcpy(&categories, (uint8_t*)actor + ACTOR_CATEGORY_OFFSET, sizeof(categories));
    last_categories = categories;

    if (categories & AC_PLAYER) return CAT_PLAYER;
    if (categories & AC_ANIMAL) return CAT_ANIMAL;
    if (categories & (AC_MONSTER | AC_MOB)) return CAT_MOB;
    return CAT_NONE;
}

static int actor_box(void* actor, V3* bottom, V3* top) {
    if (!actor || !bottom || !top) return 0;

    /* Actor is polymorphic, so BuiltInActorComponents starts at +0x218.
       [0] StateVectorComponent*, [8] AABBShapeComponent*. */
    void* state = NULL;
    void* shape = NULL;
    memcpy(&state, (uint8_t*)actor + ACTOR_BUILTINS_OFFSET + 0x0, sizeof(void*));
    memcpy(&shape, (uint8_t*)actor + ACTOR_BUILTINS_OFFSET + 0x8, sizeof(void*));

    if (shape && readable((uintptr_t)shape, 24)) {
        V3 mn, mx;
        memcpy(&mn, (uint8_t*)shape + 0x0, sizeof(V3));
        memcpy(&mx, (uint8_t*)shape + 0xc, sizeof(V3));
        if (sane_v3(mn) && sane_v3(mx) &&
            mx.y >= mn.y && mx.y - mn.y < 20.0f) {
            float cx = (mn.x + mx.x) * 0.5f;
            float cz = (mn.z + mx.z) * 0.5f;
            *bottom = (V3){cx, mn.y, cz};
            *top    = (V3){cx, mx.y, cz};
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
            return 1;
        }
    }

    return 0;
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
            esp_entries[i].x = x;
            esp_entries[i].top = top;
            esp_entries[i].bottom = bottom;
            esp_entries[i].category = category;
            esp_entries[i].seen_ms = t;
            pthread_mutex_unlock(&esp_mutex);
            return;
        }
        if (esp_entries[i].actor == 0 && free_slot < 0) free_slot = i;
        if (esp_entries[i].seen_ms < oldest_time) {
            oldest_time = esp_entries[i].seen_ms;
            oldest = i;
        }
    }

    int slot = free_slot >= 0 ? free_slot : oldest;
    esp_entries[slot] = (EspEntry){actor, x, top, bottom, category, t};
    pthread_mutex_unlock(&esp_mutex);
}

static uint32_t capture_actor_queue(void* level_renderer_camera,
                                    void* render_context) {
    if (!level_renderer_camera || !render_context ||
        !__atomic_load_n(&esp_enabled, __ATOMIC_RELAXED)) {
        return 0;
    }

    __atomic_add_fetch(&queue_scans, 1, __ATOMIC_RELAXED);

    uintptr_t q = (uintptr_t)level_renderer_camera + LRC_ACTOR_QUEUE_OFFSET;
    if (!readable(q, 24)) return 0;

    uintptr_t begin = 0, end = 0, cap = 0;
    memcpy(&begin, (void*)(q + 0x0), 8);
    memcpy(&end,   (void*)(q + 0x8), 8);
    memcpy(&cap,   (void*)(q + 0x10), 8);

    if (!begin || end < begin || cap < end) return 0;
    uintptr_t bytes = end - begin;
    if (bytes % sizeof(void*) != 0) return 0;

    uint64_t count64 = bytes / sizeof(void*);
    if (count64 > 4096) return 0;
    uint32_t count = (uint32_t)count64;
    last_queue_count = count;

    if (!readable(begin, bytes ? (size_t)bytes : sizeof(void*))) return 0;

    V3 right, up, forward, camera_pos;
    float aspect = 0.0f, fov = 0.0f;
    if (!read_camera(render_context, &right, &up, &forward,
                     &camera_pos, &aspect, &fov)) {
        return count;
    }

    for (uint32_t i = 0; i < count; ++i) {
        void* actor = NULL;
        memcpy(&actor, (void*)(begin + (uintptr_t)i * sizeof(void*)), sizeof(void*));
        actor = (void*)strip_ptr((uintptr_t)actor);
        if (!actor) continue;

        __atomic_add_fetch(&actors_seen, 1, __ATOMIC_RELAXED);

        int category = classify_actor(actor);
        if (category == CAT_NONE) continue;
        __atomic_add_fetch(&classified_calls, 1, __ATOMIC_RELAXED);

        V3 bottom_world, top_world;
        if (!actor_box(actor, &bottom_world, &top_world)) continue;

        V3 rb = sub3(bottom_world, camera_pos);
        V3 rt = sub3(top_world, camera_pos);
        float bx, by, tx, ty;

        if (!project_rel(rb, right, up, forward, aspect, fov, &bx, &by) ||
            !project_rel(rt, right, up, forward, aspect, fov, &tx, &ty)) {
            continue;
        }

        float top_n = fminf(by, ty);
        float bottom_n = fmaxf(by, ty);
        if (!isfinite(bx) || !isfinite(top_n) || !isfinite(bottom_n)) continue;
        if (bottom_n - top_n < 0.002f || bottom_n - top_n > 3.0f) continue;
        if (bx < -0.5f || bx > 1.5f || bottom_n < -0.5f || top_n > 1.5f) continue;

        __atomic_add_fetch(&projected_calls, 1, __ATOMIC_RELAXED);
        update_esp((uintptr_t)actor, category, bx, top_n, bottom_n);
    }

    return count;
}

static void level_renderer_camera_render_hook(
        void* self, void* render_context, void* render_obj, void* client_instance) {
    __atomic_add_fetch(&hook_calls, 1, __ATOMIC_RELAXED);

    uint32_t before = capture_actor_queue(self, render_context);

    if (original_level_render) {
        original_level_render(self, render_context, render_obj, client_instance);
    }

    /* Some builds populate the flat_map during render. Retry after original
       only when the pre-call queue was empty. */
    if (before == 0) {
        capture_actor_queue(self, render_context);
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

    uintptr_t target = minecraft_load_bias + LEVEL_RENDERER_CAMERA_RENDER_RVA;

    static const uint8_t fp[] = {
        0xff,0x43,0x04,0xd1,
        0xea,0x53,0x00,0xfd,
        0xe9,0xa3,0x0a,0x6d,
        0xfd,0xfb,0x0b,0xa9
    };

    if (!readable(target, sizeof(fp)) ||
        memcmp((void*)target, fp, sizeof(fp)) != 0) {
        snprintf(last_status, sizeof(last_status),
                 "LevelRendererCamera fingerprint mismatch");
        LOGE("render fingerprint mismatch at %p", (void*)target);
        return JNI_FALSE;
    }

    int init = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (init != SHADOWHOOK_ERRNO_OK) {
        LOGI("shadowhook_init returned %d; trying hook", init);
    }

    void* stub = shadowhook_hook_func_addr(
        (void*)target,
        (void*)level_renderer_camera_render_hook,
        (void**)&original_level_render);

    if (!stub || !original_level_render) {
        int e = shadowhook_get_errno();
        snprintf(last_status, sizeof(last_status), "LRC hook failed: %d", e);
        LOGE("LevelRendererCamera hook failed: %d", e);
        return JNI_FALSE;
    }

    snprintf(last_status, sizeof(last_status), "LevelRendererCamera hook installed");
    __atomic_store_n(&hook_state, 2, __ATOMIC_RELEASE);
    LOGI("A6 ESP hook installed at RVA 0x%llx",
         (unsigned long long)LEVEL_RENDERER_CAMERA_RENDER_RVA);
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
    char buf[420];
    snprintf(buf, sizeof(buf),
        "A6 %s | calls=%llu | queue=%u | seen=%llu | class=%llu | projected=%llu | cat=0x%x",
        last_status,
        (unsigned long long)__atomic_load_n(&hook_calls, __ATOMIC_RELAXED),
        last_queue_count,
        (unsigned long long)__atomic_load_n(&actors_seen, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&classified_calls, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&projected_calls, __ATOMIC_RELAXED),
        last_categories);
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
    LOGI("ESP %s", enabled ? "ON" : "OFF");
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeIsEspEnabled(
        JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return __atomic_load_n(&esp_enabled, __ATOMIC_ACQUIRE)
        ? JNI_TRUE : JNI_FALSE;
}
