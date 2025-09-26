#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <libgen.h>
#include <errno.h>

static std::string g_game_data_dir;
static std::string g_dump_path;

static bool mkdir_recursive(const std::string &path) {
    if (path.empty()) return false;
    size_t pos = 0;
    do {
        pos = path.find_first_of('/', pos + 1);
        std::string sub = path.substr(0, pos == std::string::npos ? path.size() : pos);
        if (sub.empty()) continue;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0) {
            if (mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                return false;
            }
        } else if (!S_ISDIR(st.st_mode)) {
            return false;
        }
    } while (pos != std::string::npos);
    return true;
}

static void safe_il2cpp_dump() {
    if (g_dump_path.empty()) return;
    mkdir_recursive(g_dump_path);
    il2cpp_dump(g_dump_path.c_str());
}

void* (*old_dlopen)(const char*, int) = nullptr;
void* my_dlopen(const char* filename, int flag) {
    if (filename && strstr(filename, "libil2cpp.so")) {
        LOGI("libil2cpp.so sedang di-load: %s", filename);
        void* handle = old_dlopen(filename, flag);
        if (handle) {
            il2cpp_api_init(handle);
            safe_il2cpp_dump();
        }
        return handle;
    }
    return old_dlopen(filename, flag);
}

using android_dlopen_ext_t = void*(*)(const char* filename, int flags, void* ext);
android_dlopen_ext_t old_android_dlopen_ext = nullptr;

void* my_android_dlopen_ext(const char* filename, int flags, void* ext) {
    if (filename && strstr(filename, "libil2cpp.so")) {
        LOGI("android_dlopen_ext: libil2cpp.so sedang di-load: %s", filename);
        void* handle = old_android_dlopen_ext ? old_android_dlopen_ext(filename, flags, ext) : nullptr;
        if (!handle) handle = dlopen(filename, flags);
        if (handle) {
            il2cpp_api_init(handle);
            safe_il2cpp_dump();
        }
        return handle;
    }
    return old_android_dlopen_ext ? old_android_dlopen_ext(filename, flags, ext) : nullptr;
}

void hack_start(const char *game_data_dir) {
    if (game_data_dir && strlen(game_data_dir) > 0) {
        g_game_data_dir = std::string(game_data_dir);
        g_dump_path = g_game_data_dir + "/dump";
    }
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            il2cpp_api_init(handle);
            safe_il2cpp_dump();
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (!activity_thread_clz) { LOGE("ActivityThread not found"); return {}; }

    jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                            "currentApplication",
                                                            "()Landroid/app/Application;");
    if (!currentApplicationId) { LOGE("currentApplication not found"); return {}; }

    jobject application = env->CallStaticObjectMethod(activity_thread_clz, currentApplicationId);
    jclass application_clazz = env->GetObjectClass(application);
    if (!application_clazz) { LOGE("application class not found"); return {}; }

    jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                      "getApplicationInfo",
                                                      "()Landroid/content/pm/ApplicationInfo;");
    if (!get_application_info) { LOGE("getApplicationInfo not found"); return {}; }

    jobject application_info = env->CallObjectMethod(application, get_application_info);
    jfieldID native_library_dir_id = env->GetFieldID(env->GetObjectClass(application_info),
                                                     "nativeLibraryDir",
                                                     "Ljava/lang/String;");
    if (!native_library_dir_id) { LOGE("nativeLibraryDir not found"); return {}; }

    jstring native_library_dir_jstring = (jstring) env->GetObjectField(application_info, native_library_dir_id);
    const char* path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
    std::string lib_dir(path);
    env->ReleaseStringUTFChars(native_library_dir_jstring, path);
    LOGI("lib dir %s", lib_dir.c_str());
    return lib_dir;
}

static std::string GetNativeBridgeLibrary() {
    std::array<char, PROP_VALUE_MAX> value;
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;
    void *(*loadLibrary)(const char *libpath, int flag);
    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);
    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;
    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
        hack_start(game_data_dir);
    }
#else
    hack_start(game_data_dir);
#endif
}

#if defined(__arm__) || defined(__aarch64__)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    if (game_data_dir && strlen(game_data_dir) > 0) {
        g_game_data_dir = std::string(game_data_dir);
        g_dump_path = g_game_data_dir + "/dump";
    } else {
        LOGI("JNI_OnLoad: reserved game_data_dir null");
    }
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}
#endif
