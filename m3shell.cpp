#include "MemMapManager.h"
#include "helper_cuda_drvapi.h"

CUmodule cuModule;
CUfunction _m3shell_memset_kernel;

#define FATBIN_FILE "m3shell_memset.fatbin"
static void memMapGetDeviceFunction(char **argv) {
  // first search for the module path before we load the results
  std::string module_path;
  std::ostringstream fatbin;

  if (!findFatbinPath(FATBIN_FILE, module_path, argv, fatbin)) {
    exit(EXIT_FAILURE);
  } else {
    printf("> initCUDA loading module: <%s>\n", module_path.c_str());
  }

  if (!fatbin.str().size()) {
    printf("fatbin file empty. exiting..\n");
    exit(EXIT_FAILURE);
  }

  // Create module from binary file (FATBIN)
  checkCudaErrors(cuModuleLoadData(&cuModule, fatbin.str().c_str()));

  // Get function handle from module
  checkCudaErrors(
      cuModuleGetFunction(&_m3shell_memset_kernel, cuModule, "m3shell_memset_kernel"));
}

int main(int argc, char **argv) {
    CUUTIL_ERRCHK(cuInit(0));
    CUcontext ctx;
    CUdevice device = 0;
    ProcessInfo pInfo;
    CUUTIL_ERRCHK(cuCtxCreate(&ctx, 0, device));
    pInfo.SetContext(ctx);

    memMapGetDeviceFunction(argv);


    struct sockaddr_un client_addr;
    client_addr.sun_family = AF_UNIX;
    strncpy(client_addr.sun_path, "m3shell_ipc", sizeof(client_addr.sun_path));

    std::vector<std::string> d_memId;
    std::vector<uintptr_t> d_ptr;
    std::vector<size_t> d_size;

    MemMapRequest req;
    MemMapResponse res;
    char cmd[128];
    while (true) {
        unlink("m3shell_ipc");
        printf("> ");
        scanf("%s", cmd);

        if(!strcmp(cmd, "q") || !strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
            printf("Bye\n");
            break;
        }

        if (!strcmp(cmd, "h") || !strcmp(cmd, "help")) {
            printf("Memory Map Manager testing shell\n");
            printf("PLEASE MAKE SURE THAT THE SERVER PROGRAM IS RUNNING.\n");
            printf("Usage:\n");
            printf("halt : Halts the M3 server\n");
            printf("echo <N> : Sends ECHO command and receives ACK for N times\n");
            printf("alloc <memory id> <factor> <g | m | k> : Allocates <factor> <g | m | k> bytes of GPU memory with ID <memory id>\n");
            printf("exit: exits the shell\n");
            printf("help: prints out this help message\n");
        }

        if(!strcmp(cmd, "halt")) {
            int sock_fd = ipcOpenAndBindSocket(&client_addr);
            ipcHaltM3Server(sock_fd, pInfo);
            close(sock_fd);
        }

        if(!strcmp(cmd, "echo")) {
            int rep;
            scanf("%d", &rep);
            
            int sock_fd = ipcOpenAndBindSocket(&client_addr);
            req.src = pInfo;
            req.cmd = CMD_ECHO;
            for(int i = 0; i < rep; ++i) {
                res = MemMapManager::Request(sock_fd, req, &server_addr);
                if(res.status != STATUSCODE_ACK) {
                    printf("Failed to Echo\n");
                    continue;
                } else {
                    printf("Echo back # %d\n", i);
                }
            }
            close(sock_fd);
            continue;
        }

        if(!strcmp(cmd, "alloc")) {
            char memId[MAX_MEMID_LEN];
            scanf("%s", memId);
            uint32_t factor;
            scanf("%u", &factor);
            char unit[8];
            scanf("%s", unit);

            size_t num_bytes = factor;
            switch(unit[0]) {
                case 'g':
                    num_bytes <<= 10;
                case 'm':
                    num_bytes <<= 10;
                case 'k':
                    num_bytes <<= 10;
                    break;
                default:
                    printf("Invalid unit %s!\n", unit);
                    continue;
            }
            struct sockaddr_un client_addr;
            client_addr.sun_family = AF_UNIX;
            strncpy(client_addr.sun_path, "m3shell_ipc", 108);
            int sock_fd = ipcOpenAndBindSocket(&client_addr);
            res = MemMapManager::RequestRoundedAllocationSize(pInfo, sock_fd, num_bytes);
            if(res.status != STATUSCODE_ACK) {
                printf("Failed to round %lu bytes.\n", num_bytes);
                continue;
            }
            num_bytes = res.roundedSize;
            res = MemMapManager::RequestAllocate(pInfo, sock_fd, memId, 1024, num_bytes);
            if(res.status != STATUSCODE_ACK) {
                printf("Failed to allocate %lu bytes.\n", num_bytes);
                continue;
            }
            printf("Allocated %lu bytes @ [%p : %p].\n", num_bytes, (char *)res.d_ptr, (char *)res.d_ptr + num_bytes -1);
            close(sock_fd);

            std::string memIdStr = std::string(memId);
            d_memId.push_back(memIdStr);
            d_ptr.push_back(res.d_ptr);
            d_size.push_back(num_bytes);
        }

        if(!strcmp(cmd, "lsmem")) {
            printf("List of allocated memory regions\n");
            for(int i = 0; i < d_ptr.size(); ++i) {
                printf("[%p : %p]\t%s\n", (char *)d_ptr[i], (char *)d_ptr[i] + d_size[i] - 1, d_memId[i].c_str());
            }
        }

        if(!strcmp(cmd, "read")) {
            CUdeviceptr ptrToRead;
            scanf("%llx", &ptrToRead);
            size_t sizeToRead;
            scanf("%lu", &sizeToRead);
            char *  buf = new char[sizeToRead];
            CUUTIL_ERRCHK(cuMemcpyDtoH(buf, ptrToRead, sizeToRead));
            printf("%.*s", (int)sizeToRead, buf);
            printf("\n");
            delete buf;
        }

        #define MAX_WRITE_LEN 1024*16
        if(!strcmp(cmd, "write")) {
            CUdeviceptr ptrToWrite;
            scanf("%llx", &ptrToWrite);
            char inputBuf[MAX_WRITE_LEN];
            scanf("%s", inputBuf);
            CUUTIL_ERRCHK(cuMemcpyHtoD(ptrToWrite, inputBuf, strlen(inputBuf)));
            printf("Wrote %lu bytes to %p\n", strlen(inputBuf), (void *)ptrToWrite);
        }

        if(!strcmp(cmd, "memset")) {
            CUdeviceptr ptrToWrite;
            scanf("%llx", &ptrToWrite);
            size_t sizeToWrite;
            scanf("%lu", &sizeToWrite);
            char buf[8];
            scanf("%s", buf);
            char val = buf[0];
            
            CUstream stream;
            checkCudaErrors(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));

            void *args[] = {&ptrToWrite, &sizeToWrite, &val};

            // Push a simple kernel on th buffer.
            checkCudaErrors(cuLaunchKernel(_m3shell_memset_kernel, 1, 1, 1, 1, 1, 1, 0, stream, args, 0));
            checkCudaErrors(cuStreamSynchronize(stream));
            
            printf("Set [%p : %p] to %c\n", (char *)ptrToWrite, (char *)ptrToWrite + sizeToWrite - 1, val);
        }

    }

    return 0;
}