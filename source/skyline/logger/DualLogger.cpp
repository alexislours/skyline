#include "skyline/logger/DualLogger.hpp"
#include "nn/nifm.h"

#include <atomic>

#include "skyline/utils/cpputils.hpp"

#define PORT 6969
#define POLLIN 0x01

extern "C" void skyline_tcp_send_raw(char* data, size_t size) __attribute__((visibility("default")));

void skyline_tcp_send_raw(char* data, u64 size) { skyline::logger::s_Instance->Log(data, size); }

namespace skyline::logger {
int g_tcpSocket = -1;
static std::atomic<bool> g_loggerInit{false};

Result stub() { return 0; };

void init_socket_thing(void*) {
    struct sockaddr_in serverAddr;
    s32 listenSocket = nn::socket::Socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0) return;
    
    int flags = 1;
    nn::socket::SetSockOpt(listenSocket, SOL_SOCKET, SO_KEEPALIVE, &flags, sizeof(flags));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = nn::socket::InetHtons(PORT);

    int rval = nn::socket::Bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (rval < 0) {
        nn::socket::Close(listenSocket);
        return;
    }

    rval = nn::socket::Listen(listenSocket, 1);
    if (rval < 0) {
        nn::socket::Close(listenSocket);
        return;
    }

    // Poll with a 1-second timeout to let the thread exit cleanly on emulator
    s32 clientSocket = -1;
    while (true) {
        nn::socket::PollFd pfd;
        pfd.fd = listenSocket;
        pfd.events = POLLIN;
        pfd.revents = 0;

        s32 pollResult = nn::socket::Poll(&pfd, 1, 1000);
        if (pollResult > 0 && (pfd.revents & POLLIN)) {
            u32 addrLen = sizeof(serverAddr);
            clientSocket = nn::socket::Accept(listenSocket, (struct sockaddr*)&serverAddr, &addrLen);
            break;
        }
    }

    nn::socket::Close(listenSocket);

    if (clientSocket < 0) return;
    g_tcpSocket = clientSocket;

    char* message = "TCP Socket Connected.\n";
    nn::socket::Send(g_tcpSocket, (void*)message, strlen(message), 0);
}

void skyline_socket_init() {    
    nn::nifm::Initialize();
    nn::nifm::SubmitNetworkRequest();

    while (nn::nifm::IsNetworkRequestOnHold()) { }

    if (!nn::nifm::IsNetworkAvailable()) {
        svcOutputDebugString("[Skyline] No network is available\n", 34);
        return;
    }

    nn::socket::Config config = {};

    const size_t poolSize = 0x600000;
    void* socketPool = memalign(0x4000, poolSize);

    config.pool = socketPool;
    config.allocPoolSize = 0x20000;
    config.poolSize = poolSize;
    config.concurLimit = 14;

    nn::socket::Initialize(config);
    g_loggerInit.store(true, std::memory_order_release);
}

void start_listen_thread() {
    const size_t stackSize = 0x4000;
    void* threadStack = memalign(0x1000, stackSize);

    skyline::logger::s_Instance->Log("[skyline_thread] Preparing to create thread");
    nn::os::ThreadType* thread = new nn::os::ThreadType;
    nn::os::CreateThread(thread, init_socket_thing, nullptr, threadStack, stackSize, 16, 0);
    nn::os::StartThread(thread);
}

Result init_normal(void* arg1, ulong arg2, ulong arg3, int arg4) {
    return 0;
}

Result init_config(nn::socket::Config const& config) {
    return 0;
}

void setup_socket_hooks() {
    Result (*socketInitWithConfig)(nn::socket::Config const&) = nn::socket::Initialize;
    A64HookFunction(reinterpret_cast<void*>(socketInitWithConfig), reinterpret_cast<void*>(init_config),
                    NULL);

    A64HookFunction(reinterpret_cast<void*>(nn::socket::Finalize), reinterpret_cast<void*>(stub),
                    NULL);
}

void DualLogger::Initialize() {}

bool DualLogger::ShouldFlush() {
    return true;
}

void DualLogger::SendRaw(void* data, size_t size) {
    svcOutputDebugString((const char*)data, size);

    // Also send to TCP if a client is connected
    if (g_loggerInit.load(std::memory_order_acquire) && g_tcpSocket != -1) {
        nn::socket::Send(g_tcpSocket, data, size, 0);
    }
}
};  // namespace skyline::logger
