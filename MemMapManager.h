#pragma once

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <atomic>
#include <semaphore.h>
#include "cuda.h"
#include "cuutils.h"

typedef uintptr_t shareable_handle_t;

typedef struct MemoryRegionSt {
    shareable_handle_t shareableHandle;
    uintptr_t base;
    size_t size;
} MemoryRegion;


enum M3InternalErrorType {
    M3INTERNAL_INVALIDCODE,
    M3INTERNAL_NYI,
    M3INTERNAL_OK,
    M3INTERNAL_DUPLICATE_REGISTER,
    M3INTERNAL_ENTRY_NOT_FOUND,
};

class ProcessInfo {
    public:
        pid_t pid;
        int device_ordinal;
        CUdevice device;
        CUcontext ctx;

        ProcessInfo(void) : ProcessInfo(0) {}

        ProcessInfo(int _device_ordinal) {
            pid = getpid();
        }

        ProcessInfo(const ProcessInfo& pInfo) {
            if (this != &pInfo)
            {
                pid = pInfo.pid;
                device_ordinal = pInfo.device_ordinal;
                device = pInfo.device;
                ctx = pInfo.ctx;
            }
        }

        void SetContext(CUcontext &_ctx) {
            ctx = _ctx;
            CUUTIL_ERRCHK( cuCtxGetDevice(&device) );
            // For now, we just assume that device ordinal is same as device id.
            device_ordinal = device;
        }

        bool operator ==(const ProcessInfo &other) const {
            bool ret = true;
            ret = ret && (pid == other.pid);
            return ret;
        }

        std::string AddressString() {
            char buf[1024];
            sprintf(buf, "pid_%d", pid);
            std::string s(buf);
            return s;
        }

        std::string DebugString() {
            char buf[1024];
            sprintf(buf+strlen(buf), "ProcessInfo:\n");
            sprintf(buf+strlen(buf), "* pid = %d\n", pid);
            sprintf(buf+strlen(buf), "* device = %d\n", device);
            sprintf(buf+strlen(buf), "* device_ordinal = %d\n", device_ordinal);
            std::string s(buf);
            return s;
        }

};

enum MemMapCmd {
    CMD_INVALID,
    CMD_ECHO,
    CMD_HALT,
    CMD_REGISTER,
    CMD_DEREGISTER,
    CMD_ALLOCATE,
    CMD_DEALLOCATE,
    CMD_IMPORT,
    CMD_GETROUNDEDALLOCATIONSIZE
};

enum MemMapStatusCode {
    STATUSCODE_INVALID,
    STATUSCODE_ACK,
    STATUSCODE_NYI,
    STATUSCODE_SOCKERR,
    STATUSCODE_DUPLICATE_REGISTER,
    STATUSCODE_UNKNOWN_ERR
};

#define MAX_MEMID_LEN 256

class MemMapRequest {
    public:
        MemMapRequest() : MemMapRequest(CMD_INVALID) {}
        MemMapRequest(MemMapCmd _cmd) {
            cmd = _cmd;
            size = 0;
            alignment = 0;
        }

        MemMapCmd cmd;
        ProcessInfo src;
        shareable_handle_t shareableHandle;
        char memId[MAX_MEMID_LEN];
        size_t size, alignment;
        ProcessInfo importSrc;
};

class MemMapResponse {
    public:
        MemMapResponse(void) : MemMapResponse(STATUSCODE_INVALID) {}
        MemMapResponse(MemMapStatusCode _status) {
            status = _status;
            shareableHandle = (shareable_handle_t)nullptr;
            roundedSize = 0;
            d_ptr = (CUdeviceptr)nullptr;
        }

        MemMapStatusCode status;
        ProcessInfo dst;
        shareable_handle_t shareableHandle;
        char memId[MAX_MEMID_LEN];
        size_t roundedSize;
        CUdeviceptr d_ptr;
        uint32_t numShareableHandles;

        std::string DebugString() {
            char buf[1024];
            sprintf(buf+strlen(buf), "* status code = %d\n", status);
            sprintf(buf+strlen(buf), "* roundedSize = %lx\n", roundedSize);
            sprintf(buf+strlen(buf), "* d_ptr = %p\n", (void *)d_ptr);
            sprintf(buf+strlen(buf), "* numShareableHandles = %u\n", numShareableHandles);
            sprintf(buf+strlen(buf), "* shareableHandle = %lx\n", shareableHandle);
            sprintf(buf+strlen(buf), "* memId = %s\n", memId);
            sprintf(buf+strlen(buf), "* Destination process info\n");
            sprintf(buf+strlen(buf), "* %s", dst.DebugString().c_str());
            std::string s(buf);
            return s;
        }
};

class MemMapManager {
    public:
        ~MemMapManager();
        static MemMapManager* Instance() {
            std::call_once(singletonFlag_, [](){
                instance_ = new MemMapManager();
            });
            return instance_;
        }

        // Request() is a generic Request method provided to client.
        // All requests except RequestAllocate uses Request() internally.
        // Request() uses sendto() / recvfrom() system call to send and receive messages.
        static MemMapResponse Request(int sock_fd, MemMapRequest req, struct sockaddr_un * remote_addr);
        static MemMapResponse RequestRegister(ProcessInfo &pInfo, int sock_fd);
        static MemMapResponse RequestDeAllocate(ProcessInfo &pInfo, int sock_fd, shareable_handle_t shHandle);
        static MemMapResponse RequestRoundedAllocationSize(ProcessInfo &pInfo, int sock_fd, size_t num_bytes);

        // RequestAllocate() is a dedicate method to request Allocate() function.
        // Since shareable handles are UNIX file descriptors of separate process,
        // we must receive ancillary messages using sendmsg() and recvmsg().
        // To allocate anonymous memory region (without memId), pass nullptr to memId.
        static MemMapResponse RequestAllocate(ProcessInfo &pInfo, int sock_fd, char * memId, size_t alignment, size_t num_bytes);

        // Trivial Getter / Setters.
        std::string DebugString() const;
        std::string Name() { return name; }
        static std::string EndPoint() { return endpointName; }
        CUcontext ctx(void) { return ctx_; }

        // Name of this MemMapManager instance.
        static const char name[128];
        // Name of the endpoint file.
        static const char endpointName[128];
        // Name of the semaphore file.
        static const char barrierName[128];

    private:
        // Keep constructor private in order to implement singleton pattern.
        MemMapManager();

        // Sever loop.
        void Server();

        // Register() registers ProcessInfo of new client process in M3 server.
        // If duplicate subscription is detected, Register() does nothing but returns STATUSCODE_DUPLICATE_REGISTER.
        M3InternalErrorType Register(ProcessInfo &pInfo);

        // Allocate() allocates physical memory in GPU using cuMemCreate(), and export shareable handlers.
        // Allocate() will NOT be called if memory ID is already registered in M3 server.
        // We assume that client request CMD_GETROUNDEDALLOCATIONSIZE before CMD_ALLOCATE.
        // Thus, no size rounding is provided in Allocate().
        M3InternalErrorType Allocate(ProcessInfo &pInfo, size_t alignment, size_t num_bytes, std::vector<shareable_handle_t> &shHandle, std::vector<CUmemGenericAllocationHandle> &allocHandle);

        // DeAllocate(): To Be Implemented.
        M3InternalErrorType DeAllocate(ProcessInfo &pInfo, shareable_handle_t shHandle);

        // GetRoundedAllocationSize() rounds num_bytes to the minimum granularity of the GPU device.
        // User MUST get rounded size using this method.
        // Otherwise, the behavior of Allocate() is undefined.
        size_t GetRoundedAllocationSize(size_t num_bytes);

        // Singleton members
        static MemMapManager * instance_;
        static std::once_flag singletonFlag_;

        // Cuda settings
        CUcontext ctx_;
        std::vector<CUdevice> devices_;
        int device_count_;

        // IPC settings
        int ipc_sock_fd_;
        std::vector<ProcessInfo> subscribers_;

        // Find shareable handle using memory ID, and vice versa.
        std::unordered_map<std::string, MemoryRegion> memIdToMemoryRegion_;
        std::unordered_map<shareable_handle_t, std::string> shHandletoMemId_;        


};

// sockaddr_un for server
static struct sockaddr_un server_addr = { 
    AF_UNIX,
    "MemMapManager_Server_EndPoint"
};

// panic() ; You know what it does.
void panic(const char * msg);

// Helper functions for IPC features.

// Semaphore locking / unlocking helper functions.

int ipcLockGeneric(int initialValue);
int ipcLock(void);
int ipcUnlock(void);

// ipcLockPrivileged() is used only by server, 
// just to protect server initialization process done in MemMapManager::MemMapManager().
int ipcLockPrivileged(void);

int ipcOpenAndBindSocket(struct sockaddr_un * local_addr);

// ipcSendShareableHandle() sends multiple shareable handles (UNIX file descriptors) using sendmsg().
// this function is used by RequestAllocate().
int ipcSendShareableHandle(int sock_fd, struct sockaddr_un * client_addr, shareable_handle_t shHandle);

// ipcRecvShareableHandle() receives multiple shareable handles (UNIX file descriptors) using recvmsg()
// this function is used by RequestAllocate().
int ipcRecvShareableHandle(int sock_fd, shareable_handle_t *shHandle);

// ipcHaltM3Server() halts M3 server.
// This function is used only for debug purposes, to stop infinite loop.
void ipcHaltM3Server(int sock_fd, ProcessInfo pInfo);