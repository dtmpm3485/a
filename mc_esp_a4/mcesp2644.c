#include <jni.h>
#include <android/log.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <shadowhook.h>

#define LOG_TAG "MCESP2644"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define DATA_DRIVEN_RENDER_RVA ((uintptr_t)0x0a5c8de8ULL)
#define MAX_ESP 256
#define STALE_MS 300ULL

enum { CAT_NONE=0, CAT_PLAYER=1, CAT_MOB=2, CAT_ANIMAL=3 };
typedef void (*render_fn)(void*, void*, void*);
typedef struct { uintptr_t actor; float x, top, bottom; int category; uint64_t seen_ms; } EspEntry;
typedef struct { float x,y,z; } V3;

static render_fn original_render = NULL;
static uintptr_t minecraft_load_bias = 0;
static int hook_state = 0;
static int esp_enabled = 1;
static int install_attempts = 0;
static uint64_t hook_calls = 0;
static uint64_t classified_calls = 0;
static uint64_t projected_calls = 0;
static uintptr_t last_unknown_rva = 0;
static char last_status[128] = "not installed";
static pthread_mutex_t esp_mutex = PTHREAD_MUTEX_INITIALIZER;
static EspEntry esp_entries[MAX_ESP];

static const uintptr_t PLAYER_VPTRS[] = {
    0x11f75760ULL,0x121dcde0ULL,0x121dd608ULL,0x1228cbf8ULL
};
static const uintptr_t LOCAL_PLAYER_VPTRS[] = { 0x11f74f58ULL };
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

static uint64_t now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000ULL+(uint64_t)ts.tv_nsec/1000000ULL; }
static uintptr_t strip_ptr(uintptr_t p){ return p & 0x00ffffffffffffffULL; }
static int in_list(uintptr_t v,const uintptr_t* a,size_t n){ for(size_t i=0;i<n;i++) if(a[i]==v) return 1; return 0; }
static int sane_v3(V3 v){ return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z)&&fabsf(v.x)<1e7f&&fabsf(v.y)<1e7f&&fabsf(v.z)<1e7f; }
static float dot3(V3 a,V3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static V3 sub3(V3 a,V3 b){ V3 r={a.x-b.x,a.y-b.y,a.z-b.z}; return r; }

static uintptr_t find_minecraft_bias(void){
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0;
    uintptr_t best=UINTPTR_MAX; char line[1024];
    while(fgets(line,sizeof(line),f)){
        unsigned long long start=0,off=0; char perms[8]={0}; int n=0;
        if(sscanf(line,"%llx-%*llx %7s %llx %*s %*s %n",&start,perms,&off,&n)<3) continue;
        char* p=line+n; p[strcspn(p,"\r\n")]=0;
        if(!strstr(p,"libminecraftpe.so")) continue;
        uintptr_t bias=(uintptr_t)start-(uintptr_t)off;
        if(bias<best) best=bias;
    }
    fclose(f); return best==UINTPTR_MAX?0:best;
}

static int readable(uintptr_t target,size_t size){
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0; char line[512]; int ok=0;
    uintptr_t e=target+size;
    while(fgets(line,sizeof(line),f)){
        unsigned long long s=0,en=0; char perms[8]={0};
        if(sscanf(line,"%llx-%llx %7s",&s,&en,perms)!=3) continue;
        if(perms[0]=='r'&&target>=(uintptr_t)s&&e<=(uintptr_t)en){ok=1;break;}
    }
    fclose(f); return ok;
}

static int classify_actor(void* actor){
    if(!actor||!minecraft_load_bias) return CAT_NONE;
    uintptr_t vptr=strip_ptr(*(uintptr_t*)actor); if(vptr<minecraft_load_bias) return CAT_NONE;
    uintptr_t rva=vptr-minecraft_load_bias;
    if(in_list(rva,LOCAL_PLAYER_VPTRS,sizeof(LOCAL_PLAYER_VPTRS)/sizeof(LOCAL_PLAYER_VPTRS[0]))) return CAT_NONE;
    if(in_list(rva,PLAYER_VPTRS,sizeof(PLAYER_VPTRS)/sizeof(PLAYER_VPTRS[0]))) return CAT_PLAYER;
    if(in_list(rva,ANIMAL_VPTRS,sizeof(ANIMAL_VPTRS)/sizeof(ANIMAL_VPTRS[0]))) return CAT_ANIMAL;
    if(in_list(rva,MOB_VPTRS,sizeof(MOB_VPTRS)/sizeof(MOB_VPTRS[0]))) return CAT_MOB;
    last_unknown_rva = rva;
    return CAT_NONE;
}

static int project_rel(V3 rel,V3 right,V3 up,V3 forward,float aspect,float fov,float* nx,float* ny){
    float z=dot3(rel,forward); if(z<0) z=-z;
    if(!isfinite(z)||z<0.05f||z>4096.0f) return 0;
    float fr=fov; if(fr>3.2f) fr*=0.01745329251994329577f;
    if(!(fr>0.15f&&fr<3.05f)||!(aspect>0.35f&&aspect<4.0f)) return 0;
    float t=tanf(fr*0.5f); if(!(t>0.01f&&t<100.0f)) return 0;
    float x=dot3(rel,right)/(z*t*aspect), y=dot3(rel,up)/(z*t);
    if(!isfinite(x)||!isfinite(y)||fabsf(x)>6.0f||fabsf(y)>6.0f) return 0;
    *nx=0.5f*(1.0f+x); *ny=0.5f*(1.0f-y); return 1;
}

static int project_actor(void* rc,void* ard,int cat,float* ox,float* ot,float* ob){
    if(!rc||!ard) return 0;
    V3 pos; memcpy(&pos,(uint8_t*)ard+0x10,sizeof(pos)); if(!sane_v3(pos)) return 0;
    void* sc=*(void**)((uint8_t*)rc+0x20); if(!sc) return 0;
    void* cam=*(void**)((uint8_t*)sc+0x18); if(!cam) return 0;

    V3 right,up,forward,cpos; float aspect,fov;
    memcpy(&right,(uint8_t*)cam+0x100,sizeof(right));
    memcpy(&up,(uint8_t*)cam+0x10c,sizeof(up));
    memcpy(&forward,(uint8_t*)cam+0x118,sizeof(forward));
    memcpy(&cpos,(uint8_t*)cam+0x124,sizeof(cpos));
    memcpy(&aspect,(uint8_t*)cam+0x130,sizeof(aspect));
    memcpy(&fov,(uint8_t*)cam+0x134,sizeof(fov));
    if(!sane_v3(right)||!sane_v3(up)||!sane_v3(forward)||!sane_v3(cpos)||!isfinite(aspect)||!isfinite(fov)) return 0;

    float h=cat==CAT_ANIMAL?1.35f:1.82f; V3 top=pos; top.y+=h;
    float bx,by,tx,ty;
    V3 rb=sub3(pos,cpos), rt=sub3(top,cpos);
    if(!project_rel(rb,right,up,forward,aspect,fov,&bx,&by) || !project_rel(rt,right,up,forward,aspect,fov,&tx,&ty)){
        if(!project_rel(pos,right,up,forward,aspect,fov,&bx,&by) || !project_rel(top,right,up,forward,aspect,fov,&tx,&ty)) return 0;
    }
    float t=fminf(by,ty), b=fmaxf(by,ty);
    if(b-t<0.003f||b-t>2.0f||bx<-0.3f||bx>1.3f||b<-0.3f||t>1.3f) return 0;
    *ox=bx; *ot=t; *ob=b; return 1;
}

static void update_esp(uintptr_t actor,int cat,float x,float top,float bottom){
    uint64_t t=now_ms(); pthread_mutex_lock(&esp_mutex);
    int free_slot=-1,oldest=0; uint64_t oldest_t=UINT64_MAX;
    for(int i=0;i<MAX_ESP;i++){
        if(esp_entries[i].actor==actor){ esp_entries[i]=(EspEntry){actor,x,top,bottom,cat,t}; pthread_mutex_unlock(&esp_mutex); return; }
        if(!esp_entries[i].actor&&free_slot<0) free_slot=i;
        if(esp_entries[i].seen_ms<oldest_t){oldest_t=esp_entries[i].seen_ms;oldest=i;}
    }
    int i=free_slot>=0?free_slot:oldest; esp_entries[i]=(EspEntry){actor,x,top,bottom,cat,t};
    pthread_mutex_unlock(&esp_mutex);
}

static void render_hook(void* self,void* rc,void* ard){
    __atomic_add_fetch(&hook_calls, 1, __ATOMIC_RELAXED);
    if(__atomic_load_n(&esp_enabled,__ATOMIC_RELAXED) && ard){
        void* actor=*(void**)ard; int cat=classify_actor(actor);
        if(cat){
            __atomic_add_fetch(&classified_calls, 1, __ATOMIC_RELAXED);
            float x,t,b;
            if(project_actor(rc,ard,cat,&x,&t,&b)) {
                __atomic_add_fetch(&projected_calls, 1, __ATOMIC_RELAXED);
                update_esp(strip_ptr((uintptr_t)actor),cat,x,t,b);
            }
        }
    }
    if(original_render) original_render(self,rc,ard);
}

JNIEXPORT jboolean JNICALL Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeInstallEsp(JNIEnv* env,jclass cls){
    (void)env;(void)cls;
    if(__atomic_load_n(&hook_state,__ATOMIC_ACQUIRE)==2) return JNI_TRUE;
    __atomic_add_fetch(&install_attempts, 1, __ATOMIC_RELAXED);
    minecraft_load_bias=find_minecraft_bias();
    if(!minecraft_load_bias){
        snprintf(last_status,sizeof(last_status),"waiting for libminecraftpe.so");
        LOGE("libminecraftpe.so not mapped");
        return JNI_FALSE;
    }

    uintptr_t target=minecraft_load_bias+DATA_DRIVEN_RENDER_RVA;
    static const uint8_t fp[]={0xe9,0x23,0xbc,0x6d,0xfd,0x7b,0x01,0xa9,0xf6,0x57,0x02,0xa9,0xf4,0x4f,0x03,0xa9};
    if(!readable(target,sizeof(fp))||memcmp((void*)target,fp,sizeof(fp))!=0){
        snprintf(last_status,sizeof(last_status),"fingerprint mismatch @ 0x%llx",(unsigned long long)DATA_DRIVEN_RENDER_RVA);
        LOGE("26.44.3 fingerprint mismatch");
        return JNI_FALSE;
    }

    int init = shadowhook_init(SHADOWHOOK_MODE_UNIQUE,false);
    if(init!=SHADOWHOOK_ERRNO_OK && __atomic_load_n(&hook_state,__ATOMIC_ACQUIRE)!=2){
        snprintf(last_status,sizeof(last_status),"shadowhook init failed: %d",init);
        LOGE("shadowhook_init failed");
        return JNI_FALSE;
    }
    void* stub=shadowhook_hook_func_addr((void*)target,(void*)render_hook,(void**)&original_render);
    if(!stub||!original_render){
        int e=shadowhook_get_errno();
        snprintf(last_status,sizeof(last_status),"hook failed: %d",e);
        LOGE("shadowhook_hook_func_addr failed: %d",e);
        return JNI_FALSE;
    }
    snprintf(last_status,sizeof(last_status),"hook installed");
    __atomic_store_n(&hook_state,2,__ATOMIC_RELEASE); LOGI("ESP installed"); return JNI_TRUE;
}

JNIEXPORT jint JNICALL Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeFillEspSnapshot(JNIEnv* env,jclass cls,jfloatArray output){
    (void)cls;
    if(!__atomic_load_n(&esp_enabled,__ATOMIC_RELAXED)) return 0;
    if(!output) return 0; jsize cap=(*env)->GetArrayLength(env,output); if(cap<4) return 0;
    jfloat* out=(*env)->GetFloatArrayElements(env,output,NULL); if(!out) return 0;
    int max=cap/4,count=0; uint64_t t=now_ms(); pthread_mutex_lock(&esp_mutex);
    for(int i=0;i<MAX_ESP&&count<max;i++){
        EspEntry* e=&esp_entries[i]; if(!e->actor) continue;
        if(t-e->seen_ms>STALE_MS){ memset(e,0,sizeof(*e)); continue; }
        out[count*4]=e->x; out[count*4+1]=e->top; out[count*4+2]=e->bottom; out[count*4+3]=(float)e->category; count++;
    }
    pthread_mutex_unlock(&esp_mutex); (*env)->ReleaseFloatArrayElements(env,output,out,0); return count;
}


JNIEXPORT jstring JNICALL Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeGetDebugStatus(JNIEnv* env,jclass cls){
    (void)cls;
    char buf[384];
    snprintf(buf,sizeof(buf),
        "A5 %s | attempts=%d | calls=%llu | classified=%llu | projected=%llu | base=0x%llx | unknown=0x%llx",
        last_status,
        __atomic_load_n(&install_attempts,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&hook_calls,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&classified_calls,__ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&projected_calls,__ATOMIC_RELAXED),
        (unsigned long long)minecraft_load_bias,
        (unsigned long long)last_unknown_rva);
    return (*env)->NewStringUTF(env,buf);
}


JNIEXPORT void JNICALL Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeSetEspEnabled(
        JNIEnv* env,jclass cls,jboolean enabled){
    (void)env;(void)cls;
    __atomic_store_n(&esp_enabled, enabled ? 1 : 0, __ATOMIC_RELEASE);
    if(!enabled){
        pthread_mutex_lock(&esp_mutex);
        memset(esp_entries,0,sizeof(esp_entries));
        pthread_mutex_unlock(&esp_mutex);
    }
    LOGI("ESP %s", enabled ? "ON" : "OFF");
}

JNIEXPORT jboolean JNICALL Java_org_levimc_launcher_core_minecraft_EspOverlayView_nativeIsEspEnabled(
        JNIEnv* env,jclass cls){
    (void)env;(void)cls;
    return __atomic_load_n(&esp_enabled,__ATOMIC_ACQUIRE) ? JNI_TRUE : JNI_FALSE;
}
