#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
#include <GameServiceKit/game_performance.h>
int main() {
    alarm(8); setbuf(stdout, nullptr);
    void* lib = dlopen("libgame_performance.z.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { printf("library error: %s\n", dlerror()); return 1; }
#define API(name) auto name = reinterpret_cast<decltype(&HMS_GamePerformance_##name)>(dlsym(lib, "HMS_GamePerformance_" #name)); if (!name) { puts("missing " #name); return 2; }
    API(CreateInitParameters) API(InitParameters_SetBundleName) API(InitParameters_SetAppVersion)
    API(Init) API(DestroyInitParameters) API(QueryGpuInfo) API(GpuInfo_GetGpuLoadLevel)
    API(GpuInfo_GetCurrentFrequency) API(DestroyGpuInfo)
    GamePerformance_InitParameters* p = nullptr;
    printf("create=%d\n", CreateInitParameters(&p));
    if (!p) return 3;
    // This is hmon's own identifier, not a third-party game or the hosting terminal's identity.
    printf("bundle(org.hmon.monitor)=%d\n", InitParameters_SetBundleName(p, "org.hmon.monitor"));
    printf("version=%d\n", InitParameters_SetAppVersion(p, "0.2.7"));
    auto initStatus = Init(p);
    printf("init=%d\n", initStatus);
    DestroyInitParameters(&p);
    if (initStatus != GAME_PERFORMANCE_SUCCESS) {
        puts(initStatus == GAME_PERFORMANCE_AUTH_FAILED ?
            "AUTH_FAILED: hmon's native process was not authenticated by GamePerformance. No GPU data available." :
            "Initialization failed; GPU queries require successful initialization.");
        dlclose(lib);
        return 4;
    }
    for (int i = 0; i < 3; ++i) {
        GamePerformance_GpuInfo* gpu = nullptr;
        printf("query[%d]=%d\n", i, QueryGpuInfo(&gpu));
        if (gpu) {
            int32_t level = -1, khz = -1;
            auto a = GpuInfo_GetGpuLoadLevel(gpu, &level);
            auto b = GpuInfo_GetCurrentFrequency(gpu, &khz);
            printf("loadStatus=%d level=%d (1..10, not percent) freqStatus=%d kHz=%d\n", a, level, b, khz);
            DestroyGpuInfo(&gpu);
        }
        sleep(1);
    }
    dlclose(lib);
}
