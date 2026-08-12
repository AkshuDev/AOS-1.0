#pragma once

#include <aos_inttypes.h>

#include <inc/core/pcie.h>
#include <inc/core/module.h>

#ifdef PBFS_WDRIVERS
    #undef PBFS_WDRIVERS
#endif
#define PBFS_NDRIVERS
#include <PBFS/headers/pbfs-fs.h>
#undef PBFS_NDRIVERS

typedef uint32_t nvme_nssr_t;
typedef uint64_t nvme_asq_t;
typedef uint64_t nvme_acq_t;

union nvme_cap {
    uint64_t val;
    struct {
        uint16_t mqes; // Max Queue Entries Supported
        uint8_t cqr : 1; // Contiguous Queues Required
        uint8_t ams : 2; // Arbitration Mechanism Supported
        uint8_t rsvd1 : 5;
        uint8_t to; // Timeout value in 500ms units
        uint8_t dstrd : 4; // Doorbell Stride (2 ^ (2 + dstrd) bytes)
        uint8_t nssrs : 1; // NVM Subsystem Reset Supported
        uint8_t css; // Command Sets Supported
        uint8_t bps : 1; // Boot Partition Support
        uint8_t rsvd2 : 2;
        uint8_t mpsmin: 4; // Memory Page Size Minimum (2 ^ (12 + mpsmin))
        uint8_t mpsmax : 4; // Memory Page Size Maximum (2 ^ (12 + mpsmax))
        uint8_t pmrs : 1; // Persistent Memory Region Supported
        uint8_t cmbs : 1; // Controller Memory Buffer Supported
        uint8_t rsvd3 : 6;
    };
} __attribute__((packed));

union nvme_vs {
    uint32_t value;

    struct {
        uint8_t ter; // Tertiary Version
        uint8_t min; // Minor Version
        uint16_t maj; // Major Version
    };
} __attribute__((packed));

union nvme_intms {
    uint32_t value;

    struct {
        uint32_t ivms;
    };
} __attribute__((packed));

union nvme_intmc {
    uint32_t value;

    struct {
        uint32_t ivmc;
    };
} __attribute__((packed));

union nvme_cc {
    uint32_t value;

    struct {
        uint8_t en : 1; // Enable
        uint8_t rsvd1 : 3;

        uint8_t css : 3; // I/O Command Set Selected
        uint8_t mps : 4; // Memory Page Size

        uint8_t ams : 3; // Arbitration Mechanism Selected

        uint8_t shn : 2; // Shutdown Notification
        uint8_t iosqes : 4; // I/O SQ Entry Size
        uint8_t iocqes : 4; // I/O CQ Entry Size

        uint8_t rsvd2 : 4;
    };
} __attribute__((packed));

union nvme_csts {
    uint32_t value;

    struct {
        uint8_t rdy : 1;  // Ready
        uint8_t cfs : 1;  // Controller Fatal Status
        uint8_t shst : 2;  // Shutdown Status
        uint8_t nssro : 1; // NVM Subsystem Reset Occurred
        uint8_t pp : 1; // Processing Paused
 
        uint32_t rsvd : 26;
    };
} __attribute__((packed));

union nvme_aqa {
    uint32_t value;

    struct {
        uint16_t asqs : 12; // Admin SQ Size
        uint8_t rsvd : 4;
        uint16_t acqs : 12; // Admin CQ Size
        uint8_t rsvd2 : 4;
    };
} __attribute__((packed));

struct nvme_regs {
    volatile union nvme_cap cap;
    volatile union nvme_vs vs;
    volatile union nvme_intms intms;
    volatile union nvme_intmc intmc;
    volatile union nvme_cc cc;
    volatile uint32_t rsvd1;
    volatile union nvme_csts csts;
    volatile nvme_nssr_t nssr;
    volatile union nvme_aqa aqa;
    volatile nvme_asq_t asq;
    volatile nvme_acq_t acq;
} __attribute__((packed));

struct nvme_command {
    uint32_t cdw0;
    uint32_t nsid;

    uint32_t rsvd2;
    uint32_t rsvd3;

    uint64_t mptr;

    uint64_t prp1;
    uint64_t prp2;

    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct nvme_completion {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed));

struct nvme_admin_queue {
    struct nvme_command* sq;
    uint64_t sq_phys;
	uint16_t sq_tail;

    struct nvme_completion* cq;
    uint64_t cq_phys;
    uint16_t cq_head;
    uint8_t cq_phase;
};

aos_bool nvme_init(struct AOS_Module* m) __attribute__((used));
void nvme_get_pcie(uint64_t cidx, pcie_device_t* out) __attribute__((used));
aos_bool nvme_read_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_write_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_flush(uint64_t cidx, int port_id) __attribute__((used));
aos_bool nvme_get_block_device(uint64_t cidx, int port_id, struct block_device* out) __attribute__((used));
void nvme_get_available_ports(uint64_t cidx, uint8_t* out, int out_size) __attribute__((used));