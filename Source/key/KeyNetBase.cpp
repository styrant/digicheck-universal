// KeyNet's convolution for any x86-64 CPU (see KeyNetKernel.inc)
#define KEYNET_KERNEL keynet_kernels::convEluBase
namespace keynet_kernels { void convEluBase (const float*, const float*, int, int, int, const float*, int, int, float*, float*); }
#include "KeyNetKernel.inc"
