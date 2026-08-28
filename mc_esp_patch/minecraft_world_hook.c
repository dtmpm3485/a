#include "minecraft_world_hook.h"
#include "kafka_inline_hook.h"

#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "MCESP2644"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define DATA_DRIVEN_RENDER_RVA ((uintptr_t)0x0a5c8de8ULL)
#define MAX_ESP 256
#define STALE_MS 280ULL

enum {
    CAT_NONE = 0,
    CAT_PLAYER = 1,
    CAT_MOB = 2,
    CAT_ANIMAL = 3
};

typedef void (*render_fn)(void*, void*, void*);

typedef struct {
    uintptr_t actor;
    float x;
    float top;
    float bottom;
    int category;
    uint64_t seen_ms;
} EspEntry;

static render_fn original_render;
static uintptr_t minecraft_load_bias;
static int hook_installation_state;
static pthread_mutex_t esp_mutex = PTHREAD_MUTEX_INITIALIZER;
static EspEntry esp_entries[MAX_ESP];

static const uintptr_t PLAYER_VPTRS[] = {
    0x11f75760ULL, /* RemotePlayer */
    0x121dcde0ULL, /* ServerPlayer */
    0x121dd608ULL, /* SimulatedPlayer */
    0x1228cbf8ULL  /* Player */
};

static const uintptr_t LOCAL_PLAYER_VPTRS[] = {
    0x11f74f58ULL /* LocalPlayer - never draw self */
};

static const uintptr_t ANIMAL_VPTRS[] = {
    0x1220b440ULL, /* HappyGhast */
    0x1220cab0ULL, /* MushroomCow */
    0x12214448ULL, /* Bee */
    0x12214f68ULL, /* Cat */
    0x1220e680ULL, /* Pig */
    0x122170e0ULL, /* Goat */
    0x122a5670ULL, /* Wolf */
    0x122149d8ULL, /* Camel */
    0x1220b9d0ULL, /* Horse */
    0x1220c520ULL, /* Llama */
    0x1220db60ULL, /* Panda */
    0x12210250ULL, /* Sheep */
    0x12211fc8ULL, 0x12212e08ULL, /* Animal */
    0x1220d5d0ULL, /* Ocelot */
    0x1220e0f0ULL, /* Parrot */
    0x1220f730ULL, /* Rabbit */
    0x122a4b50ULL, /* Turtle */
    0x12213928ULL, /* Axolotl */
    0x122154f8ULL, /* Chicken */
    0x122107e0ULL, /* Sniffer */
    0x12211308ULL, /* Strider */
    0x12213398ULL, /* Armadillo */
    0x1220ec10ULL, /* PolarBear */
    0x12213eb8ULL, /* Bat */
    0x122165b8ULL, /* Fish */
    0x12210d70ULL, /* Squid */
    0x12216018ULL, /* Dolphin */
    0x122a50e0ULL, /* WaterAnimal */
    0x122a45c0ULL, /* TropicalFish */
    0x1220fcc0ULL, /* Salmon */
    0x1220f1a0ULL, /* Pufferfish */
    0x12211898ULL, /* Tadpole */
    0x12216b48ULL, /* GlowSquid */
    0x1220d040ULL  /* Nautilus */
};

static const uintptr_t MOB_VPTRS[] = {
    0x12223408ULL, /* ArmorStand */
    0x1229bdc8ULL, /* CaveSpider */
    0x1229a748ULL, /* Silverfish */
    0x1229c368ULL, /* SulfurCube */
    0x12292948ULL, /* VillagerV2 */
    0x1229d690ULL, /* WitherBoss */
    0x12215a88ULL, /* CopperGolem */
    0x12294db8ULL, /* EnderDragon */
    0x12299688ULL, /* PiglinBrute */
    0x122974e0ULL, /* IllagerBeast */
    0x122a1ed0ULL, /* TripodCamera */
    0x122923a0ULL, /* VillagerBase */
    0x122911b0ULL, /* ZombieVillager */
    0x12296f48ULL, /* HumanoidMonster */
    0x12292ed8ULL, /* WanderingTrader */
    0x12295e80ULL, /* EvocationIllager */
    0x1228fb50ULL, /* VindicationIllager */
    0x122177c8ULL, 0x12224bd8ULL, 0x122937e8ULL, /* Mob */
    0x12291790ULL, /* Npc */
    0x1229c918ULL, /* Vex */
    0x12221750ULL, /* Agent */
    0x12212878ULL, /* Allay */
    0x122a2460ULL, /* Blaze */
    0x12296418ULL, /* Ghast */
    0x1229b278ULL, /* Slime */
    0x12290680ULL, /* Witch */
    0x122a29f8ULL, /* Breeze */
    0x122990f0ULL, 0x1229d0e0ULL, /* Piglin */
    0x1229b828ULL, /* Spider */
    0x122900e8ULL, /* Warden */
    0x12290c18ULL, /* Zombie */
    0x122943c0ULL, /* Creeper */
    0x12298028ULL, 0x122a31d8ULL, /* Monster */
    0x122985c0ULL, /* Phantom */
    0x1229a1b8ULL, /* Shulker */
    0x12293e28ULL, /* Creaking */
    0x12295350ULL, /* EnderMan */
    0x122969b0ULL, /* Guardian */
    0x12299c20ULL, /* Pillager */
    0x1229ace0ULL, /* Skeleton */
    0x12291e10ULL, /* Villager */
    0x122958e8ULL, /* Endermite */
    0x1220bf90ULL, /* IronGolem */
    0x12297a78ULL, /* LavaSlime */
    0x12298b58ULL  /* PigZombie */
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

static int in_list(uintptr_t value, const uintptr_t* list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == value) return 1;
    }
    return 0;
}

static int classify_actor(void* actor) {
    if (actor == NULL || minecraft_load_bias == 0) return CAT_NONE;
    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    if (vptr < minecraft_load_bias) return CAT_NONE;
    uintptr_t rva = vptr - minecraft_load_bias;

    if (in_list(rva, LOCAL_PLAYER_VPTRS, sizeof(LOCAL_PLAYER_VPTRS) / sizeof(LOCAL_PLAYER_VPTRS[0])))
        return CAT_NONE;
    if (in_list(rva, PLAYER_VPTRS, sizeof(PLAYER_VPTRS) / sizeof(PLAYER_VPTRS[0])))
        return CAT_PLAYER;
    if (in_list(rva, ANIMAL_VPTRS, sizeof(ANIMAL_VPTRS) / sizeof(ANIMAL_VPTRS[0])))
        return CAT_ANIMAL;
    if (in_list(rva, MOB_VPTRS, sizeof(MOB_VPTRS) / sizeof(MOB_VPTRS[0])))
        return CAT_MOB;
    return CAT_NONE;
}

typedef struct { float x, y, z; } V3;

static int sane_v3(V3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z)
        && fabsf(v.x) < 10000000.0f && fabsf(v.y) < 10000000.0f && fabsf(v.z) < 10000000.0f;
}
static float dot3(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3 sub3(V3 a, V3 b) { V3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }

static int project_rel(
        V3 rel, V3 right, V3 up, V3 forward, float aspect, float fov,
        float* nx, float* ny, float* depth) {
    float z = dot3(rel, forward);
    if (z < 0.0f) z = -z;
    if (!isfinite(z) || z < 0.05f || z > 4096.0f) return 0;

    float fov_rad = fov;
    if (fov_rad > 3.2f) fov_rad = fov_rad * 0.01745329251994329577f;
    if (!(fov_rad > 0.15f && fov_rad < 3.05f)) return 0;
    if (!(aspect > 0.35f && aspect < 4.0f)) return 0;

    float t = tanf(fov_rad * 0.5f);
    if (!(t > 0.01f && t < 100.0f)) return 0;

    float x = dot3(rel, right) / (z * t * aspect);
    float y = dot3(rel, up) / (z * t);
    if (!isfinite(x) || !isfinite(y) || fabsf(x) > 6.0f || fabsf(y) > 6.0f) return 0;
    *nx = 0.5f * (1.0f + x);
    *ny = 0.5f * (1.0f - y);
    *depth = z;
    return 1;
}

static int project_actor(void* render_context, void* actor_render_data, int category,
                         float* out_x, float* out_top, float* out_bottom) {
    if (render_context == NULL || actor_render_data == NULL) return 0;

    V3 pos;
    memcpy(&pos, (uint8_t*)actor_render_data + 0x10, sizeof(pos));
    if (!sane_v3(pos)) return 0;

    void* screen_context = *(void**)((uint8_t*)render_context + 0x20);
    if (screen_context == NULL) return 0;
    void* camera = *(void**)((uint8_t*)screen_context + 0x18);
    if (camera == NULL) return 0;

    V3 right, up, forward, camera_pos;
    float aspect, fov;
    memcpy(&right,      (uint8_t*)camera + 0x100, sizeof(right));
    memcpy(&up,         (uint8_t*)camera + 0x10c, sizeof(up));
    memcpy(&forward,    (uint8_t*)camera + 0x118, sizeof(forward));
    memcpy(&camera_pos, (uint8_t*)camera + 0x124, sizeof(camera_pos));
    memcpy(&aspect,     (uint8_t*)camera + 0x130, sizeof(aspect));
    memcpy(&fov,        (uint8_t*)camera + 0x134, sizeof(fov));

    if (!sane_v3(right) || !sane_v3(up) || !sane_v3(forward) || !sane_v3(camera_pos)
        || !isfinite(aspect) || !isfinite(fov)) return 0;

    float height = category == CAT_ANIMAL ? 1.35f : 1.82f;
    V3 pos_top = pos;
    pos_top.y += height;

    V3 rel_abs = sub3(pos, camera_pos);
    V3 rel_abs_top = sub3(pos_top, camera_pos);

    float bx1, by1, bz1, tx1, ty1, tz1;
    int ok_abs = project_rel(rel_abs, right, up, forward, aspect, fov, &bx1, &by1, &bz1)
              && project_rel(rel_abs_top, right, up, forward, aspect, fov, &tx1, &ty1, &tz1);

    float bx2, by2, bz2, tx2, ty2, tz2;
    int ok_rel = project_rel(pos, right, up, forward, aspect, fov, &bx2, &by2, &bz2)
              && project_rel(pos_top, right, up, forward, aspect, fov, &tx2, &ty2, &tz2);

    if (!ok_abs && !ok_rel) return 0;

    float bx, by, tx, ty;
    if (ok_abs && ok_rel) {
        float score_abs = fabsf(bx1 - 0.5f) + fabsf(by1 - 0.5f);
        float score_rel = fabsf(bx2 - 0.5f) + fabsf(by2 - 0.5f);
        if (score_abs <= score_rel) { bx=bx1; by=by1; tx=tx1; ty=ty1; }
        else                        { bx=bx2; by=by2; tx=tx2; ty=ty2; }
    } else if (ok_abs) {
        bx=bx1; by=by1; tx=tx1; ty=ty1;
    } else {
        bx=bx2; by=by2; tx=tx2; ty=ty2;
    }

    float top = fminf(by, ty);
    float bottom = fmaxf(by, ty);
    if (bottom - top < 0.003f || bottom - top > 2.0f) return 0;
    if (bx < -0.3f || bx > 1.3f || bottom < -0.3f || top > 1.3f) return 0;

    *out_x = bx;
    *out_top = top;
    *out_bottom = bottom;
    return 1;
}

static void update_esp(uintptr_t actor, int category, float x, float top, float bottom) {
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
    esp_entries[slot].actor = actor;
    esp_entries[slot].x = x;
    esp_entries[slot].top = top;
    esp_entries[slot].bottom = bottom;
    esp_entries[slot].category = category;
    esp_entries[slot].seen_ms = t;
    pthread_mutex_unlock(&esp_mutex);
}

static void data_driven_render_hook(void* self, void* render_context, void* actor_render_data) {
    if (actor_render_data != NULL) {
        void* actor = *(void**)actor_render_data;
        int category = classify_actor(actor);
        if (category != CAT_NONE) {
            float x, top, bottom;
            if (project_actor(render_context, actor_render_data, category, &x, &top, &bottom)) {
                update_esp(strip_ptr((uintptr_t)actor), category, x, top, bottom);
            }
        }
    }
    original_render(self, render_context, actor_render_data);
}

static uintptr_t find_load_bias(const char* path) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    uintptr_t best = UINTPTR_MAX;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, off = 0;
        char perms[8] = {0};
        int n = 0;
        if (sscanf(line, "%llx-%*llx %7s %llx %*s %*s %n", &start, perms, &off, &n) < 3)
            continue;
        char* mapped = line + n;
        mapped[strcspn(mapped, "\r\n")] = 0;
        if (strcmp(mapped, path) != 0) continue;
        uintptr_t bias = (uintptr_t)start - (uintptr_t)off;
        if (bias < best) best = bias;
    }
    fclose(f);
    return best == UINTPTR_MAX ? 0 : best;
}

static int address_is_mapped(uintptr_t target, size_t size) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    uintptr_t end_target = target + size;
    int ok = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] == 'r' && target >= (uintptr_t)start && end_target <= (uintptr_t)end) {
            ok = 1;
            break;
        }
    }
    fclose(f);
    return ok;
}

static int target_matches_2644(uintptr_t target) {
    static const uint8_t expected[] = {
        0xe9,0x23,0xbc,0x6d, 0xfd,0x7b,0x01,0xa9,
        0xf6,0x57,0x02,0xa9, 0xf4,0x4f,0x03,0xa9
    };
    if (!address_is_mapped(target, sizeof(expected))) {
        LOGE("ESP target is not mapped/readable: %p", (void*)target);
        return 0;
    }
    return memcmp((const void*)target, expected, sizeof(expected)) == 0;
}

static void report_installed(ANativeActivity* activity, const char* backend) {
    if (!activity || !activity->env || !activity->clazz) return;
    JNIEnv* env = activity->env;
    jclass activity_class = (*env)->GetObjectClass(env, activity->clazz);
    if (!activity_class) return;
    jmethodID get_app = (*env)->GetMethodID(env, activity_class, "getApplication", "()Landroid/app/Application;");
    (*env)->DeleteLocalRef(env, activity_class);
    if (!get_app) { (*env)->ExceptionClear(env); return; }
    jobject app = (*env)->CallObjectMethod(env, activity->clazz, get_app);
    if ((*env)->ExceptionCheck(env) || !app) { (*env)->ExceptionClear(env); return; }
    jclass app_class = (*env)->GetObjectClass(env, app);
    jmethodID method = app_class ? (*env)->GetMethodID(env, app_class, "onMinecraftHookInstalled", "(Ljava/lang/String;)V") : NULL;
    if (method) {
        jstring s = (*env)->NewStringUTF(env, backend);
        (*env)->CallVoidMethod(env, app, method, s);
        (*env)->DeleteLocalRef(env, s);
    } else {
        (*env)->ExceptionClear(env);
    }
    if (app_class) (*env)->DeleteLocalRef(env, app_class);
    (*env)->DeleteLocalRef(env, app);
}

int kafka_install_world_entered_hook(const char* minecraft_library_path, ANativeActivity* activity) {
    int expected = 0;
    if (!__atomic_compare_exchange_n(&hook_installation_state, &expected, 1, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return expected == 2;
    }

    minecraft_load_bias = find_load_bias(minecraft_library_path);
    if (!minecraft_load_bias) {
        LOGE("Could not resolve libminecraftpe load bias");
        __atomic_store_n(&hook_installation_state, 0, __ATOMIC_RELEASE);
        return 0;
    }

    uintptr_t target = minecraft_load_bias + DATA_DRIVEN_RENDER_RVA;
    if (!target_matches_2644(target)) {
        LOGE("26.44.3 render fingerprint mismatch at %p", (void*)target);
        __atomic_store_n(&hook_installation_state, 3, __ATOMIC_RELEASE);
        return 0;
    }

    kafka_hook_result result;
    memset(&result, 0, sizeof(result));
    if (!kafka_inline_hook_install((void*)target, (void*)data_driven_render_hook,
                                   (void**)&original_render, &result)) {
        LOGE("ESP hook failed: %s", result.error ? result.error : "unknown");
        __atomic_store_n(&hook_installation_state, 3, __ATOMIC_RELEASE);
        return 0;
    }

    const char* backend = kafka_hook_backend_name(result.backend);
    LOGI("Minecraft 26.44.3 ESP hook installed at RVA 0x%llx via %s",
         (unsigned long long)DATA_DRIVEN_RENDER_RVA, backend);
    report_installed(activity, "MC ESP 26.44.3");
    __atomic_store_n(&hook_installation_state, 2, __ATOMIC_RELEASE);
    return 1;
}

JNIEXPORT jint JNICALL
Java_com_aruked_kafkalauncher_EspOverlayView_nativeFillEspSnapshot(
        JNIEnv* env, jclass clazz, jfloatArray output) {
    (void)clazz;
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
