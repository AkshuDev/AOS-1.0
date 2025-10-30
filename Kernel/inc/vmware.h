#include <inttypes.h>
#include <inc/pcie.h>
#include <inc/gpu.h>

void vmware_init(struct gpu_device* gpu) __attribute__((used));
void vmware_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) __attribute__((used));