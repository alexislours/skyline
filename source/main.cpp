#include "main.hpp"

#include "skyline/logger/DualLogger.hpp"
#include "skyline/utils/ipc.hpp"
#include "skyline/utils/cpputils.hpp"
#include "skyline/utils/utils.h"
#include "skyline/utils/call_once.hpp"
#include "skyline/utils/cur_proc_handle.hpp"
#include <nn/vi.h>

// For handling exceptions
char ALIGNA(0x1000) exception_handler_stack[0x4000];
nn::os::UserExceptionInfo exception_info;

void exception_handler(nn::os::UserExceptionInfo* info) {
    skyline::logger::s_Instance->SendRawFormat("Exception occurred!\n");

    skyline::logger::s_Instance->SendRawFormat("Error description: %x\n", info->ErrorDescription);
    for (int i = 0; i < 29; i++)
        skyline::logger::s_Instance->SendRawFormat("X[%02i]: %" PRIx64 "\n", i, info->CpuRegisters[i].x);
    skyline::logger::s_Instance->SendRawFormat("FP: %" PRIx64 "\n", info->FP.x);
    skyline::logger::s_Instance->SendRawFormat("LR: %" PRIx64 "\n", info->LR.x);
    skyline::logger::s_Instance->SendRawFormat("SP: %" PRIx64 "\n", info->SP.x);
    skyline::logger::s_Instance->SendRawFormat("PC: %" PRIx64 "\n", info->PC.x);
}

void stub() {}

static skyline::utils::Once g_MountRomInit;
Result (*nnFsMountRomImpl)(char const*, void*, unsigned long);

Result handleNnFsMountRom(char const* path, void* buffer, unsigned long size) {
    Result rc = nnFsMountRomImpl(path, buffer, size);

    skyline::utils::g_RomMountStr = std::string(path) + ":/";

    return rc;
}

static skyline::utils::Once g_RoInit;
Result (*nnRoInitializeImpl)();

Result nn_ro_init() {
    Result ret = 0;

    g_RoInit.call_once([&ret]() {
        ret = nnRoInitializeImpl();
        skyline::logger::s_Instance->LogFormat("[skyline_main] Ran hooked nn::ro::initialize (0x%x)", ret);
    });

    return ret;
}

Result (*orig_CreateLayer)(nn::vi::Layer**, nn::vi::Display*);
static skyline::utils::Once g_CreateLayer;

Result hooked_CreateLayer(nn::vi::Layer** out, nn::vi::Display* disp) {
    Result res = orig_CreateLayer(out, disp);

    g_CreateLayer.call_once([]() {
        skyline::logger::skyline_socket_init();
        skyline::logger::setup_socket_hooks();
        skyline::logger::start_listen_thread();

        if (!skyline::utils::g_RomMountStr.empty()) {
            auto manager = new skyline::plugin::Manager();
            manager->LoadPluginsImpl(); 
        } else {
            skyline::logger::s_Instance->Log("[skyline_main] ERROR: RomFS path is empty");
        }
    });

    return res;
}

void skyline_main() {
    // populate our own process handle
    envSetOwnProcessHandle(skyline::proc_handle::Get());

    // init hooking setup
    A64HookInit();

    // initialize logger
    nn::fs::MountSdCardForDebug("sd");
    skyline::logger::s_Instance = new skyline::logger::DualLogger();
    skyline::logger::s_Instance->StartThread();
    skyline::logger::s_Instance->Log("[skyline_main] Beginning initialization.\n");

    // override exception handler to dump info
    nn::os::SetUserExceptionHandler(exception_handler, exception_handler_stack, sizeof(exception_handler_stack),
                                     &exception_info);

    // hook to prevent the game from double mounting romfs
    A64HookFunction(reinterpret_cast<void*>(nn::fs::MountRom), reinterpret_cast<void*>(handleNnFsMountRom),
                    (void**)&nnFsMountRomImpl);

    A64HookFunction(reinterpret_cast<void*>(nn::ro::Initialize), reinterpret_cast<void*>(nn_ro_init), (void**)&nnRoInitializeImpl);

    A64HookFunction(
        reinterpret_cast<void*>(nn::vi::CreateLayer),
        reinterpret_cast<void*>(hooked_CreateLayer),
        (void**)&orig_CreateLayer
    );

    skyline::logger::s_Instance->LogFormat("[skyline_main] text: 0x%" PRIx64 " | rodata: 0x%" PRIx64
                                           " | data: 0x%" PRIx64 " | bss: 0x%" PRIx64 " | heap: 0x%" PRIx64,
                                           skyline::utils::g_MainTextAddr, skyline::utils::g_MainRodataAddr,
                                           skyline::utils::g_MainDataAddr, skyline::utils::g_MainBssAddr,
                                           skyline::utils::g_MainHeapAddr);
}

extern "C" void skyline_init() {
    skyline::utils::init();
    virtmemSetup();  // needed for libnx JIT

    skyline_main();
}
