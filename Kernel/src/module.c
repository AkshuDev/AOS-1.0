#include <aos_inttypes.h>

#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/core/kfuncs.h>

#include <inc/drivers/io/io.h>

#include <inc/drivers/io/sata.h>
#include <inc/drivers/gpu/virtio.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/gpu/apis/pyrion.h>
#include <inc/drivers/core/framebuffer.h>
#include <inc/drivers/usb/xhci/xhci.h>

#include <inc/core/module.h>

#define AOS_MODULE_SIGN(id) ((0x414F53ULL << 32) | ((uint64_t)(id) & 0xFFFFFFFFULL))
#define AOS_MODULE_PACK_VERSION(major, minor, patch) ((((major) & 0xFF) << 24) | (((minor) & 0xFF) << 16) | ((patch) & 0xFFFF))

static struct AOS_Module* modules = NULL;
static size_t module_count = 0;
static size_t module_cap = 0;

static struct AOS_Module** registered_modules = NULL;
static size_t registered_module_count = 0;
static size_t registered_module_cap = 0;

aos_bool modules_init(void) {
    // Loads core modules and more
    modules = (struct AOS_Module*)avmf_alloc(sizeof(struct AOS_Module)*16, MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
    if (!modules) {
        serial_print("[Module-System] Allocation failed!\n");
        return AOS_FALSE;
    }
    module_cap = 16;

    int id = module_count++;
    // SATA
	#ifndef MODULE_NSATA
    modules[id].hdr = (struct AOS_ModuleHeader){
        .name = "AOS-inb-driver-SATA",
        .signature = AOS_MODULE_SIGN(id),
        .version = AOS_MODULE_PACK_VERSION(1, 0, 0),
        .type = MODULE_TYPE_DRIVER,
        .registered = AOS_FALSE
    };
    modules[id].initialize_on_register = AOS_TRUE;
	modules[id].init_module = sata_init;
	modules[id].Modules.driver_module = (struct AOS_ModuleDriver){
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
            .active = AOS_FALSE,
            .init = sata_init,
            .read_blk = sata_read_blk,
            .write_blk = sata_write_blk,
            .flush = sata_flush,
            .get_block_device = sata_get_block_device,
            .cur_port = 0,
			.controller_idx = 0,
            .pcie_device = NULL,
            .name = NULL,
            .block_dev = (struct block_device){0}
        }
    };
	#endif

    // Virtio-GPU
	#ifndef MODULE_NVIRTIO
    id = module_count++;
    modules[id].hdr = (struct AOS_ModuleHeader){
        .name = "AOS-inb-driver-gpu-VIRTIO",
        .signature = AOS_MODULE_SIGN(id),
        .version = AOS_MODULE_PACK_VERSION(1, 0, 0),
        .type = MODULE_TYPE_DRIVER,
        .registered = AOS_FALSE
    };
    modules[id].initialize_on_register = AOS_TRUE;
	modules[id].init_module = virtio_init;
	modules[id].Modules.driver_module = (struct AOS_ModuleDriver){
        .type = MODULE_DRIVER_TYPE_GPU,
        
        .target_class = PCI_VGA_DISPLAY,
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

            .pyrion = (struct pyrion_api){
                .init = pyrion_init_virtio,
                .finish = pyrion_finish_virtio,
                .create_ctx = pyrion_create_ctx_virtio,
                .destroy_ctx = pyrion_destroy_ctx_virtio,
                .flush = pyrion_flush_virtio,
                .viewport = pyrion_viewport_virtio,
                .clear = pyrion_clear_virtio,
                .pixel = pyrion_pixel_virtio,
                .draw_rect = pyrion_rect_virtio,
                .upload_font = pyrion_upload_font_virtio,
                .destroy_font = pyrion_destroy_font_virtio,
                .draw_char = pyrion_draw_char_virtio
            },

            .active = AOS_FALSE,
        }
    };
	#endif

	// xHCI
	#ifndef MODULE_NXHCI
    id = module_count++;
    modules[id].hdr = (struct AOS_ModuleHeader){
        .name = "AOS-inb-driver-usb-xHCI",
        .signature = AOS_MODULE_SIGN(id),
        .version = AOS_MODULE_PACK_VERSION(1, 0, 0),
        .type = MODULE_TYPE_DRIVER,
        .registered = AOS_FALSE
    };
    modules[id].initialize_on_register = AOS_TRUE;
	modules[id].init_module = xhci_init;
	modules[id].Modules.driver_module = (struct AOS_ModuleDriver){
        .type = MODULE_DRIVER_TYPE_xHCI,
        
        .target_class = PCI_CLASS_SERIAL_BUS_CONTROLLER,
        .target_subclass = PCI_SUBCLASS_USB,
        .target_progifclass = XHCI_PROGRAMMIMG_INTERFACE,
        .target_use_progifclass = 1,
        .target_revision = 0,
        .target_use_revision = 0,
        .target_vendor = 0,
        .target_use_vendor = 0,
    };
	#endif

    serial_print("[Module-System] Core Modules Loaded!\n");

    return AOS_TRUE;
}

aos_bool module_register(struct AOS_Module* module) {
	for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        
        if (!mod) {
			registered_modules[i] = module;
			module->hdr.registered = AOS_TRUE;
			return AOS_TRUE;
		}
    }

    if (!registered_modules || registered_module_count >= registered_module_cap) {
        struct AOS_Module** nptr = (struct AOS_Module**)avmf_alloc(sizeof(struct AOS_Module*)*(registered_module_cap + 16), MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
        if (!nptr) {
            serial_print("[Module-System] Reallocation of registered modules failed!\n");
            return AOS_FALSE;
        }
        registered_module_cap += 16;
        if (registered_modules) {
            memcpy(nptr, registered_modules, sizeof(struct AOS_Module*)*registered_module_count);
            avmf_free((uint64_t)registered_modules);
        }
        registered_modules = nptr;
    }
    registered_modules[registered_module_count++] = module;
    module->hdr.registered = AOS_TRUE;
    return AOS_TRUE;
}

aos_bool module_unregister(struct AOS_Module* module) {
    for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        
        if (module == mod) {
			registered_modules[i] = NULL;
		}
    }
    return AOS_TRUE;
}

aos_bool module_unregister_dealloc(struct AOS_Module* module) {
    for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        
        if (module == mod) {
			avmf_free((uint64_t)registered_modules[i]);
			registered_modules[i] = NULL;
		}
    }
    return AOS_TRUE;
}

struct AOS_Module* module_get_applicable_driver(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) {
	uint64_t found_count = 0;
    for (size_t i = 0; i < module_count; i++) {
        struct AOS_Module* mod = &modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class) {
            if (use_subclass == 1 && mod->Modules.driver_module.target_subclass != subclass) continue;
            if (use_progif == 1 && mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (use_revision == 1 && mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (use_vendor == 1 && mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != vendor) continue;
            }

			if (loop_idx != found_count++) continue;
            
            return mod;
        }
    }
    return NULL;
}

struct AOS_Module* module_get_applicable_registered_driver(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) {
    uint64_t found_count = 0;
	for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class) {
            if (use_subclass == 1 && mod->Modules.driver_module.target_subclass != subclass) continue;
            if (use_progif == 1 && mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (use_revision == 1 && mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (use_vendor == 1 && mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != vendor) continue;
            }

			if (loop_idx != found_count++) continue;

            return mod;
        }
    }
    return NULL;
}

struct AOS_Module* module_get_applicable_driver_alloced(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) {
    uint64_t found_count = 0;
	for (size_t i = 0; i < module_count; i++) {
        struct AOS_Module* mod = &modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class) {
            if (use_subclass == 1 && mod->Modules.driver_module.target_subclass != subclass) continue;
            if (use_progif == 1 && mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (use_revision == 1 && mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (use_vendor == 1 && mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != vendor) continue;
            }

			if (loop_idx != found_count++) continue;

			struct AOS_Module* nmod = (struct AOS_Module*)avmf_alloc(sizeof(struct AOS_Module), MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
			if (!nmod) return NULL;

			memcpy(nmod, mod, sizeof(struct AOS_Module));
            return nmod;
        }
    }
    return NULL;
}

struct AOS_Module* module_get_applicable_registered_driver_alloced(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) {
    uint64_t found_count = 0;
	for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        if (mod->hdr.type != MODULE_TYPE_DRIVER) continue;
        
        if (mod->Modules.driver_module.target_class == class) {
            if (use_subclass == 1 && mod->Modules.driver_module.target_subclass != subclass) continue;
            if (use_progif == 1 && mod->Modules.driver_module.target_use_progifclass == 1) {
                if (mod->Modules.driver_module.target_progifclass != progif) continue;
            }
            if (use_revision == 1 && mod->Modules.driver_module.target_use_revision == 1) {
                if (mod->Modules.driver_module.target_revision != revision) continue;
            }
            if (use_vendor == 1 && mod->Modules.driver_module.target_use_vendor == 1) {
                if (mod->Modules.driver_module.target_vendor != vendor) continue;
            }

			if (loop_idx != found_count++) continue;
			
            struct AOS_Module* nmod = (struct AOS_Module*)avmf_alloc(sizeof(struct AOS_Module), MALLOC_TYPE_KERNEL, PAGE_RW | PAGE_PRESENT, NULL);
			if (!nmod) return NULL;

			memcpy(nmod, mod, sizeof(struct AOS_Module));
            return nmod;
        }
    }
    return NULL;
}

aos_bool module_already_initialized(struct AOS_Module* module) {
    for (size_t i = 0; i < registered_module_count; i++) {
        struct AOS_Module* mod = registered_modules[i];
        if (mod == module) return AOS_TRUE;
    }
    return AOS_FALSE;
}

size_t module_get_registered_module_count(void) {
	return registered_module_count;
}
