#include <pch.h>

#include "processing.h"
#include "unreal.h"

using ohl::unreal::FJsonObject;
using ohl::unreal::FSparkRequest;
using ohl::unreal::FString;
using ohl::unreal::TSharedPtr;

namespace ohl::hooks {

#pragma region Types

typedef void* (*fmemory_malloc)(size_t count, uint32_t align);
typedef void* (*fmemory_realloc)(void* original, size_t count, uint32_t align);
typedef void (*fmemory_free)(void* data);
typedef int32_t (*get_services_verification)(void* this_api,
                                             FString* uuid,
                                             FString* consumer,
                                             FString* platform,
                                             FString* hardware,
                                             FString* title,
                                             void* g,
                                             void* h,
                                             void* i,
                                             void* j);
typedef bool (*discovery_from_json)(void* this_service, FJsonObject** json);
typedef bool (*news_from_json)(void* this_response, FJsonObject** json);
typedef void (*add_image_to_cache)(void* this_image_manager, TSharedPtr<FSparkRequest>* req);

/**
 * @brief Struct holding all the game functions we scan for.
 */
struct game_functions {
    fmemory_malloc malloc;
    fmemory_realloc realloc;
    fmemory_free free;
    get_services_verification get_verification;
    discovery_from_json discovery;
    news_from_json news;
    add_image_to_cache image_cache;
};

static game_functions funcs = {};

#pragma endregion

#pragma region Sig Scanning

/**
 * @brief Struct holding information about a sigscan.
 */
struct sigscan_pattern {
    const uint8_t* bytes;
    const uint8_t* mask;
    const size_t size;
};

/**
 * @brief Helper to convert strings into a sigscan pattern.
 *
 * @tparam n The length of the strings (should be picked up automatically).
 * @return A sigscan pattern.
 */
template <size_t n>
constexpr sigscan_pattern make_pattern(const char (&bytes)[n], const char (&mask)[n]) {
    return sigscan_pattern{reinterpret_cast<const uint8_t*>(bytes),
                           reinterpret_cast<const uint8_t*>(mask), n - 1};
}

static const sigscan_pattern malloc_pattern = make_pattern(
    "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x20\x48\x8B\xF9\x8B\xDA\x48\x8B\x0D\x00\x00\x00\x00\x48"
    "\x85\xC9",
    "\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF"
    "\xFF\xFF");

static const sigscan_pattern realloc_pattern = make_pattern(
    "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x20\x48\x8B\xF1\x41\x8B\xD8\x48\x8B"
    "\x0D\x00\x00\x00\x00\x48\x8B\xFA",
    "\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
    "\xFF\x00\x00\x00\x00\xFF\xFF\xFF");

static const sigscan_pattern free_pattern = make_pattern(
    "\x48\x85\xC9\x74\x00\x53\x48\x83\xEC\x20\x48\x8B\xD9\x48\x8B\x0D\x00\x00\x00\x00",
    "\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00");

static const sigscan_pattern get_verification_pattern = make_pattern(
    "\x40\x55\x53\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\xAC\x24\x00\x00\x00\x00\x48\x81"
    "\xEC\xE8\x02\x00\x00\x48\x8B\x05\x00\x00\x00\x00\x48\x33\xC4\x48\x89\x85\x00\x00\x00\x00\x48"
    "\x8B\x85\x00\x00\x00\x00\x4D\x8B\xE0",
    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF"
    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00\x00\x00\x00\xFF"
    "\xFF\xFF\x00\x00\x00\x00\xFF\xFF\xFF");

static const sigscan_pattern discovery_pattern = make_pattern(
    "\x40\x55\x53\x57\x48\x8D\x6C\x24\x00\x48\x81\xEC\x90\x00\x00\x00\x48\x83\x3A\x00\x48\x8B\xDA"
    "\x48\x8B\xF9\x75\x00\x32\xC0\x48\x81\xC4\x90\x00\x00\x00\x5F\x5B\x5D\xC3\x4C\x89\xBC\x24\x00"
    "\x00\x00\x00",
    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
    "\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00"
    "\x00\x00\x00");

static const sigscan_pattern news_pattern = make_pattern(
    "\x40\x55\x53\x57\x48\x8D\x6C\x24\x00\x48\x81\xEC\x90\x00\x00\x00\x48\x83\x3A\x00\x48\x8B\xDA"
    "\x48\x8B\xF9\x75\x00\x32\xC0\x48\x81\xC4\x90\x00\x00\x00\x5F\x5B\x5D\xC3\x48\x89\xB4\x24\x00"
    "\x00\x00\x00",
    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
    "\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00"
    "\x00\x00\x00");

static const sigscan_pattern image_cache_pattern = make_pattern(
    "\x40\x55\x41\x54\x41\x55\x41\x56\x48\x8D\x6C\x24\x00\x48\x81\xEC\xA8\x00\x00\x00",
    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF");

/**
 * @brief Performs a sigscan.
 * @note Throws a runtime error on scan failure.
 *
 * @tparam T The type to cast the return to.
 * @param start The address to start the search at.
 * @param size The length of the region to search.
 * @param pattern The pattern to search for.
 * @return The found location, cast to the relevant type.
 */
template <typename T>
static T sigscan(void* start, size_t size, const sigscan_pattern& pattern) {
    // The naive O(nm) search works well enough, even repeating it for each different pattern
    for (auto i = 0; i < (size - pattern.size); i++) {
        bool found = true;
        for (auto j = 0; j < pattern.size; j++) {
            auto val = reinterpret_cast<uint8_t*>(start)[i + j];
            if ((val & pattern.mask[j]) != pattern.bytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return reinterpret_cast<T>(&reinterpret_cast<uint8_t*>(start)[i]);
        }
    }

    throw std::runtime_error("Sigscan failed");
}

#pragma endregion

#pragma region Wrappers

/**
 * @brief Detour function for `GbxSparkSdk::Discovery::Api::GetServicesVerification`
 *
 * @param this_api The api object this was called on.
 * @param uuid The authentication uuid.
 * @param consumer The consumer of this api call (i.e. `client`).
 * @param platform The platform this call is on (i.e. `steam`/`epic`)
 * @param hardware The hardware this call is on (i.e. `pc`)
 * @param title The title this call is for (i.e. `oak`/`daffodil`)
 * @param g ¯\_(ツ)_/¯
 * @param h ¯\_(ツ)_/¯
 * @param i ¯\_(ツ)_/¯
 * @param j ¯\_(ツ)_/¯
 * @return ¯\_(ツ)_/¯
 */
static get_services_verification original_get_verification = nullptr;
bool detour_get_verification(void* this_api,
                             FString* uuid,
                             FString* consumer,
                             FString* platform,
                             FString* hardware,
                             FString* title,
                             void* g,
                             void* h,
                             void* i,
                             void* j) {
    try {
        LOGD << "[OHL] Hit GetServicesVerification detour";
        ohl::processing::handle_get_verification();
    } catch (std::exception ex) {
        LOGE << "[OHL] Exception occured in get verification hook: " << ex.what();
    }

    return original_get_verification(this_api, uuid, consumer, platform, hardware, title, g, h, i,
                                     j);
}

/**
 * @brief Detour function for `GbxSparkSdk::Discovery::Services::FromJson`.
 *
 * @param this_service The service object this was called on.
 * @param json Unreal json objects containing the received data.
 * @return ¯\_(ツ)_/¯
 */
static discovery_from_json original_discovery_from_json = nullptr;
bool detour_discovery_from_json(void* this_service, FJsonObject** json) {
    try {
        LOGD << "[OHL] Hit Discovery::Services::FromJson detour";
        ohl::processing::handle_discovery_from_json(json);
    } catch (std::exception ex) {
        LOGE << "[OHL] Exception occured in discovery hook: " << ex.what();
    }

    return original_discovery_from_json(this_service, json);
}

/**
 * @brief Detour function for `GbxSparkSdk::News::NewsResponse::FromJson`.
 *
 * @param this_service The service object this was called on.
 * @param json Unreal json objects containing the received data.
 * @return ¯\_(ツ)_/¯
 */
static news_from_json original_news_from_json = nullptr;
bool detour_news_from_json(void* this_service, FJsonObject** json) {
    try {
        LOGD << "[OHL] Hit News::NewsResponse::FromJson detour";
        ohl::processing::handle_news_from_json(json);
    } catch (std::exception ex) {
        LOGE << "[OHL] Exception occured in news hook: " << ex.what();
    }

    return original_news_from_json(this_service, json);
}

/**
 * @brief Detour function for `FOnlineImageManager::AddImageToFileCache`.
 *
 * @param this_service The image manager object this was called on.
 * @param json A pointer to the spark request for this image.
 * @return ¯\_(ツ)_/¯
 */
static add_image_to_cache original_add_image_to_cache = nullptr;
void detour_add_image_to_cache(void* this_image_manager, TSharedPtr<FSparkRequest>* req) {
    bool may_continue = true;
    try {
        LOGD << "[OHL] Hit AddImageToFileCache detour";
        may_continue = ohl::processing::handle_add_image_to_cache(req);
    } catch (std::exception ex) {
        LOGE << "[OHL] Exception occured in image cache hook: " << ex.what();
    }

    if (may_continue) {
        original_add_image_to_cache(this_image_manager, req);
    }
}

void* malloc_raw(size_t count) {
    if (funcs.malloc == nullptr) {
        throw std::runtime_error("Tried to call malloc, which was not found!");
    }
    auto ret = funcs.malloc(count, 8);
    if (ret == nullptr) {
        throw std::runtime_error("Failed to allocate memory!");
    }
    memset(ret, 0, count);
    return ret;
}

void* realloc_raw(void* original, size_t count) {
    if (funcs.realloc == nullptr) {
        throw std::runtime_error("Tried to call realloc, which was not found!");
    }
    auto ret = funcs.realloc(original, count, 8);
    if (ret == nullptr) {
        throw std::runtime_error("Failed to re-allocate memory!");
    }
    return ret;
}

void free(void* data) {
    if (funcs.free == nullptr) {
        throw std::runtime_error("Tried to call free, which was not found!");
    }
    funcs.free(data);
}

#pragma endregion

void init(void) {
    LOGD << "[OHL] Initalizing hooks";

    auto exe_module = GetModuleHandle(NULL);

    MEMORY_BASIC_INFORMATION mem;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(exe_module), &mem, sizeof(mem))) {
        throw std::runtime_error("VirtualQuery failed!");
    }

    uint8_t* allocation_base = (uint8_t*)mem.AllocationBase;
    if (allocation_base == nullptr) {
        throw std::runtime_error("AllocationBase was NULL!");
    }

    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(allocation_base);
    auto pe = reinterpret_cast<IMAGE_NT_HEADERS*>(allocation_base + dos->e_lfanew);
    auto module_length = pe->OptionalHeader.SizeOfImage;

    LOGD << "[OHL] Sigscanning";

    funcs.malloc = sigscan<fmemory_malloc>(allocation_base, module_length, malloc_pattern);
    funcs.realloc = sigscan<fmemory_realloc>(allocation_base, module_length, realloc_pattern);
    funcs.free = sigscan<fmemory_free>(allocation_base, module_length, free_pattern);
    funcs.get_verification = sigscan<get_services_verification>(allocation_base, module_length,
                                                                get_verification_pattern);
    funcs.discovery =
        sigscan<discovery_from_json>(allocation_base, module_length, discovery_pattern);
    funcs.news = sigscan<news_from_json>(allocation_base, module_length, news_pattern);
    funcs.image_cache =
        sigscan<add_image_to_cache>(allocation_base, module_length, image_cache_pattern);

    LOGD << "[OHL] Injecting detours";

    auto ret = MH_Initialize();
    if (ret != MH_OK) {
        throw std::runtime_error("MH_Initialize failed " + std::to_string(ret));
    }

    ret = MH_CreateHook((LPVOID)funcs.get_verification, (LPVOID)&detour_get_verification,
                        reinterpret_cast<LPVOID*>(&original_get_verification));
    if (ret != MH_OK) {
        throw std::runtime_error("MH_CreateHook failed " + std::to_string(ret));
    }
    ret = MH_EnableHook((LPVOID)funcs.get_verification);
    if (ret != MH_OK) {
        throw std::runtime_error("MH_EnableHook failed " + std::to_string(ret));
    }

    ret = MH_CreateHook((LPVOID)funcs.discovery, (LPVOID)&detour_discovery_from_json,
                        reinterpret_cast<LPVOID*>(&original_discovery_from_json));
    if (ret != MH_OK) {
        throw std::runtime_error("MH_CreateHook failed " + std::to_string(ret));
    }
    ret = MH_EnableHook((LPVOID)funcs.discovery);
    if (ret != MH_OK) {
        throw std::runtime_error("MH_EnableHook failed " + std::to_string(ret));
    }

    ret = MH_CreateHook((LPVOID)funcs.news, (LPVOID)&detour_news_from_json,
                        reinterpret_cast<LPVOID*>(&original_news_from_json));
    if (ret != MH_OK) {
        throw std::runtime_error("MH_CreateHook failed " + std::to_string(ret));
    }
    ret = MH_EnableHook((LPVOID)funcs.news);
    if (ret != MH_OK) {
        throw std::runtime_error("MH_EnableHook failed " + std::to_string(ret));
    }

    ret = MH_CreateHook((LPVOID)funcs.image_cache, (LPVOID)&detour_add_image_to_cache,
                        reinterpret_cast<LPVOID*>(&original_add_image_to_cache));
    if (ret != MH_OK) {
        throw std::runtime_error("MH_CreateHook failed " + std::to_string(ret));
    }
    ret = MH_EnableHook((LPVOID)funcs.image_cache);
    if (ret != MH_OK) {
        throw std::runtime_error("MH_EnableHook failed " + std::to_string(ret));
    }

    LOGI << "[OHL] Hooks injected successfully";
}

}  // namespace ohl::hooks
