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

// NVMe Capabilities
#define NVME_CAP_MQES_SHIFT 0
#define NVME_CAP_MQES_MASK (0xFFFFULL << NVME_CAP_MQES_SHIFT)
#define NVME_CAP_MQES(v) (((v) & NVME_CAP_MQES_MASK) >> NVME_CAP_MQES_SHIFT)

#define NVME_CAP_CQR_SHIFT 16
#define NVME_CAP_CQR_MASK (0x1ULL << NVME_CAP_CQR_SHIFT)
#define NVME_CAP_CQR(v) (((v) & NVME_CAP_CQR_MASK) >> NVME_CAP_CQR_SHIFT)

#define NVME_CAP_AMS_SHIFT 17
#define NVME_CAP_AMS_MASK (0x7ULL << NVME_CAP_AMS_SHIFT)
#define NVME_CAP_AMS(v) (((v) & NVME_CAP_AMS_MASK) >> NVME_CAP_AMS_SHIFT)

#define NVME_CAP_TO_SHIFT 24
#define NVME_CAP_TO_MASK (0xFFULL << NVME_CAP_TO_SHIFT)
#define NVME_CAP_TO(v) (((v) & NVME_CAP_TO_MASK) >> NVME_CAP_TO_SHIFT)

#define NVME_CAP_DSTRD_SHIFT 32
#define NVME_CAP_DSTRD_MASK (0xFULL << NVME_CAP_DSTRD_SHIFT)
#define NVME_CAP_DSTRD(v) (((v) & NVME_CAP_DSTRD_MASK) >> NVME_CAP_DSTRD_SHIFT)

#define NVME_CAP_NSSRS_SHIFT 36
#define NVME_CAP_NSSRS_MASK (0x1ULL << NVME_CAP_NSSRS_SHIFT)
#define NVME_CAP_NSSRS(v) (((v) & NVME_CAP_NSSRS_MASK) >> NVME_CAP_NSSRS_SHIFT)

#define NVME_CAP_CSS_SHIFT 37
#define NVME_CAP_CSS_MASK (0xFFULL << NVME_CAP_CSS_SHIFT)
#define NVME_CAP_CSS(v) (((v) & NVME_CAP_CSS_MASK) >> NVME_CAP_CSS_SHIFT)

#define NVME_CAP_BPS_SHIFT 45
#define NVME_CAP_BPS_MASK (0x1ULL << NVME_CAP_BPS_SHIFT)
#define NVME_CAP_BPS(v) (((v) & NVME_CAP_BPS_MASK) >> NVME_CAP_BPS_SHIFT)

#define NVME_CAP_MPSMIN_SHIFT 48
#define NVME_CAP_MPSMIN_MASK (0xFULL << NVME_CAP_MPSMIN_SHIFT)
#define NVME_CAP_MPSMIN(v) (((v) & NVME_CAP_MPSMIN_MASK) >> NVME_CAP_MPSMIN_SHIFT)

#define NVME_CAP_MPSMAX_SHIFT 52
#define NVME_CAP_MPSMAX_MASK (0xFULL << NVME_CAP_MPSMAX_SHIFT)
#define NVME_CAP_MPSMAX(v) (((v) & NVME_CAP_MPSMAX_MASK) >> NVME_CAP_MPSMAX_SHIFT)

#define NVME_CAP_PMRS_SHIFT 56
#define NVME_CAP_PMRS_MASK (0x1ULL << NVME_CAP_PMRS_SHIFT)
#define NVME_CAP_PMRS(v) (((v) & NVME_CAP_PMRS_MASK) >> NVME_CAP_PMRS_SHIFT)

#define NVME_CAP_CMBS_SHIFT 57
#define NVME_CAP_CMBS_MASK (0x1ULL << NVME_CAP_CMBS_SHIFT)
#define NVME_CAP_CMBS(v) (((v) & NVME_CAP_CMBS_MASK) >> NVME_CAP_CMBS_SHIFT)

// NVMe Version
#define NVME_VS_TER_SHIFT 0
#define NVME_VS_TER_MASK (0xFFU << NVME_VS_TER_SHIFT)
#define NVME_VS_TER(v) (((v) & NVME_VS_TER_MASK) >> NVME_VS_TER_SHIFT)

#define NVME_VS_MIN_SHIFT 8
#define NVME_VS_MIN_MASK (0xFFU << NVME_VS_MIN_SHIFT)
#define NVME_VS_MIN(v) (((v) & NVME_VS_MIN_MASK) >> NVME_VS_MIN_SHIFT)

#define NVME_VS_MAJ_SHIFT 16
#define NVME_VS_MAJ_MASK (0xFFFFU << NVME_VS_MAJ_SHIFT)
#define NVME_VS_MAJ(v) (((v) & NVME_VS_MAJ_MASK) >> NVME_VS_MAJ_SHIFT)

// NVMe Controller Configuration
#define NVME_CC_EN_SHIFT 0
#define NVME_CC_EN_MASK (0x1U << NVME_CC_EN_SHIFT)
#define NVME_CC_EN(v) (((v) & NVME_CC_EN_MASK) >> NVME_CC_EN_SHIFT)

#define NVME_CC_CSS_SHIFT 4
#define NVME_CC_CSS_MASK (0x7U << NVME_CC_CSS_SHIFT)
#define NVME_CC_CSS(v) (((v) & NVME_CC_CSS_MASK) >> NVME_CC_CSS_SHIFT)

#define NVME_CC_MPS_SHIFT 7
#define NVME_CC_MPS_MASK (0xFU << NVME_CC_MPS_SHIFT)
#define NVME_CC_MPS(v) (((v) & NVME_CC_MPS_MASK) >> NVME_CC_MPS_SHIFT)

#define NVME_CC_AMS_SHIFT 11
#define NVME_CC_AMS_MASK (0x7U << NVME_CC_AMS_SHIFT)
#define NVME_CC_AMS(v) (((v) & NVME_CC_AMS_MASK) >> NVME_CC_AMS_SHIFT)

#define NVME_CC_SHN_SHIFT 14
#define NVME_CC_SHN_MASK (0x3U << NVME_CC_SHN_SHIFT)
#define NVME_CC_SHN(v) (((v) & NVME_CC_SHN_MASK) >> NVME_CC_SHN_SHIFT)

#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_IOSQES_MASK (0xFU << NVME_CC_IOSQES_SHIFT)
#define NVME_CC_IOSQES(v) (((v) & NVME_CC_IOSQES_MASK) >> NVME_CC_IOSQES_SHIFT)

#define NVME_CC_IOCQES_SHIFT 20
#define NVME_CC_IOCQES_MASK (0xFU << NVME_CC_IOCQES_SHIFT)
#define NVME_CC_IOCQES(v) (((v) & NVME_CC_IOCQES_MASK) >> NVME_CC_IOCQES_SHIFT)

// NVMe Controller Status
#define NVME_CSTS_RDY_SHIFT 0
#define NVME_CSTS_RDY_MASK (0x1U << NVME_CSTS_RDY_SHIFT)
#define NVME_CSTS_RDY(v) (((v) & NVME_CSTS_RDY_MASK) >> NVME_CSTS_RDY_SHIFT)

#define NVME_CSTS_CFS_SHIFT 1
#define NVME_CSTS_CFS_MASK (0x1U << NVME_CSTS_CFS_SHIFT)
#define NVME_CSTS_CFS(v) (((v) & NVME_CSTS_CFS_MASK) >> NVME_CSTS_CFS_SHIFT)

#define NVME_CSTS_SHST_SHIFT 2
#define NVME_CSTS_SHST_MASK (0x3U << NVME_CSTS_SHST_SHIFT)
#define NVME_CSTS_SHST(v) (((v) & NVME_CSTS_SHST_MASK) >> NVME_CSTS_SHST_SHIFT)

#define NVME_CSTS_NSSRO_SHIFT 4
#define NVME_CSTS_NSSRO_MASK (0x1U << NVME_CSTS_NSSRO_SHIFT)
#define NVME_CSTS_NSSRO(v) (((v) & NVME_CSTS_NSSRO_MASK) >> NVME_CSTS_NSSRO_SHIFT)

#define NVME_CSTS_PP_SHIFT 5
#define NVME_CSTS_PP_MASK (0x1U << NVME_CSTS_PP_SHIFT)
#define NVME_CSTS_PP(v) (((v) & NVME_CSTS_PP_MASK) >> NVME_CSTS_PP_SHIFT)

// NVMe Admin Queue Attributes
#define NVME_AQA_ASQS_SHIFT 0
#define NVME_AQA_ASQS_MASK (0xFFFU << NVME_AQA_ASQS_SHIFT)
#define NVME_AQA_ASQS(v) (((v) & NVME_AQA_ASQS_MASK) >> NVME_AQA_ASQS_SHIFT)

#define NVME_AQA_ACQS_SHIFT 16
#define NVME_AQA_ACQS_MASK (0xFFFU << NVME_AQA_ACQS_SHIFT)
#define NVME_AQA_ACQS(v) (((v) & NVME_AQA_ACQS_MASK) >> NVME_AQA_ACQS_SHIFT)

struct nvme_regs {
    volatile uint64_t cap;
    volatile uint32_t vs;
    volatile uint32_t intms;
    volatile uint32_t intmc;
    volatile uint32_t cc;
    volatile uint32_t rsvd1;
    volatile uint32_t csts;
    volatile uint32_t nssr;
    volatile uint32_t aqa;
    volatile uint64_t asq;
    volatile uint64_t acq;
};

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
};

struct nvme_completion {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
};


#define NVME_ADMIN_QUEUE_SIZE 64
struct nvme_admin_queue {
    struct nvme_command* sq;
    uint64_t sq_phys;
	uint16_t sq_tail;
	uint16_t sq_count;

    struct nvme_completion* cq;
    uint64_t cq_phys;
    uint16_t cq_head;
    uint8_t cq_phase;

	volatile uint32_t* sq_db;
	volatile uint32_t* cq_db;

	uint16_t next_cid;
    uint64_t cid_bitmap[NVME_ADMIN_QUEUE_SIZE / 64];
};

typedef struct {
    aos_bool valid;

    uint32_t nsid;
	
    uint64_t block_count;
    uint32_t block_size;
} nvme_namespace;


enum nvme_opcodes {
	NVME_CMD_FLUSH = 0x0,
	NVME_CMD_WRITE,
	NVME_CMD_READ,
	NVME_CMD_WRITE_UNCOR = 0x4,
	NVME_CMD_COMPARE,
	NVME_CMD_WRITE_ZEROES = 0x8,
	NVME_CMD_DSM,
	NVME_CMD_VERIFY = 0xC,
	NVME_CMD_RESV_REGISTER,
	NVME_CMD_RESV_REPORT,
	NVME_CMD_RESV_AQUIRE = 0x11,
	NVME_CMD_IO_MGMT_RECV,
	NVME_CMD_RESV_RELEASE = 0x15,
	NVME_CMD_ZONE_MGMT_SEND	= 0x79,
	NVME_CMD_ZONE_MGMT_RESV	= 0x7A,
	NVME_CMD_ZONE_APPEND = 0x7D,
	NVME_CMD_VENDOR_START = 0x80,
};

enum nvme_admin_opcodes {
	NVME_ADMIN_CMD_DELETE_SQ = 0x0,
	NVME_ADMIN_CMD_CREATE_SQ,
	NVME_ADMIN_CMD_GET_LOG_PAGE,
	NVME_ADMIN_CMD_DELETE_CQ = 0x4,
	NVME_ADMIN_CMD_CREATE_CQ,
	NVME_ADMIN_CMD_IDENTIFY,
	NVME_ADMIN_CMD_ABORT_CMD = 0x08,
	NVME_ADMIN_CMD_SET_FEATURES,
	NVME_ADMIN_CMD_GET_FEATURES,
	NVME_ADMIN_CMD_ASYNC_EVENT = 0x0C,
	NVME_ADMIN_CMD_NS_MGMT,
	NVME_ADMIN_CMD_FIRMWARE_ACTIVATE = 0x10,
	NVME_ADMIN_CMD_FIRMWARE_DOWNLOAD,
	NVME_ADMIN_CMD_FORMAT_NVM = 0x80,
	NVME_ADMIN_CMD_SECURITY_SEND,
	NVME_ADMIN_CMD_SECURITY_RECV = 0x82,
	NVME_ADMIN_CMD_SANATIZE_NVM = 0x84
};

aos_bool nvme_init(struct AOS_Module* m) __attribute__((used));
void nvme_get_pcie(uint64_t cidx, pcie_device_t* out) __attribute__((used));
aos_bool nvme_read_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_write_blk(uint64_t cidx, int port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_flush(uint64_t cidx, int port_id) __attribute__((used));
aos_bool nvme_get_block_device(uint64_t cidx, int port_id, struct block_device* out) __attribute__((used));
void nvme_get_available_ports(uint64_t cidx, uint8_t* out, int out_size) __attribute__((used));