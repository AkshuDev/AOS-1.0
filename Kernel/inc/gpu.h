#include <inttypes.h>
#include <asm.h>
#include <inc/pcie.h>

#define Bochs_VENDORID 0x1234
#define VMware_VENDORID 0x15AD
#define Intel_VENDORID 0x8086
#define Nvidia_VENDORID 0x10DE
#define AMD_VENDORID 0x1002
#define VirtIo_VENDORID 0x1AF4

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

typedef struct gpu_device {
    const char* name;

    pcie_device_t* pcie_device;
    PCIe_FB* framebuffer;

    // Function pointers
    void (*init)(struct gpu_device* gpu);
    void (*set_mode)(struct gpu_device* gpu, uint32_t width, uint32_t height, uint32_t bpp);
    void (*swap_buffers)(struct gpu_device* gpu);
} gpu_device_t;

struct virtio_gpu_resp_display {
    uint32_t enabled;
    uint32_t x; // top-left corner
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

struct virtio_gpu_ctrl_hdr {
    uint32_t type; // command type, e.g., GET_DISPLAY_INFO
    uint32_t flags; // 0 for host-to-device
    uint64_t fence_id; // optional
    uint32_t context_id; // usually 0
    uint32_t padding;
};

struct virtq_desc {
    uint64_t addr; // guest physical address
    uint32_t len; // length of the buffer
    uint16_t flags; // VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE
    uint16_t next; // index of next descriptor if F_NEXT
};

//struct virtqueue {
 //   struct list_head list;
 //   void (*callback)(struct virtqueue *vq);
 //   const char *name;
 //   struct virtio_device *vdev;
 //   unsigned int index;
 //   unsigned int num_free;
 //   unsigned int num_max;
 //   bool reset;
 //   void *priv;
//};

uint64_t gpu_get_framebuffer_and_info(PCIe_FB* fb, pcie_device_t* dev, gpu_device_t* gpu) __attribute__((used));
