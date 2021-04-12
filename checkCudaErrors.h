#pragma once

#ifdef __DRIVER_TYPES_H__
#ifndef DEVICE_RESET
#define DEVICE_RESET cudaDeviceReset();
#endif
#else
#ifndef DEVICE_RESET
#define DEVICE_RESET 
#endif
#endif

template< typename T >
void check(T result, char const *const func, const char *const file, int const line)
{
    if (result)
    {
        fprintf(stderr, "CUDA error at %s:%d code=%d(%s) \"%s\" \n",
                file, line, static_cast<unsigned int>(result), _cudaGetErrorEnum(result), func);
		DEVICE_RESET
        // Make sure we call CUDA Device Reset before exiting
        exit(EXIT_FAILURE);
    }
}

#ifdef __DRIVER_TYPES_H__
// This will output the proper CUDA error strings in the event that a CUDA host call returns an error
#define checkCudaErrors(val)           check ( (val), #val, __FILE__, __LINE__ )