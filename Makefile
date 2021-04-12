NVCC=/usr/local/cuda-11.2/bin/nvcc
all: m3shell m3server memMapManager_test m3shell_memset.fatbin

m3shell_memset.fatbin:
	$(NVCC) -g -lcuda -lrt -fatbin -o m3shell_memset.fatbin m3shell_memset.cu

m3shell:
	$(NVCC) -g -lcuda -lrt -o m3shell memMapManager.cpp m3shell.cpp

m3server:
	$(NVCC) -g -lcuda -lrt -o m3server memMapManager.cpp m3server.cpp

memMapManager_test:
	$(NVCC) -g -lcuda -lrt -o memMapManager_test memMapManager.cpp memMapManager_test.cpp

clean:
	rm memMapManager_test
	rm m3shell
	rm m3server
	rm pid_*
	rm /dev/shm/sem.MemMapManager_Server_Barrier
	rm MemMapManager_Server_EndPoint
	rm m3shell_ipc 
	rm m3shell_memset.fatbin

