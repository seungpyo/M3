# M3: Memory Map Manager for NVIDIA GPUs

## Overview
M3 allows multiple processes to share a memory region in NVIDIA GPU.
Any POSIX process can use M3 API which requests server to allocate or share a memory region.
UNIX domain sockets are used to implement IPC between M3 server and client processes.
To ensure the consistency of IPC, a semaphore file is used to "lock" the IPC socket.

## M3 Server
A single line of code is enough to boot M3 server;

`auto m3Srv = MemMapManger::Instance();` 

The server class is implemented in singleton pattern, thus every call of `Instance()` will return the identical instance.

## M3 APIs
Currently, M3 supports APIs below:
### RequestRegister
`MemMapManager::RequestRegister(ProcessInfo &pInfo, int sock_fd);`

Registers client process info to M3 server. The process info is passed by argument `pInfo`.

This API is left for future use. The list of subscribers can be used for access control, request validation, etc.

### RequestRoundedAllocationSize
`MemMapManager::RequestRoundedAllocationSize(ProcessInfo &pInfo, int sock_fd, size_t num_bytes);`

Get allocation size, rounded up by minimum memory granularity.

CUDA environment has a minimum granularity size whicih is determined by device. If we try to allocate a memory region whose size is not a multiple of the granularity, CUDA APIs may throw an error.

It is the M3 server that determines which GPU device to allocate memory, minimum granularity should be obtained from server.

User **MUST** call this API before calling `MemMapManager::RequestAllocate()`.

### RequestAllocate
`MemMapManager::RequestAllocate(ProcessInfo &pInfo, int sock_fd, char * memId, size_t alignment, size_t num_bytes);`

Allocate a memory region in GPU device.

`memId` works as a hint for memory reuse. If M3 server finds a memory region which is tagged with the same `memId`, the region is not allocated redundantly. Instead, a handler to the region is passed to the client. The client uses the handler to map the region into its own virtual address space.

## To Do

* Support multiple GPU - This feature requires P2P communication between GPUs using NVLINK, which my PC doesn't support yet.
* Refactor `ProcessInfo` class

## Notes

* This project is done in [Big Data Systems Lab, Hanyang University](bdsl.hanyang.ac.kr).
* Previous commits are in [my cuda_study repo](https://github.com/seungpyo/cuda_study/tree/master/ipc)
