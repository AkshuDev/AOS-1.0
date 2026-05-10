#pragma once

#include <inttypes.h>
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
    //MODULE_DRIVER_TYPE_xHCI
};

struct AOS_ModuleHeader {
    char* name;
    int version;
    uint64_t signature;
    enum AOS_ModuleTypes type;

    uint8_t registered;
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
    union {
        struct AOS_ModuleDriver driver_module;
    } Modules;
};

uint8_t modules_init(void) __attribute__((used));
uint8_t module_register(struct AOS_Module* module) __attribute__((used));
uint8_t module_already_initialized(struct AOS_Module* module) __attribute__((used));

struct AOS_Module* module_get_first_applicable_driver(uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));
struct AOS_Module* module_get_first_applicable_registered_driver(uint8_t class, uint8_t subclass, uint8_t use_subclass, uint8_t progif, uint8_t use_progif, uint8_t revision, uint8_t use_revision, uint16_t vendor, uint8_t use_vendor) __attribute__((used));
