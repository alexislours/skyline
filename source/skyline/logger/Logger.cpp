#include "skyline/logger/Logger.hpp"

#include <atomic>
#include <cstdarg>

#include "alloc.h"
#include "mem.h"
#include "nn/os.hpp"
#include "operator.h"

namespace skyline::logger {

Logger* s_Instance;

#ifndef NOLOG

std::queue<char*>* g_msgQueue = nullptr;
static nn::os::MutexType g_msgQueueMutex;
static std::atomic<bool> g_msgQueueReady{false};

void ThreadMain(void* arg) {
    Logger* t = (Logger*)arg;

    t->Initialize();

    t->LogFormat("[%s] Logger initialized.", t->FriendlyName().c_str());

    while (true) {
        t->Flush();
        nn::os::YieldThread();  // let other parts of OS do their thing
        nn::os::SleepThread(nn::TimeSpan::FromNanoSeconds(100000000));
    }
}

void Logger::StartThread() {
    const size_t stackSize = 0x3000;
    void* threadStack = memalign(0x1000, stackSize);

    if (!g_msgQueue) g_msgQueue = new std::queue<char*>();
    nn::os::InitializeMutex(&g_msgQueueMutex, false, 0);
    g_msgQueueReady.store(true, std::memory_order_release);

    nn::os::ThreadType* thread = new nn::os::ThreadType;
    nn::os::CreateThread(thread, ThreadMain, this, threadStack, stackSize, 16, 0);
    nn::os::StartThread(thread);
}

void Logger::SendRaw(const char* data) { SendRaw((void*)data, strlen(data)); }

void Logger::SendRawFormat(const char* format, ...) {
    va_list args;
    char buff[0x1000] = {0};
    va_start(args, format);

    int len = vsnprintf(buff, sizeof(buff), format, args);

    SendRaw(buff, len);

    va_end(args);
}

void AddToQueue(char* data) {
    if (!g_msgQueueReady.load(std::memory_order_acquire)) {
        if (!g_msgQueue) g_msgQueue = new std::queue<char*>();
        g_msgQueue->push(data);
        return;
    }

    nn::os::LockMutex(&g_msgQueueMutex);
    g_msgQueue->push(data);
    nn::os::UnlockMutex(&g_msgQueueMutex);
}

bool Logger::ShouldFlush() {
    return true;
}

void Logger::Flush() {
    if (!this->ShouldFlush()) return;
    if (!g_msgQueueReady.load(std::memory_order_acquire) || !g_msgQueue) return;

    std::queue<char*> pending;
    nn::os::LockMutex(&g_msgQueueMutex);
    std::swap(pending, *g_msgQueue);
    nn::os::UnlockMutex(&g_msgQueueMutex);

    while (!pending.empty()) {
        auto data = pending.front();

        SendRaw(data, strlen(data));
        delete[] data;
        pending.pop();
    }
}

void Logger::Log(const char* data, size_t size) {
    if (size == UINT32_MAX) size = strlen(data);

    char* ptr = new char[size + 2];
    memset(ptr, 0, size + 2);
    memcpy(ptr, data, size);
    // ptr[size] = '\n';

    AddToQueue(ptr);
    return;
}

void Logger::Log(std::string str) { Log(str.data(), str.size()); }

void Logger::LogFormat(const char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t len = vsnprintf(NULL, 0, format, args);
    char* ptr = new char[len + 2];
    memset(ptr, 0, len + 2);
    vsnprintf(ptr, len + 1, format, args);
    ptr[len] = '\n';

    AddToQueue(ptr);
    va_end(args);

    return;
}

#endif  // NOLOG

};  // namespace skyline::logger
