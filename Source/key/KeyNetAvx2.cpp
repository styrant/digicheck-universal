// KeyNet's convolution compiled with AVX2 + FMA (see KeyNetKernel.inc); only called when the CPU has both
#define KEYNET_KERNEL keynet_kernels::convEluAvx2
namespace keynet_kernels { void convEluAvx2 (const float*, const float*, int, int, int, const float*, int, int, float*, float*); }
#include "KeyNetKernel.inc"
