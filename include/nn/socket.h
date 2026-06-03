/**
 * @file socket.h
 * @brief Functions for opening sockets for wireless communication.
 */

#pragma once

#include <sys/socket.h>

#include "types.h"

namespace nn {
namespace socket {
    struct InAddr {
        u32 addr;
    };

class Config;
    Result Initialize(void* pool, ulong poolSize, ulong allocPoolSize, int concurLimit);
    Result Initialize(Config const& config);
    Result Finalize();
    s32 SetSockOpt(s32 socket, s32 socketLevel, s32 option, void const*, u32 len);
    u64 Send(s32 socket, void const* buffer, u64 bufferLength, s32 flags);
    s32 Socket(s32 domain, s32 type, s32 proto);
    u16 InetHtons(u16);
    u32 InetAton(const char* str, InAddr*);
    u32 Connect(s32 socket, const sockaddr* addr, u32 addrLen);
    u32 Bind(s32 socket, const sockaddr* addr, u32 addrLen);
    u32 Listen(s32 socket, s32 backlog);
    u32 Accept(s32 socket, sockaddr* addrOut, u32* addrLenOut);
    s32 Close(s32 socket);
    s32 Shutdown(s32 socket, s32 how);

    struct BsdBufferConfig {
        ulong tcp_tx_buf_size    = 0x8000;
        ulong tcp_rx_buf_size    = 0x10000;
        ulong tcp_tx_buf_max_size = 0x30000;
        ulong tcp_rx_buf_max_size = 0x30000;
        ulong udp_tx_buf_size    = 0x2400;
        ulong udp_rx_buf_size    = 0xA500;
        int   sb_efficiency      = 4;
    };

    struct Config {
        int version = 2;                // 0x0 (2 for most games, 8 for SV)
        bool unkBool1 = false;          // 0x4
        bool isUseBsdS = false;         // 0x5
        void* pool;           // 0x8
        ulong poolSize;             // 0x10
        ulong allocPoolSize;        // 0x18
        BsdBufferConfig bufferConfig;  // 0x20-0x50
        int concurLimit = 14;           // 0x54
        int padding;
    };

    static_assert(sizeof(Config) == 0x60, "Config Size");

    struct PollFd {
        s32 fd;
        s16 events;
        s16 revents;
    };

    s32 Poll(PollFd* fds, u64 nfds, s32 timeout);
};  // namespace socket
};  // namespace nn