#include <inttypes.h>
#include <asm.h>
#include <system.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/gpu/virtio.h>
#include <inc/drivers/io/io.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/core/pcie.h>
#include <inc/core/smp.h>
#include <inc/core/kfuncs.h>

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define MAX_CMD_RESP_BUFS 16

static uint8_t vq_buf[0x1000] __attribute__((aligned(4096)));
static struct virtqueue virtq;

static struct virtio_gpu_ctrl_hdr* cmd_buf[MAX_CMD_RESP_BUFS];
static struct virtio_gpu_resp_display_info* resp_buf[MAX_CMD_RESP_BUFS];
static uint64_t cmd_buf_phys[MAX_CMD_RESP_BUFS];
static uint64_t resp_buf_phys[MAX_CMD_RESP_BUFS];
static uint64_t main_buf_slot = 0;
static uint64_t worker_buf_slot = 0;

static spinlock_t virtq_lock = 0;

static uintptr_t notify_base;
static uint32_t notify_multiplier;

static uint32_t gpu_cmd_core = 0xFFFF;

static volatile struct virtio_common_cfg* common_cfg;

static void mmio_write64(uint64_t addr, uint64_t val) {
    *(volatile uint64_t*)addr = val;
}
static uint64_t mmio_read64(uint64_t addr) {
    return *(volatile uint64_t*)addr;
}
static void mmio_write32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t*)addr = val;
}
static uint32_t mmio_read32(uint64_t addr) {
    return *(volatile uint32_t*)addr;
}
static void mmio_write16(uint64_t addr, uint16_t val) {
    *(volatile uint16_t*)addr = val;
}
static uint16_t mmio_read16(uint64_t addr) {
    return *(volatile uint16_t*)addr;
}
static void mmio_write8(uint64_t addr, uint8_t val) {
    *(volatile uint8_t*)addr = val;
}
static uint8_t mmio_read8(uint64_t addr) {
    return *(volatile uint8_t*)addr;
}

static void virtio_sync_poll(void) {
    while (virtq.used->idx != virtq.avail->idx) {
        __asm__ volatile("pause");
    }

    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    uint16_t head = virtq.free_head;
    uint16_t next = (head + 1) % virtq.queue_size;
    virtq.free_head = (next + 1) % virtq.queue_size;
    spin_unlock_irqrestore(&virtq_lock, flags);
    
    worker_buf_slot++;
    serial_print("[VIRTIO] Submitted Sync\n");

    smp_yield();
}

static void virtio_submit_async(void* cmd, uint64_t cmd_phys, size_t cmd_size, void* resp, uint64_t resp_phys, size_t resp_size) {
    serial_printf("[VIRTIO] Submitting Sync [CMD: %lx, CMD PHYS: %lx, CMD SIZE: %lx]\n", (uint64_t)cmd, cmd_phys, cmd_size);
    uint16_t head = virtq.free_head;
    uint16_t next = (head + 1) % virtq.queue_size;

    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    // Descriptor 1: The Command (Read-only)
    serial_print("[VIRTIO] Setting Descriptor 1\n");
    virtq.desc[head].addr = (uintptr_t)cmd_phys;
    virtq.desc[head].len = cmd_size;
    virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    virtq.desc[head].next = next;

    // Descriptor 2: The Response (Write-only for GPU)
    serial_print("[VIRTIO] Setting Descriptor 2\n");
    virtq.desc[next].addr = (uintptr_t)resp_phys;
    virtq.desc[next].len = resp_size;
    virtq.desc[next].flags = VIRTQ_DESC_F_WRITE;
    virtq.desc[next].next = 0;

    virtq.avail->ring[virtq.avail->idx % virtq.queue_size] = head;
    __asm__ volatile("sfence" ::: "memory"); // Ensure GPU sees RAM update
    virtq.avail->idx++;
    spin_unlock_irqrestore(&virtq_lock, flags);

    // Notify Doorbell
    serial_print("[VIRTIO] Notifying GPU\n");
    uintptr_t db = notify_base + (common_cfg->queue_notify_off * notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    serial_print("[VIRTIO] Polling on a new thread\n");
    enum core_status status = 0;
    smp_get_core_status(gpu_cmd_core, &status);

    if (gpu_cmd_core != 0xFFFF && status == CORE_STATUS_RESERVED)
        smp_push_task(gpu_cmd_core, virtio_sync_poll); // Push task here
    else
        virtio_sync_poll(); // either no extra core, or core busy

    main_buf_slot++;
}

static void virtio_submit_sync(void* cmd, uint64_t cmd_phys, size_t cmd_size, void* resp, uint64_t resp_phys, size_t resp_size) {
    serial_printf("[VIRTIO] Submitting Sync [CMD: %lx, CMD PHYS: %lx, CMD SIZE: %lx]\n", (uint64_t)cmd, cmd_phys, cmd_size);
    uint16_t head = virtq.free_head;
    uint16_t next = (head + 1) % virtq.queue_size;

    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    // Descriptor 1: The Command (Read-only)
    serial_print("[VIRTIO] Setting Descriptor 1\n");
    virtq.desc[head].addr = (uintptr_t)cmd_phys;
    virtq.desc[head].len = cmd_size;
    virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    virtq.desc[head].next = next;

    // Descriptor 2: The Response (Write-only for GPU)
    serial_print("[VIRTIO] Setting Descriptor 2\n");
    virtq.desc[next].addr = (uintptr_t)resp_phys;
    virtq.desc[next].len = resp_size;
    virtq.desc[next].flags = VIRTQ_DESC_F_WRITE;
    virtq.desc[next].next = 0;

    virtq.avail->ring[virtq.avail->idx % virtq.queue_size] = head;
    __asm__ volatile("sfence" ::: "memory"); // Ensure GPU sees RAM update
    virtq.avail->idx++;
    spin_unlock_irqrestore(&virtq_lock, flags);

    // Notify Doorbell
    serial_print("[VIRTIO] Notifying GPU\n");
    uintptr_t db = notify_base + (common_cfg->queue_notify_off * notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    serial_print("[VIRTIO] Polling\n");
    virtio_sync_poll();
    main_buf_slot++;
}

static struct virtio_cap get_cap(uint8_t b, uint8_t s, uint8_t f, uint8_t target_type) {
    struct virtio_cap cap = {0};
    uint8_t cap_ptr = pcie_read(b, s, f, 0x34) & 0xFF;
    while (cap_ptr != 0) {
        uint32_t cap_hdr = pcie_read(b, s, f, cap_ptr);
        uint8_t cap_id = cap_hdr & 0xFF;

        if (cap_id == 0x09) { // Vendor Specific
            uint8_t type = (pcie_read(b, s, f, cap_ptr + 3) >> 24) & 0xFF;
            if (type == target_type) {
                cap.cap_ptr = cap_ptr;
                cap.bar = (pcie_read(b, s, f, cap_ptr + 4) >> 0) & 0xFF;
                cap.offset = pcie_read(b, s, f, cap_ptr + 8);
                cap.length = pcie_read(b, s, f, cap_ptr + 12);
                return cap;
            }
        }
        cap_ptr = (cap_hdr >> 8) & 0xFF;
    }
    return cap;
}

static void setup_queue(gpu_device_t* gpu, uint16_t q_idx) {
    serial_print("[VIRTIO] Setting Queues....\n");

    common_cfg->queue_select = q_idx;
    uint16_t size = common_cfg->queue_size;
    size_t desc_t_size = size * sizeof(struct virtq_desc);
    size_t avail_r_size = 6 + (2 * size);
    size_t used_r_size = 6 + (8 * size);

    uint64_t phys = 0;
    void* mem = (void*)avmf_alloc(desc_t_size + avail_r_size + used_r_size, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &phys);
    if (!mem) {serial_print("[VIRTIO] Failed to Allocate Memory!\n"); return;}
    if (!phys) {serial_print("[VIRTIO] Failed to retrieve physical address!\n"); return;}

    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    virtq.desc = (struct virtq_desc*)mem;
    virtq.avail = (struct virtq_avail*)((uintptr_t)mem + desc_t_size);
    virtq.used = (struct virtq_used*)((uintptr_t)virtq.avail + avail_r_size);
    virtq.queue_size = size;
    virtq.free_head = 0;
    spin_unlock_irqrestore(&virtq_lock, flags);

    common_cfg->queue_desc = phys;
    common_cfg->queue_avail = phys + desc_t_size;
    common_cfg->queue_used = (uintptr_t)phys + avail_r_size + desc_t_size;
    common_cfg->queue_enable = 1;

    serial_print("[VIRTIO] Queues ready!\n");
}

void virtio_flush(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id) {
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)cmd_buf[cur_buf_slot];
    memset(f, 0, sizeof(*f));
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->r.x = x;
    f->r.y = y;
    f->r.width = w;
    f->r.height = h;
    f->resource_id = resource_id;
    f->padding = 0;

    virtio_submit_async(f, cmd_buf_phys[cur_buf_slot], sizeof(*f), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

// Total hours wasted on virtio: 18 (I DID COOK)
void virtio_init(struct gpu_device* gpu) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uintptr_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing...\n");

    struct virtio_cap common_cap = get_cap(dev->bus, dev->slot, dev->func, 1);
    uint32_t bar_val = pcie_read_bar(dev->bus, dev->slot, dev->func, common_cap.bar);
    uintptr_t bar_phys = bar_val & ~0xF;
    uintptr_t common_cfg_phys = bar_phys + common_cap.offset;
    common_cfg = (volatile struct virtio_common_cfg*)(common_cfg_phys + AOS_DIRECT_MAP_BASE);

    struct virtio_cap notify_cap = get_cap(dev->bus, dev->slot, dev->func, 2);
    uint32_t n_bar_val = pcie_read_bar(dev->bus, dev->slot, dev->func, notify_cap.bar);
    notify_base = (n_bar_val & ~0xF) + notify_cap.offset + AOS_DIRECT_MAP_BASE;
    notify_multiplier = pcie_read(dev->bus, dev->slot, dev->func, notify_cap.cap_ptr + 16);

    // RESET
    common_cfg->device_status = 0;
    while (common_cfg->device_status != 0) { __asm__ volatile("pause"); }
    common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
    common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

    // Features
    common_cfg->device_feature_select = 0;
    uint32_t features = common_cfg->device_feature;
    common_cfg->device_feature_select = 0;
    common_cfg->driver_feature = features;

    common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_print("[VIRTIO] Failed to negotiate features!\n");
        return;
    }
    
    // setup buffers
    for (uint64_t i = 0; i < MAX_CMD_RESP_BUFS; i++) {
        cmd_buf[i] = (struct virtio_gpu_ctrl_hdr*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &cmd_buf_phys[i]);
        resp_buf[i] = (struct virtio_gpu_resp_display_info*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &resp_buf_phys[i]);
        if (!cmd_buf || !resp_buf) {
            serial_print("[VIRTIO] Failed to allocate command and response buffers!\n");
            return;
        }
    }
    main_buf_slot = 0;

    // setup Queue
    setup_queue(gpu, 0);

    common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;

    // Setup gpu core
    if (smp_get_first_free_core(&gpu_cmd_core) != 1) {
        gpu_cmd_core = 0xFFFF;
    }

    // Get fb info
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_ctrl_hdr* get_info = (struct virtio_gpu_ctrl_hdr*)cmd_buf[cur_buf_slot];
    memset(get_info, 0, sizeof(*get_info));
    get_info->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    virtio_submit_sync(get_info, cmd_buf_phys[cur_buf_slot], sizeof(*get_info), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_resp_display_info));
    struct virtio_gpu_resp_display_info* display = (struct virtio_gpu_resp_display_info*)resp_buf[cur_buf_slot];

    if (display->displays[0].enabled) {
        gpu->framebuffer->w = display->displays[0].r.width;
        gpu->framebuffer->h = display->displays[0].r.height;
    } else {
        // Fallback if the device doesn't have a preference
        gpu->framebuffer->w = 1024;
        gpu->framebuffer->h = 768;
    }
    gpu->framebuffer->bpp = 32;
    gpu->framebuffer->pitch = gpu->framebuffer->w * (gpu->framebuffer->bpp / 8);
    gpu->framebuffer->size = gpu->framebuffer->pitch * gpu->framebuffer->h;

    gpu->active = 1;
    serial_print("[VIRTIO] Initialization completed!\n");
}

void virtio_init_resources(struct gpu_device* gpu, int id) {
    pcie_device_t* dev = gpu->pcie_device;
    PCIe_FB* fb = gpu->framebuffer;
    uintptr_t bar0 = dev->bar0 & ~0xF;

    serial_print("[VIRTIO DRIVER] Initializing Resources...\n");

    // make resources
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_create_2d* create = (struct virtio_gpu_resource_create_2d*)cmd_buf[cur_buf_slot];
    memset(create, 0, sizeof(*create));
    create->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create->resource_id = id;
    create->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    create->width = fb->w;
    create->height = fb->h;
    virtio_submit_async(create, cmd_buf_phys[cur_buf_slot], sizeof(*create), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_attach_backing* attach = (struct virtio_gpu_resource_attach_backing*)cmd_buf[cur_buf_slot];
    memset(attach, 0, sizeof(*attach));
    attach->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach->resource_id = id;
    attach->nr_entries = 1;
    struct virtio_gpu_mem_entry* entry = (struct virtio_gpu_mem_entry*)((uintptr_t)attach + sizeof(*attach));
    entry->addr = fb->phys;
    entry->length = fb->size;
    virtio_submit_async(attach, cmd_buf_phys[cur_buf_slot], sizeof(*attach) + sizeof(*entry), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_set_scanout* scanout = (struct virtio_gpu_set_scanout*)cmd_buf[cur_buf_slot];
    memset(scanout, 0, sizeof(*scanout));
    scanout->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout->scanout_id = 0;
    scanout->resource_id = id;
    scanout->r.x = 0;
    scanout->r.y = 0;
    scanout->r.width = fb->w;
    scanout->r.height = fb->h;
    virtio_submit_async(scanout, cmd_buf_phys[cur_buf_slot], sizeof(*scanout), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));

    serial_print("[VIRTIO] Initialization of resources completed!\n");
}

void virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) {
    (void)gpu; (void)w; (void)h; (void)bpp;
}

void virtio_switch_off(struct gpu_device* gpu) {
    serial_print("[VIRTIO] Switching Off...\n");
    common_cfg->device_status = 0; // Reset
    while (common_cfg->device_status != 0) { __asm__ volatile("pause"); }
    serial_print("[VIRTIO] GPU Reset completed!\n[VIRTIO] Unmapping used memory!\n");

    // Unmap
    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    avmf_free((uint64_t)virtq.desc);
    for (uint64_t i = 0; i < MAX_CMD_RESP_BUFS; i++) {
        avmf_free((uint64_t)cmd_buf[i]);
        avmf_free((uint64_t)resp_buf[i]);
    }
    spin_unlock_irqrestore(&virtq_lock, flags);

    serial_print("[VIRTIO] Unmapping completed!\n");

    gpu->active = 0;
    serial_print("[VIRTIO] Switched off!\n");
}

static void virtio_create_context(uint32_t ctx_id) {
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;
    struct virtio_gpu_ctx_create* cmd = (struct virtio_gpu_ctx_create*)cmd_buf[cur_buf_slot];
    memset(cmd, 0, sizeof(*cmd));
    
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
    cmd->hdr.ctx_id = ctx_id;
    cmd->nlen = 6;
    memcpy(cmd->debug_name, "Pyrion", 6);
    
    virtio_submit_sync(cmd, cmd_buf_phys[cur_buf_slot], sizeof(*cmd), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

static void virtio_destroy_context(uint32_t ctx_id) {
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    uint64_t cur_buf_slot = main_buf_slot % MAX_CMD_RESP_BUFS;
    struct virtio_gpu_ctx_destroy* cmd = (struct virtio_gpu_ctx_destroy*)cmd_buf[cur_buf_slot];
    memset(cmd, 0, sizeof(*cmd));
    
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
    cmd->hdr.ctx_id = ctx_id;
    
    virtio_submit_sync(cmd, cmd_buf_phys[cur_buf_slot], sizeof(*cmd), resp_buf[cur_buf_slot], resp_buf_phys[cur_buf_slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

// Pyrion implementation
#include <inc/drivers/gpu/apis/pyrion.h>
#define MAX_PYRION_CONTEXTS 256

static struct pyrion_ctx* p_contexts[MAX_PYRION_CONTEXTS];
static uint64_t p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
static uint32_t current_font_resource_id = 999;

void pyrion_init_virtio(void) {
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        uint64_t ctx_phys = 0;
        uint64_t ctx_virt = (uint64_t)avmf_alloc(sizeof(struct pyrion_ctx), MALLOC_TYPE_KERNEL, PAGE_PRESENT | PAGE_RW, &ctx_phys);
        if (!ctx_phys || !ctx_virt) continue;

        struct pyrion_ctx* ctx = (struct pyrion_ctx*)ctx_virt;
        ctx->ctx_phys = ctx_phys;
        ctx->ctx_id = i;
        ctx->res_id = 0;
        ctx->valid = 0; // NOT VALID
        p_contexts[i] = ctx;
        if (p_nxt_ctx > MAX_PYRION_CONTEXTS) // Not set yet
            p_nxt_ctx = i;
    }
}

void pyrion_finish_virtio(void) {
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        struct pyrion_ctx* ctx = (struct pyrion_ctx*)p_contexts[i];
        if (!ctx || !ctx->ctx_phys) continue;

        if (ctx->valid == 1) {
            virtio_destroy_context(i);
            if (ctx->driver_data) avmf_free((uint64_t)ctx->driver_data);
        }

        avmf_free((uint64_t)ctx);
        p_contexts[i] = NULL;
    }
    p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
}

static uint64_t pyrion_get_free_ctx_slot(void) {
    if (p_nxt_ctx < MAX_PYRION_CONTEXTS) {
        uint64_t id = p_nxt_ctx;
        p_nxt_ctx = 0xFFFFFFFFFFFFFFFF;
        return id;
    }
    for (uint64_t i = 0; i < MAX_PYRION_CONTEXTS; i++) {
        if (p_contexts[i] && p_contexts[i]->valid == 0)
            return i;
    }
    return 0xFFFFFFFFFFFFFFFF;
}

static void pyrion_push_virgl(struct pyrion_ctx* ctx, uint32_t opcode, uint32_t obj_type, uint32_t* args, uint32_t arg_count) {
    if (ctx == NULL || ctx->driver_data == NULL) return;
    uint32_t* stream_base = (uint32_t*)ctx->driver_data;
    uint32_t current_size = stream_base[0];

    uint32_t* write_ptr = (uint32_t*)((uintptr_t)ctx->driver_data + sizeof(uint32_t) + current_size);
    *write_ptr = VIRGL_CMD_HEADER(opcode, obj_type, arg_count & 0xFF);
    write_ptr++;

    for (uint32_t i = 0; i < arg_count; i++) {
        *write_ptr = args[i];
        write_ptr++;
    }

    stream_base[0] += (1 + arg_count) * sizeof(uint32_t);
}

struct pyrion_ctx* pyrion_create_ctx_virtio(void) {
    serial_print("[Pyrion] Creating Context...\n");

    uint64_t slot = pyrion_get_free_ctx_slot();
    if (slot > MAX_PYRION_CONTEXTS) {
        serial_print("[Pyrion] Context Limit Reached!\n");
        return NULL;
    }

    struct pyrion_ctx* ctx = (struct pyrion_ctx*)p_contexts[slot];
    ctx->driver_data = (void*)avmf_alloc(0x1000, MALLOC_TYPE_DRIVER, PAGE_PRESENT | PAGE_RW, &ctx->driver_data_phys);
    if (!ctx->driver_data) return NULL;

    virtio_create_context(slot);
    
    ctx->viewport.x = 0; ctx->viewport.y=0; ctx->viewport.width=0; ctx->viewport.height; ctx->viewport.color = 0;
    ctx->valid = 1;

    serial_print("[Pyrion] Context Created\n");
    return ctx;
}

void pyrion_destroy_ctx_virtio(struct pyrion_ctx* ctx) {
    if (ctx == NULL || ctx->ctx_id > 0) return;

    virtio_destroy_context(ctx->ctx_id);

    if (ctx->driver_data) avmf_free((uint64_t)ctx->driver_data);

    p_nxt_ctx = ctx->ctx_id;
    ctx->valid = 0;
}

void pyrion_viewport_virtio(struct pyrion_ctx* ctx, struct pyrion_rect* viewport) {
    if (ctx == NULL || ctx->ctx_id > MAX_PYRION_CONTEXTS) return;
    uint64_t res_id = ctx->ctx_id + MAX_PYRION_CONTEXTS; // Ensure not a single resource id causes trouble

    // Create a 3D Resource (Texture) on the Host GPU
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = main_buf_slot % MAX_CMD_RESP_BUFS;
    
    struct virtio_gpu_resource_create_3d* c3d = (struct virtio_gpu_resource_create_3d*)cmd_buf[slot];
    memset(c3d, 0, sizeof(*c3d));
    c3d->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    c3d->resource_id = res_id;
    c3d->target = VIRTIO_GPU_PIPE_TEXTURE_2D;
    c3d->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    c3d->width = viewport->width;
    c3d->height = viewport->height;
    c3d->depth = 1;
    c3d->last_level = 0;
    c3d->nr_samples = 0;
    c3d->flags = 0;

    virtio_submit_sync(c3d, cmd_buf_phys[slot], sizeof(*c3d), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Attach the resource to the 3D Context
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_attach_backing* att = (struct virtio_gpu_resource_attach_backing*)cmd_buf[slot];
    memset(att, 0, sizeof(*att));
    att->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    att->hdr.ctx_id = ctx->ctx_id;
    att->resource_id = res_id;
    
    virtio_submit_sync(att, cmd_buf_phys[slot], sizeof(*att), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    ctx->res_id = res_id;
    ctx->viewport = *viewport;
}

void pyrion_flush_virtio(struct pyrion_ctx* ctx) {
    if (ctx == NULL || ctx->valid != 1) return;

    uint32_t stream_size = ctx->driver_data ? *(uint32_t*)(ctx->driver_data) : 0;
    if (stream_size == 0) return;
    if (!ctx->driver_data || stream_size < 1) {
        while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
            __asm__ volatile("pause"); 
        }
        uint64_t slot = main_buf_slot % MAX_CMD_RESP_BUFS;

        struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)cmd_buf[slot];
        memset(f, 0, sizeof(*f));
        f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
        f->hdr.ctx_id = ctx->ctx_id;
        f->r.x = ctx->viewport.x;
        f->r.y = ctx->viewport.y;
        f->r.width = ctx->viewport.width;
        f->r.height = ctx->viewport.height;
        f->resource_id = ctx->res_id;
        f->padding = 0;

        virtio_submit_async(f, cmd_buf_phys[slot], sizeof(*f), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));
        return;
    }

    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_cmd_submit_3d* virgl = (struct virtio_gpu_cmd_submit_3d*)cmd_buf[slot];
    virgl->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
    virgl->hdr.ctx_id = ctx->ctx_id;
    virgl->size = stream_size;

    uint16_t head = virtq.free_head;
    uint16_t next1 = (head + 1) % virtq.queue_size;
    uint16_t next2 = (next1 + 1) % virtq.queue_size;

    uint64_t flags = spin_lock_irqsave(&virtq_lock);
    // Descriptor 1: The Command (Read-only)
    virtq.desc[head].addr = (uintptr_t)cmd_buf_phys[slot];
    virtq.desc[head].len = sizeof(*virgl);
    virtq.desc[head].flags = VIRTQ_DESC_F_NEXT;
    virtq.desc[head].next = next1;

    // Descriptor 2: The stream (Ready-Only)
    virtq.desc[next1].addr = (uintptr_t)(ctx->driver_data_phys + sizeof(uint32_t));
    virtq.desc[next1].len = stream_size;
    virtq.desc[next1].flags = VIRTQ_DESC_F_NEXT;
    virtq.desc[next1].next = next2;

    // Descriptor 3: The response (Write-Only)
    virtq.desc[next2].addr = resp_buf_phys[slot];
    virtq.desc[next2].len = sizeof(struct virtio_gpu_ctrl_hdr);
    virtq.desc[next2].flags = VIRTQ_DESC_F_WRITE;
    virtq.desc[next2].next = 0;

    virtq.avail->ring[virtq.avail->idx % virtq.queue_size] = head;
    __asm__ volatile("sfence" ::: "memory");
    virtq.avail->idx++;

    virtq.free_head = (next2 + 1) % virtq.queue_size;
    spin_unlock_irqrestore(&virtq_lock, flags);

    // Notify Doorbell
    uintptr_t db = notify_base + (common_cfg->queue_notify_off * notify_multiplier);
    mmio_write32(db, 0);

    // Poll for completion
    while (virtq.used->idx != virtq.avail->idx) {
        __asm__ volatile("pause");
    }
    
    *(uint32_t*)(ctx->driver_data) = 0;
    
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) {
        __asm__ volatile("pause"); 
    }
    slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_flush* f = (struct virtio_gpu_resource_flush*)cmd_buf[slot];
    memset(f, 0, sizeof(*f));
    f->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    f->hdr.ctx_id = ctx->ctx_id;
    f->r.x = ctx->viewport.x;
    f->r.y = ctx->viewport.y;
    f->r.width = ctx->viewport.width;
    f->r.height = ctx->viewport.height;
    f->resource_id = ctx->res_id;
    f->padding = 0;

    virtio_submit_async(f, cmd_buf_phys[slot], sizeof(*f), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));
}

void pyrion_clear_virtio(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t args[5];
    args[0] = 0x7;
    args[1] = *(uint32_t*)&r;
    args[2] = *(uint32_t*)&g;
    args[3] = *(uint32_t*)&b;
    args[4] = *(uint32_t*)&a;

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);
}

void pyrion_pixel_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t scissor_args[2];
    scissor_args[0] = (y << 16) | x;
    scissor_args[1] = ((y + 1) << 16) | (x + 1);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
    
    uint32_t args[5];
    args[0] = 0x7;
    args[1] = *(uint32_t*)&r;
    args[2] = *(uint32_t*)&g;
    args[3] = *(uint32_t*)&b;
    args[4] = *(uint32_t*)&a;

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);
}

void pyrion_draw_char_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t atlas_x, uint32_t atlas_y, uint32_t w, uint32_t h, uint32_t font_res_id) {
    uint32_t args[13];
    args[0] = ctx->res_id; // Destination (The Window)
    args[1] = 0; // Dest level
    args[2] = x; // Dest X
    args[3] = y; // Dest Y
    args[4] = 0; // Dest Z
    args[5] = font_res_id; // Source (The Font Atlas)
    args[6] = 0; // Source level
    args[7] = atlas_x; // Source X (Where char is in atlas)
    args[8] = atlas_y; // Source Y
    args[9] = 0; // Source Z
    args[10] = w; // Width of char
    args[11] = h; // Height of char
    args[12] = 1; // Depth

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_RESOURCE_COPY_REGION, VIRTIO_VIRGL_OBJECT_NULL, args, 13);
}

void pyrion_destroy_font_virtio(uint32_t font_res_id, void* font_mem) {
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_unref* unref = (struct virtio_gpu_resource_unref*)cmd_buf[slot];
    memset(unref, 0, sizeof(*unref));
    unref->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    unref->resource_id = font_res_id;

    virtio_submit_sync(unref, cmd_buf_phys[slot], sizeof(*unref), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    if (font_mem) {
        avmf_free((uint64_t)font_mem);
    }
}

uint32_t pyrion_upload_font_virtio(struct pyrion_ctx* ctx, uint64_t atlas_phys, uint32_t* atlas, uint32_t atlas_w, uint32_t atlas_total_h) {
    uint32_t res_id = current_font_resource_id++;

    // Create 3D Resource
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    uint64_t slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_create_3d* c3d = (struct virtio_gpu_resource_create_3d*)cmd_buf[slot];
    memset(c3d, 0, sizeof(*c3d));
    c3d->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    c3d->hdr.ctx_id = ctx->ctx_id;
    c3d->target = VIRTIO_GPU_PIPE_TEXTURE_2D;
    c3d->format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
    c3d->width = atlas_w;
    c3d->height = atlas_total_h;
    c3d->depth = 1;
    virtio_submit_sync(c3d, cmd_buf_phys[slot], sizeof(*c3d), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Attach Backing
    struct cmd_struct {
        struct virtio_gpu_resource_attach_backing att;
        struct virtio_gpu_mem_entry entry;
    };
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct cmd_struct* cmd = (struct cmd_struct*)cmd_buf[slot];
    memset(cmd, 0, sizeof(*cmd));
    cmd->att.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->att.hdr.ctx_id = ctx->ctx_id;
    cmd->att.resource_id = res_id;
    cmd->att.nr_entries = 1;
    cmd->entry.addr = atlas_phys;
    cmd->entry.length = atlas_w * atlas_total_h * sizeof(uint32_t);
    virtio_submit_sync(cmd, cmd_buf_phys[slot], sizeof(*cmd), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    // Transfer to Host
    while (main_buf_slot - worker_buf_slot >= MAX_CMD_RESP_BUFS) { __asm__ volatile("pause"); }
    slot = main_buf_slot % MAX_CMD_RESP_BUFS;

    struct virtio_gpu_resource_create_3d* t = (struct virtio_gpu_resource_create_3d*)cmd_buf[slot];
    memset(t, 0, sizeof(*t));
    t->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D;
    t->hdr.ctx_id = ctx->ctx_id;
    t->resource_id = res_id;
    t->width = atlas_w;
    t->height = atlas_total_h;
    t->depth = 1;
    virtio_submit_sync(t, cmd_buf_phys[slot], sizeof(*t), resp_buf[slot], resp_buf_phys[slot], sizeof(struct virtio_gpu_ctrl_hdr));

    return res_id;
}

void pyrion_rect_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t scissor_args[2];
    scissor_args[0] = (y << 16) | x;
    scissor_args[1] = ((y + h) << 16) | (x + w);

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE, VIRTIO_VIRGL_OBJECT_NULL, scissor_args, 2);
    
    uint32_t args[5];
    args[0] = 0x7;
    args[1] = *(uint32_t*)&r;
    args[2] = *(uint32_t*)&g;
    args[3] = *(uint32_t*)&b;
    args[4] = *(uint32_t*)&a;

    pyrion_push_virgl(ctx, VIRTIO_VIRGL_CCMD_CLEAR, VIRTIO_VIRGL_OBJECT_NULL, args, 5);
}