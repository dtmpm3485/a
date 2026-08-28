#include <jni.h>
#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <shadowhook.h>

#define LOG_TAG "MCESP2644A8"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LEVEL_RENDERER_CAMERA_RENDER_RVA ((uintptr_t)0x0ae1b6e0ULL)
#define GATHER_ACTOR_CANDIDATES_RVA       ((uintptr_t)0x0ae68d90ULL)
#define LOCAL_PLAYER_VPTR_RVA             ((uintptr_t)0x11f74f58ULL)

#define MAX_ESP 512
#define STALE_MS 450ULL
#define PROJECT_INTERVAL_MS 33ULL
#define DISCOVERY_INTERVAL_MS 750ULL

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
typedef void (*gather_candidates_fn)(void*, void*, void*, float);

static level_render_fn original_level_render = NULL;
static gather_candidates_fn original_gather_candidates = NULL;

static uintptr_t minecraft_load_bias = 0;
static uintptr_t discovered_builtins_offset = 0;
static int hook_state = 0;
static int esp_enabled = 1;

static uint64_t render_calls = 0;
static uint64_t gather_calls = 0;
static uint64_t actors_seen = 0;
static uint64_t classified_calls = 0;
static uint64_t position_ok = 0;
static uint64_t projected_calls = 0;
static uint64_t camera_ok = 0;
static uint64_t last_project_ms = 0;
static uint64_t last_discovery_ms = 0;
static uint32_t last_candidate_count = 0;
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
    if (!target || !size || target + size < target) return 0;
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

static float dot3(V3 a,V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static V3 sub3(V3 a,V3 b) { V3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }

static int in_list(uintptr_t v,const uintptr_t* list,size_t count) {
    for (size_t i=0;i<count;i++) if (list[i]==v) return 1;
    return 0;
}

static int actor_pointer_plausible(uintptr_t actor) {
    actor = strip_ptr(actor);
    if (!actor || !readable(actor, 8)) return 0;
    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    return vptr >= minecraft_load_bias &&
           vptr < minecraft_load_bias + 0x13000000ULL;
}

static int classify_actor(void* actor) {
    if (!actor || !minecraft_load_bias || !readable((uintptr_t)actor, 8)) return CAT_NONE;
    uintptr_t vptr = strip_ptr(*(uintptr_t*)actor);
    if (vptr < minecraft_load_bias) return CAT_NONE;
    uintptr_t rva = vptr - minecraft_load_bias;

    if (rva == LOCAL_PLAYER_VPTR_RVA) return CAT_NONE;
    if (in_list(rva,PLAYER_VPTRS,sizeof(PLAYER_VPTRS)/sizeof(PLAYER_VPTRS[0]))) return CAT_PLAYER;
    if (in_list(rva,ANIMAL_VPTRS,sizeof(ANIMAL_VPTRS)/sizeof(ANIMAL_VPTRS[0]))) return CAT_ANIMAL;
    if (in_list(rva,MOB_VPTRS,sizeof(MOB_VPTRS)/sizeof(MOB_VPTRS[0]))) return CAT_MOB;
    return CAT_NONE;
}

static int valid_aabb(V3 mn,V3 mx) {
    if (!sane_v3(mn) || !sane_v3(mx)) return 0;
    float dx=mx.x-mn.x, dy=mx.y-mn.y, dz=mx.z-mn.z;
    return dx>=0 && dy>=0 && dz>=0 && dx<20 && dy<30 && dz<20;
}

static uintptr_t discover_builtins(void* actor) {
    if (discovered_builtins_offset) return discovered_builtins_offset;
    uint64_t t=now_ms();
    if (t-last_discovery_ms < DISCOVERY_INTERVAL_MS) return 0;
    last_discovery_ms=t;

    if (!actor || !readable((uintptr_t)actor,0x700)) return 0;

    for (uintptr_t off=0x100; off<=0x600; off+=8) {
        void* state=NULL; void* shape=NULL;
        memcpy(&state,(uint8_t*)actor+off,8);
        memcpy(&shape,(uint8_t*)actor+off+8,8);
        state=(void*)strip_ptr((uintptr_t)state);
        shape=(void*)strip_ptr((uintptr_t)shape);
        if (!state || !shape) continue;
        if (!readable((uintptr_t)state,12) || !readable((uintptr_t)shape,24)) continue;

        V3 p,mn,mx;
        memcpy(&p,state,12);
        memcpy(&mn,shape,12);
        memcpy(&mx,(uint8_t*)shape+12,12);
        if (!sane_v3(p) || !valid_aabb(mn,mx)) continue;

        float cx=(mn.x+mx.x)*0.5f, cz=(mn.z+mx.z)*0.5f;
        if (fabsf(p.x-cx)>8 || fabsf(p.z-cz)>8 ||
            p.y<mn.y-8 || p.y>mx.y+8) continue;

        discovered_builtins_offset=off;
        LOGI("Actor layout discovered: 0x%llx",(unsigned long long)off);
        return off;
    }
    return 0;
}

static int actor_box(void* actor,V3* bottom,V3* top) {
    uintptr_t off=discover_builtins(actor);
    if (!off || !readable((uintptr_t)actor+off,16)) return 0;

    void* state=NULL; void* shape=NULL;
    memcpy(&state,(uint8_t*)actor+off,8);
    memcpy(&shape,(uint8_t*)actor+off+8,8);
    state=(void*)strip_ptr((uintptr_t)state);
    shape=(void*)strip_ptr((uintptr_t)shape);

    if (shape && readable((uintptr_t)shape,24)) {
        V3 mn,mx;
        memcpy(&mn,shape,12);
        memcpy(&mx,(uint8_t*)shape+12,12);
        if (valid_aabb(mn,mx)) {
            float cx=(mn.x+mx.x)*0.5f, cz=(mn.z+mx.z)*0.5f;
            *bottom=(V3){cx,mn.y,cz};
            *top=(V3){cx,mx.y,cz};
            __atomic_add_fetch(&position_ok,1,__ATOMIC_RELAXED);
            return 1;
        }
    }

    if (state && readable((uintptr_t)state,12)) {
        V3 p;
        memcpy(&p,state,12);
        if (sane_v3(p)) {
            *bottom=p; *top=p; top->y+=1.8f;
            __atomic_add_fetch(&position_ok,1,__ATOMIC_RELAXED);
            return 1;
        }
    }
    return 0;
}

static int read_camera(void* render_context,
                       V3* right,V3* up,V3* forward,V3* camera_pos,
                       float* aspect,float* fov) {
    if (!render_context || !readable((uintptr_t)render_context+0x28,8)) return 0;
    void* screen_context=*(void**)((uint8_t*)render_context+0x28);
    if (!screen_context || !readable((uintptr_t)screen_context+0x18,8)) return 0;
    void* camera=*(void**)((uint8_t*)screen_context+0x18);
    if (!camera || !readable((uintptr_t)camera+0x138,4)) return 0;

    memcpy(right,(uint8_t*)camera+0x100,12);
    memcpy(up,(uint8_t*)camera+0x10c,12);
    memcpy(forward,(uint8_t*)camera+0x118,12);
    memcpy(camera_pos,(uint8_t*)camera+0x124,12);
    memcpy(aspect,(uint8_t*)camera+0x130,4);
    memcpy(fov,(uint8_t*)camera+0x134,4);

    int ok=sane_v3(*right)&&sane_v3(*up)&&sane_v3(*forward)&&
           sane_v3(*camera_pos)&&isfinite(*aspect)&&isfinite(*fov);
    if (ok) __atomic_add_fetch(&camera_ok,1,__ATOMIC_RELAXED);
    return ok;
}

static int project_rel(V3 rel,V3 right,V3 up,V3 forward,
                       float aspect,float fov,float* nx,float* ny) {
    float z=dot3(rel,forward);
    if (z<0) z=-z;
    if (!isfinite(z)||z<0.05f||z>8192) return 0;
    float fr=fov;
    if (fr>3.2f) fr*=0.01745329251994329577f;
    if (!(fr>0.15f&&fr<3.05f)||!(aspect>0.35f&&aspect<4.0f)) return 0;
    float t=tanf(fr*0.5f);
    if (!(t>0.01f&&t<100)) return 0;
    float x=dot3(rel,right)/(z*t*aspect);
    float y=dot3(rel,up)/(z*t);
    if (!isfinite(x)||!isfinite(y)||fabsf(x)>8||fabsf(y)>8) return 0;
    *nx=0.5f*(1+x); *ny=0.5f*(1-y);
    return 1;
}

static void update_esp(uintptr_t actor,int category,float x,float top,float bottom) {
    uint64_t t=now_ms();
    pthread_mutex_lock(&esp_mutex);
    int free_slot=-1,oldest=0; uint64_t oldest_t=UINT64_MAX;
    for (int i=0;i<MAX_ESP;i++) {
        if (esp_entries[i].actor==actor) {
            esp_entries[i]=(EspEntry){actor,x,top,bottom,category,t};
            pthread_mutex_unlock(&esp_mutex); return;
        }
        if (!esp_entries[i].actor&&free_slot<0) free_slot=i;
        if (esp_entries[i].seen_ms<oldest_t) { oldest_t=esp_entries[i].seen_ms; oldest=i; }
    }
    int slot=free_slot>=0?free_slot:oldest;
    esp_entries[slot]=(EspEntry){actor,x,top,bottom,category,t};
    pthread_mutex_unlock(&esp_mutex);
}

static void snapshot_candidates(void* candidates) {
    if (!candidates || !readable((uintptr_t)candidates,24)) return;

    uintptr_t begin=0,end=0,cap=0;
    memcpy(&begin,(uint8_t*)candidates+0,8);
    memcpy(&end,(uint8_t*)candidates+8,8);
    memcpy(&cap,(uint8_t*)candidates+16,8);
    begin=strip_ptr(begin); end=strip_ptr(end); cap=strip_ptr(cap);

    if (!begin || end<begin || cap<end) {
        last_candidate_count=0;
        return;
    }

    uintptr_t bytes=end-begin;
    if (bytes%8 || bytes/8>4096 || (bytes && !readable(begin,(size_t)bytes))) {
        last_candidate_count=0;
        return;
    }

    uintptr_t temp[MAX_ESP];
    uint32_t count=0;
    uint64_t n=bytes/8;
    for (uint64_t i=0;i<n && count<MAX_ESP;i++) {
        uintptr_t actor=0;
        memcpy(&actor,(void*)(begin+i*8),8);
        actor=strip_ptr(actor);
        if (!actor_pointer_plausible(actor)) continue;
        temp[count++]=actor;
    }

    pthread_mutex_lock(&candidate_mutex);
    candidate_count=count;
    if (count) memcpy(candidate_actors,temp,count*sizeof(uintptr_t));
    pthread_mutex_unlock(&candidate_mutex);
    last_candidate_count=count;
}

static void gather_candidates_hook(void* candidates,void* source,void* camera_pos,float distance) {
    __atomic_add_fetch(&gather_calls,1,__ATOMIC_RELAXED);
    if (original_gather_candidates)
        original_gather_candidates(candidates,source,camera_pos,distance);
    if (__atomic_load_n(&esp_enabled,__ATOMIC_RELAXED))
        snapshot_candidates(candidates);
}

static void project_snapshot(void* render_context) {
    uint64_t t=now_ms();
    if (t-last_project_ms<PROJECT_INTERVAL_MS) return;
    last_project_ms=t;

    uintptr_t local[MAX_ESP];
    uint32_t count=0;
    pthread_mutex_lock(&candidate_mutex);
    count=candidate_count>MAX_ESP?MAX_ESP:candidate_count;
    if (count) memcpy(local,candidate_actors,count*sizeof(uintptr_t));
    pthread_mutex_unlock(&candidate_mutex);
    if (!count) return;

    V3 right,up,forward,camera_pos;
    float aspect=0,fov=0;
    if (!read_camera(render_context,&right,&up,&forward,&camera_pos,&aspect,&fov)) return;

    for (uint32_t i=0;i<count;i++) {
        void* actor=(void*)local[i];
        if (!actor_pointer_plausible((uintptr_t)actor)) continue;
        __atomic_add_fetch(&actors_seen,1,__ATOMIC_RELAXED);

        int category=classify_actor(actor);
        if (!category) continue;
        __atomic_add_fetch(&classified_calls,1,__ATOMIC_RELAXED);

        V3 bw,tw;
        if (!actor_box(actor,&bw,&tw)) continue;

        V3 rb=sub3(bw,camera_pos), rt=sub3(tw,camera_pos);
        float bx,by,tx,ty;
        if (!project_rel(rb,right,up,forward,aspect,fov,&bx,&by) ||
            !project_rel(rt,right,up,forward,aspect,fov,&tx,&ty)) continue;

        float top=fminf(by,ty), bottom=fmaxf(by,ty);
        if (bottom-top<0.002f||bottom-top>3.0f) continue;
        if (bx<-0.5f||bx>1.5f||bottom<-0.5f||top>1.5f) continue;

        __atomic_add_fetch(&projected_calls,1,__ATOMIC_RELAXED);
        update_esp((uintptr_t)actor,category,bx,top,bottom);
    }
}

static void level_render_hook(void* self,void* render_context,void* render_obj,void* client_instance) {
    __atomic_add_fetch(&render_calls,1,__ATOMIC_RELAXED);
    if (__atomic_load_n(&esp_enabled,__ATOMIC_RELAXED))
        project_snapshot(render_context);
    if (original_level_render)
        original_level_render(self,render_context,render_obj,client_instance);
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeInstallEsp(JNIEnv* env,jclass cls) {
    (void)env;(void)cls;
    if (__atomic_load_n(&hook_state,__ATOMIC_ACQUIRE)==2) return JNI_TRUE;

    minecraft_load_bias=find_minecraft_bias();
    if (!minecraft_load_bias) {
        snprintf(last_status,sizeof(last_status),"waiting for libminecraftpe");
        return JNI_FALSE;
    }

    uintptr_t render_target=minecraft_load_bias+LEVEL_RENDERER_CAMERA_RENDER_RVA;
    uintptr_t gather_target=minecraft_load_bias+GATHER_ACTOR_CANDIDATES_RVA;

    static const uint8_t render_fp[]={
        0xff,0x43,0x04,0xd1,0xea,0x53,0x00,0xfd,
        0xe9,0xa3,0x0a,0x6d,0xfd,0xfb,0x0b,0xa9
    };
    static const uint8_t gather_fp[]={
        0xff,0x83,0x02,0xd1,0xfd,0x7b,0x04,0xa9,
        0xfb,0x2b,0x00,0xf9,0xfa,0x67,0x06,0xa9
    };

    if (!readable(render_target,sizeof(render_fp)) ||
        memcmp((void*)render_target,render_fp,sizeof(render_fp))!=0) {
        snprintf(last_status,sizeof(last_status),"render fingerprint mismatch");
        return JNI_FALSE;
    }
    if (!readable(gather_target,sizeof(gather_fp)) ||
        memcmp((void*)gather_target,gather_fp,sizeof(gather_fp))!=0) {
        snprintf(last_status,sizeof(last_status),"gather fingerprint mismatch");
        return JNI_FALSE;
    }

    shadowhook_init(SHADOWHOOK_MODE_UNIQUE,false);

    void* rstub=shadowhook_hook_func_addr(
        (void*)render_target,(void*)level_render_hook,(void**)&original_level_render);
    if (!rstub||!original_level_render) {
        snprintf(last_status,sizeof(last_status),"render hook failed:%d",shadowhook_get_errno());
        return JNI_FALSE;
    }

    void* gstub=shadowhook_hook_func_addr(
        (void*)gather_target,(void*)gather_candidates_hook,(void**)&original_gather_candidates);
    if (!gstub||!original_gather_candidates) {
        snprintf(last_status,sizeof(last_status),"gather hook failed:%d",shadowhook_get_errno());
        return JNI_FALSE;
    }

    snprintf(last_status,sizeof(last_status),"render+candidate hooks installed");
    __atomic_store_n(&hook_state,2,__ATOMIC_RELEASE);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeFillEspSnapshot(
        JNIEnv* env,jclass cls,jfloatArray output) {
    (void)cls;
    if (!__atomic_load_n(&esp_enabled,__ATOMIC_RELAXED) || !output) return 0;
    jsize capacity=(*env)->GetArrayLength(env,output);
    if (capacity<4) return 0;
    jfloat* out=(*env)->GetFloatArrayElements(env,output,NULL);
    if (!out) return 0;

    int max_count=capacity/4,count=0;
    uint64_t t=now_ms();
    pthread_mutex_lock(&esp_mutex);
    for (int i=0;i<MAX_ESP && count<max_count;i++) {
        EspEntry* e=&esp_entries[i];
        if (!e->actor) continue;
        if (t-e->seen_ms>STALE_MS) { memset(e,0,sizeof(*e)); continue; }
        out[count*4]=e->x;
        out[count*4+1]=e->top;
        out[count*4+2]=e->bottom;
        out[count*4+3]=(float)e->category;
        count++;
    }
    pthread_mutex_unlock(&esp_mutex);
    (*env)->ReleaseFloatArrayElements(env,output,out,0);
    return count;
}

JNIEXPORT jstring JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeGetDebugStatus(
        JNIEnv* env,jclass cls) {
    (void)cls;
    char buf[440];
    snprintf(buf,sizeof(buf),
        "A8 %s | render=%llu | gather=%llu | cand=%u | seen=%llu | pos=%llu | class=%llu | proj=%llu | cam=%llu | layout=0x%llx",
        last_status,
        (unsigned long long)__atomic_load_n(&render_calls,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&gather_calls,__ATOMIC_RELAXED),
        last_candidate_count,
        (unsigned long long)__atomic_load_n(&actors_seen,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&position_ok,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&classified_calls,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&projected_calls,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&camera_ok,__ATOMIC_RELAXED),
        (unsigned long long)discovered_builtins_offset);
    return (*env)->NewStringUTF(env,buf);
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeSetEspEnabled(
        JNIEnv* env,jclass cls,jboolean enabled) {
    (void)env;(void)cls;
    __atomic_store_n(&esp_enabled,enabled?1:0,__ATOMIC_RELEASE);
    if (!enabled) {
        pthread_mutex_lock(&esp_mutex);
        memset(esp_entries,0,sizeof(esp_entries));
        pthread_mutex_unlock(&esp_mutex);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeIsEspEnabled(
        JNIEnv* env,jclass cls) {
    (void)env;(void)cls;
    return __atomic_load_n(&esp_enabled,__ATOMIC_ACQUIRE)?JNI_TRUE:JNI_FALSE;
}
