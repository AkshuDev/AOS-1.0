#pragma once

#include <aos_inttypes.h>
#include <stddef.h>
#include <inc/drivers/gpu/gpu.h>
#include <inc/drivers/io/drive.h>

enum AOS_ModuleTypes {
    MODULE_TYPE_DRIVER,
    MODULE_TYPE_EXECUTABLE_RUNTIME
};

enum AOS_ModuleDriverTypes {
    MODULE_DRIVER_TYPE_SATA,
    MODULE_DRIVER_TYPE_HDD,
    MODULE_DRIVER_TYPE_NVMe,
    MODULE_DRIVER_TYPE_GPU,
    MODULE_DRIVER_TYPE_xHCI
};

struct AOS_ModuleHeader {
    char* name;
    int version;
    uint64_t signature;
    enum AOS_ModuleTypes type;

    aos_bool registered;
};

struct AOS_ModuleDriver {
    enum AOS_ModuleDriverTypes type;

    uint8_t target_class;
    uint8_t target_subclass;
    uint8_t target_progifclass;
    uint8_t target_use_progifclass;
    uint8_t target_revision;
    uint8_t target_use_revision;
    uint16_t target_vendor;
    uint8_t target_use_vendor;

    pcie_device_t pcie_device;

    union {
        gpu_device_t gpu_connector;
        drive_device_t drive_connector;
    } DriverConnections;
};

struct AOS_Module {
    struct AOS_ModuleHeader hdr;
	aos_bool initialize_on_register;
	aos_bool (*init_module)(struct AOS_Module* self);
    union {
        struct AOS_ModuleDriver driver_module;
    } Modules;
};

aos_bool modules_init(void) __attribute__((used));
aos_bool module_register(struct AOS_Module* module) __attribute__((used));
aos_bool module_unregister(struct AOS_Module* module) __attribute__((used));
aos_bool module_unregister_dealloc(struct AOS_Module* module) __attribute__((used));
aos_bool module_already_initialized(struct AOS_Module* module) __attribute__((used));
size_t module_get_registered_module_count(void) __attribute__((used));

struct AOS_Module* module_get_applicable_driver(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));
struct AOS_Module* module_get_applicable_registered_driver(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));
struct AOS_Module* module_get_applicable_driver_alloced(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));
struct AOS_Module* module_get_applicable_registered_driver_alloced(uint64_t loop_idx, uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));

#define module_get_first_applicable_driver(class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor) module_get_applicable_driver(0, class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor)
#define module_get_first_applicable_registered_driver(class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor) module_get_applicable_registered_driver(0, class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor)
#define module_get_first_applicable_driver_alloced(class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor) module_get_applicable_driver_alloced(0, class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor)
#define module_get_first_applicable_registered_driver_alloced(class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor) module_get_applicable_registered_driver_alloced(0, class, subclass, use_subclass, progif, use_progif, revision, use_revision, vendor, use_vendor)
