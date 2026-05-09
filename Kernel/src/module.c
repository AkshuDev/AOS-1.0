#include <inttypes.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/drivers/io/io.h>

#include <inc/drivers/io/sata.h>
#include <inc/drivers/gpu/virtio.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/core/framebuffer.h>

#include <inc/core/module.h>

#define AOS_MODULE_SIGN(id) ((0x414F53ULL << 32) | ((uint64_t)(id) & 0xFFFFFFFFULL))
#define AOS_MODULE_PACK_VERSION(major, minor, patch) ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | ((patch) & 0xFFFF))

static struct AOS_Module* modules = NULL;
static size_t module_count = 0;
static size_t module_cap = 0;

static struct AOS_Module** registered_modules = NULL;
static size_t registered_module_count = 0;
static size_t registered_module_cap = 0;

uint8_t modules_init(void) {
    // Loads core modules and more
    modules = (struct AOS_Module*)avmf_alloc(sizeof(struct AOS_Module)*16, MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
    if (!modules) {
        serial_print("[Module-System] Allocation failed!\n");
        return 0;
    }
    module_cap = 16;

    int id = module_count++;
    // SATA
    modules[id].hdr = (struct AOS_ModuleHeader){
        .name = "AOS-inb-driver-SATA",
        .signature = AOS_MODULE_SIGN(id),
        .version = AOS_MODULE_PACK_VERSION(1, 0, 0),
        .type = MODULE_TYPE_DRIVER
    };
    modules[module_count].Modules.driver_module = (struct AOS_ModuleDriver){
        .type = MODULE_DRIVER_TYPE_SATA,
        
        .target_class = MASS_STORAGE_CLASS,
        .target_subclass = SATA_SUBCLASS,
        .target_progifclass = AHCI_PROGIF,
        .target_use_progifclass = 1,
        .target_revision = 0,
        .target_use_revision = 0,
        .target_vendor = 0,
        .target_use_vendor = 0,

        .DriverConnections.drive_connector = (struct drive_device){
            .active = 0,
            .init = sata_init,
            .read_blk = sata_read_blk,
            .write_blk = sata_write_blk,
            .flush = sata_flush,
            .get_block_device = sata_get_block_device,
            .cur_port = 0,
            .pcie_device = NULL,
            .name = NULL,
            .block_dev = (struct block_device){0}
        }
    };

    // Virtio-GPU
    id = module_count++;
    modules[id].hdr = (struct AOS_ModuleHeader){
        .name = "AOS-inb-driver-gpu-VIRTIO",
        .signature = AOS_MODULE_SIGN(id),
        .version = AOS_MODULE_PACK_VERSION(1, 0, 0),
        .type = MODULE_TYPE_DRIVER
    };
    modules[module_count].Modules.driver_module = (struct AOS_ModuleDriver){
        .type = MODULE_DRIVER_TYPE_GPU,
        
        .target_class = PCI_CLASS_DISPLAY,
        .target_subclass = PCI_SUBCLASS_VGA,
        .target_progifclass = 0,
        .target_use_progifclass = 0,
        .target_revision = 0,
        .target_use_revision = 0,
        .target_vendor = VirtIo_VENDORID,
        .target_use_vendor = 1,

        .DriverConnections.gpu_connector = (struct gpu_device){
            .name = "VirtIo",
            .pcie_device = NULL,
            .framebuffer = NULL,
            .init = virtio_init,
            .init_resources = virtio_init_resources,
            .set_mode = virtio_set_mode,
            .swap_buffers = NULL,
            .flush = virtio_flush,
            .switch_off = virtio_switch_off,

            .pyrion.init = pyrion_init_virtio,
            .pyrion.finish = pyrion_finish_virtio,

            .pyrion.create_ctx = pyrion_create_ctx_virtio,
            .pyrion.destroy_ctx = pyrion_destroy_ctx_virtio,

            .pyrion.flush = pyrion_flush_virtio,
            .pyrion.viewport = pyrion_viewport_virtio,

            .pyrion.clear = pyrion_clear_virtio,
            .pyrion.pixel = pyrion_pixel_virtio,
            .pyrion.draw_rect = pyrion_rect_virtio,

            .pyrion.upload_font = pyrion_upload_font_virtio,
            .pyrion.destroy_font = pyrion_destroy_font_virtio,

            .pyrion.draw_char = pyrion_draw_char_virtio,

            .active = 0,
        }
    };

    serial_print("[Module-System] Core Modules Loaded!\n");

    return 1;
}

uint8_t module_register(struct AOS_Module* module) {
    if (!registered_modules || registered_module_count >= registered_module_cap) {
        struct AOS_Module** nptr = (struct AOS_Module**)avmf_alloc(sizeof(struct AOS_Module*)*(registered_module_cap + 16), MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
        if (!nptr) {
            serial_print("[Module-System] Reallocation of registered modules failed!\n");
            return 0;
        }
        registered_module_cap += 16;
        if (registered_modules) {
            
        }
    }
}

struct AOS_Module* module_get_first_applicable_driver(uint8_t class, uint8_t subclass, uint8_t progif, uint8_t revision, uint16_t vendor) {
    for (size_t i = 0; i < module_count; i++) {
        struct AOS_Module* mod = &modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class && mod->Modules.driver_module.target_subclass == subclass) {
            if (mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != progif) continue;
            }
            return mod;
        }
    }
    return NULL;
}

struct AOS_Module* module_get_first_applicable_registered_driver(uint8_t class, uint8_t subclass, uint8_t progif, uint8_t revision, uint16_t vendor) {
    for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class && mod->Modules.driver_module.target_subclass == subclass) {
            if (mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != progif) continue;
            }
            return mod;
        }
    }
    return NULL;
}

