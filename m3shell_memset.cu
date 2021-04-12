// Device code
extern "C" __global__ void m3shell_memset_kernel(char *ptr, int sz, char val)
{
    // Dummy kernel
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    for (; idx < sz; idx += (gridDim.x * blockDim.x)) {
        ptr[idx] = val;
    }
}
