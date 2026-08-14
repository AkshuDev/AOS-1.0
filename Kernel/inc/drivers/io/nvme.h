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

#define NVME_CQE_PHASE(status) ((status) & 0x1U)
#define NVME_CQE_STATUS(status) ((status) >> 1)

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

#define NVME_IO_QUEUE_SIZE 64
struct nvme_io_queue {
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
    uint64_t cid_bitmap[NVME_IO_QUEUE_SIZE / 64];
};

struct nvme_identify_controller {
    uint16_t vid; // PCI Vendor ID
    uint16_t ssvid; // PCI SubSys Vendor ID
    char sn[20]; // Serial Number
    char mn[40]; // Model Number
    char fr[8]; // Firmware Revision
    uint8_t rab; // Recommended Arbitration Burst
    uint8_t ieee[3]; // IEEE OUI
    uint8_t cmic; // Controller Multi Path I/O and Namespace Sharing Capabilities
    uint8_t mdts; // Maximum Data Transfer Size
    uint16_t cntlid; // Controller ID
    uint32_t ver; // NVMe Version
    uint32_t rtd3r; // RTD3 Resume Latency
    uint32_t rtd3e; // RTD3 Entry Latency
    uint32_t oaes; // Optional Async Events Supported
    uint32_t ctratt; // Controller Attributes
    uint16_t rrls; // Read Recovery Levels Supported
    uint8_t reserved_066[9];
    uint8_t cntrltype; // Controller Type

    uint16_t fguid[8]; // Firmware GUID
    uint16_t crdt1; // Controller Ready Time 1
    uint16_t crdt2; // Controller Ready Time 2
    uint16_t crdt3; // Controller Ready Time 3
    uint8_t reserved_086[2];

    uint8_t nvmsr; // NVM Subsystem Reset
    uint8_t vwci; // VPD Write Cycle Information
    uint8_t mec; // Management Endpoint Capabilities
    uint8_t reserved_08B[5];

    uint16_t oacs; // Optional Admin Command Support
    uint8_t acl; // Abort Command Limit
    uint8_t aerl; // Async Event Request Limit
    uint8_t frmw; // Firmware Updates
    uint8_t lpa; // Log Page Attributes
    uint8_t elpe; // Error Log Page Entries
    uint8_t npss; // Number of Power States Supported
    uint8_t avscc; // Admin Vendor Specific Command Configuration
    uint8_t apsta; // Autonomous Power State Transition Attributes
    uint16_t wctemp; // Warning Composite Temperature Threshold
    uint16_t cctemp; // Critical Composite Temperature Threshold
    uint16_t mtfa; // Maximum Time for Firmware Activation

    uint32_t hmpre; // Host Memory Buffer Preferred Size
    uint32_t hmmin; // Host Memory Buffer Minimum Size
    uint8_t tnvmcap[16]; // Total NVM Capacity
    uint8_t unvmcap[16]; // Unallocated NVM Capacity

    uint32_t rpmbs; // Replay Protected Memory Block Support
    uint16_t edstt; // Extended Device Self-Test Time
    uint8_t dsto; // Device Self-Test Options
    uint8_t fwug; // Firmware Update Granularity
    uint16_t kas; // Keep Alive Support
    uint16_t hctma; // Host Controlled Thermal Management Attributes
    uint16_t mntmt; // Minimum Thermal Management Temperature
    uint16_t mxtmt; // Maximum Thermal Management Temperature

    uint32_t sanicap; // Sanitize Capabilities
    uint32_t hmminds; // Host Memory Buffer Minimum Descriptor Size
    uint16_t hmmaxd; // Host Memory Buffer Maximum Descriptors
    uint16_t nsetidmax; // NVM Set Identifier Maximum
    uint16_t endgidmax; // Endurance Group Identifier Maximum
    uint8_t anatt; // ANA Transition Time
    uint8_t anacap; // Asymmetric Namespace Access Capabilities
    uint32_t anagrpmax; // ANA Group Identifier Maximum
    uint32_t nanagrpid; // Number of ANA Group Identifiers
    uint32_t pels; // Persistent Event Log Size

    uint8_t  reserved_0F4[156];

    uint16_t sqes; // Submission Queue Entry Size
    uint16_t cqes; // Completion Queue Entry Size
    uint16_t maxcmd; // Maximum Outstanding Commands
    uint32_t nn; // Number of Namespaces
    uint16_t oncs; // Optional NVM Command Support
    uint16_t fuses; // Fused Operation Support
    uint8_t fna; // Format NVM Attributes
    uint8_t vwc; // Volatile Write Cache
    uint16_t awun; // Atomic Write Unit Normal
    uint16_t awupf; // Atomic Write Unit Power Fail
    uint8_t nvscc; // NVM Vendor Specific Command Configuration
    uint8_t nwpc; // Namespace Write Protection Capabilities
    uint16_t acwu; // Atomic Compare & Write Unit
    uint16_t copy; // Copy Formats
    uint8_t sgls[4]; // SGL Support
    uint32_t mnan; // Maximum Number of Allowed Namespaces

    uint8_t reserved_1B2[18];

    uint32_t maxdna; // Maximum DNA Transfer Size
    uint32_t maxcna; // Maximum CMB Allocation

    uint8_t reserved_1CC[8];

    uint32_t oacs2; // Optional Admin Command Support 2
    uint32_t hmbs; // Host Memory Buffer Size

    uint8_t reserved_1DC[36];

    struct {
        uint16_t mp; // Maximum Power
        uint8_t reserved_002;
        uint8_t flags;
        uint32_t enlat; // Entry Latency
        uint32_t exlat; // Exit Latency
        uint8_t rrt; // Relative Read Throughput
        uint8_t rrl; // Relative Read Latency
        uint8_t rwt; // Relative Write Throughput
        uint8_t rwl; // Relative Write Latency
        uint16_t idle_power;
        uint8_t idle_scale;
        uint8_t reserved_00F;
        uint16_t active_power;
        uint8_t active_workload;
        uint8_t active_scale;
        uint8_t reserved_014[2];
    } psd[32]; // Power State Descriptors

    uint8_t vendor_specific[1024];
};

struct nvme_lbaf {
    uint16_t ms; // Metadata Size
    uint8_t ds; // LBA Data Size: 2^DS bytes
    uint8_t rp; // Relative Performance
};

struct nvme_identify_namespace {
    uint64_t nsze; // Namespace Size
    uint64_t ncap; // Namespace Capacity
    uint64_t nuse; // Namespace Utilization

    uint8_t nsfeat; // Namespace Features
    uint8_t nlbaf; // Number of LBA Formats
    uint8_t flbas; // Formatted LBA Size
    uint8_t mc; // Metadata Capabilities
    uint8_t dpc; // End-to-End Data Protection Capabilities
    uint8_t dps; // End-to-End Data Protection Type Settings
    uint8_t nmic; // Namespace Multi-path I/O and Namespace Sharing Capabilities
    uint8_t rescap; // Reservation Capabilities

    uint8_t fpi; // Format Progress Indicator
    uint8_t dlfeat; // Deallocate Logical Block Features
    uint16_t nawun; // Namespace Atomic Write Unit Normal
    uint16_t nawupf; // Namespace Atomic Write Unit Power Fail
    uint16_t nacwu; // Namespace Atomic Compare & Write Unit

    uint16_t nabsn; // Namespace Atomic Boundary Size Normal
    uint16_t nabo; // Namespace Atomic Boundary Offset
    uint16_t nabspf; // Namespace Atomic Boundary Size Power Fail

    uint16_t noiob; // Namespace Optimal I/O Boundary

    uint8_t nvmcap[16]; // NVM Capacity

    uint16_t npwg; // Namespace Preferred Write Granularity
    uint16_t npwa; // Namespace Preferred Write Alignment
    uint16_t npdg; // Namespace Preferred Deallocate Granularity
    uint16_t npda; // Namespace Preferred Deallocate Alignment

    uint16_t nows; // Namespace Optimal Write Size

    uint8_t mssrl; // Maximum Single Source Range Length
    uint8_t mcl; // Maximum Copy Length
    uint8_t msrc; // Maximum Source Range Count

    uint8_t reserved_02b[9];

    uint32_t anagrpid; // ANA Group Identifier

    uint8_t reserved_038[3];

    uint8_t nsattr; // Namespace Attributes

    uint16_t nvmsetid; // NVM Set Identifier
    uint16_t endgid; // Endurance Group Identifier

    uint8_t nguid[16]; // Namespace Globally Unique Identifier
    uint8_t eui64[8]; // IEEE Extended Unique Identifier

    struct nvme_lbaf lbaf[64]; // LBA Format Support

    uint8_t reserved_168[PAGE_SIZE - 0x168];
};

typedef struct {
    aos_bool valid;
    uint32_t nsid;
	
    struct nvme_identify_namespace* identity;
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

enum nvme_identify_cns {
    NVME_IDENTIFY_CNS_NAMESPACE = 0x00,
    NVME_IDENTIFY_CNS_CONTROLLER,
    NVME_IDENTIFY_CNS_ACTIVE_NS_LIST,
    NVME_IDENTIFY_CNS_NS_ID_DESCRIPTOR,
    NVME_IDENTIFY_CNS_NVM_SET_LIST,
    NVME_IDENTIFY_CNS_NS_LIST = 0x10,
    NVME_IDENTIFY_CNS_CTRL_LIST,
    NVME_IDENTIFY_CNS_PRIMARY_CTRL_CAP,
    NVME_IDENTIFY_CNS_SECONDARY_CTRL_LIST,
    NVME_IDENTIFY_CNS_NS_GRANULARITY,
    NVME_IDENTIFY_CNS_UUID_LIST = 0x17,
    NVME_IDENTIFY_CNS_DOMAIN_LIST = 0x19,
    NVME_IDENTIFY_CNS_ENDURANCE_GROUP_LIST,
    NVME_IDENTIFY_CNS_ALLOCATED_NS_LIST,
    NVME_IDENTIFY_CNS_IOCS,
    NVME_IDENTIFY_CNS_ALLOCATED_NS,
    NVME_IDENTIFY_CNS_NS_ACTIVE_CTRL_LIST,
    NVME_IDENTIFY_CNS_NS_GRANULARITY_2,
    NVME_IDENTIFY_CNS_IOCS_NS,
};

enum nvme_status_code {
	NVME_SC_SUCCESS = 0x0,
	NVME_SC_INVALID_OPCODE,
	NVME_SC_INVALID_FIELD,
	NVME_SC_CMDID_CONFLICT,
	NVME_SC_DATA_XFER_ERROR,
	NVME_SC_POWER_LOSS,
	NVME_SC_INTERNAL,
	NVME_SC_ABORT_REQ,
	NVME_SC_ABORT_QUEUE,
	NVME_SC_FUSED_FAIL,
	NVME_SC_FUSED_MISSING,
	NVME_SC_INVALID_NS,
	NVME_SC_CMD_SEQ_ERROR,
	NVME_SC_SGL_INVALID_LAST,
	NVME_SC_SGL_INVALID_COUNT,
	NVME_SC_SGL_INVALID_DATA,
	NVME_SC_SGL_INVALID_METADATA,
	NVME_SC_SGL_INVALID_TYPE,
	NVME_SC_CMB_INVALID_USE,
	NVME_SC_PRP_INVALID_OFFSET,
	NVME_SC_ATOMIC_WU_EXCEEDED,
	NVME_SC_OP_DENIED,
	NVME_SC_SGL_INVALID_OFFSET,
	NVME_SC_RESERVED,
	NVME_SC_HOST_ID_INCONSIST,
	NVME_SC_KA_TIMEOUT_EXPIRED,
	NVME_SC_KA_TIMEOUT_INVALID,
	NVME_SC_ABORTED_PREEMPT_ABORT,
	NVME_SC_SANITIZE_FAILED,
	NVME_SC_SANITIZE_IN_PROGRESS,
	NVME_SC_SGL_INVALID_GRANULARITY,
	NVME_SC_CMD_NOT_SUP_CMB_QUEUE,
	NVME_SC_NS_WRITE_PROTECTED,
	NVME_SC_CMD_INTERRUPTED,
	NVME_SC_TRANSIENT_TR_ERR,
	NVME_SC_ADMIN_COMMAND_MEDIA_NOT_READY = 0x24,
	NVME_SC_INVALID_IO_CMD_SET,
	NVME_SC_LBA_RANGE = 0x80,
	NVME_SC_CAP_EXCEEDED,
	NVME_SC_NS_NOT_READY,
	NVME_SC_RESERVATION_CONFLICT,
	NVME_SC_FORMAT_IN_PROGRESS,

	NVME_SC_CQ_INVALID = 0x100,
	NVME_SC_QID_INVALID,
	NVME_SC_QUEUE_SIZE,
	NVME_SC_ABORT_LIMIT ,
	NVME_SC_ABORT_MISSING,
	NVME_SC_ASYNC_LIMIT,
	NVME_SC_FIRMWARE_SLOT,
	NVME_SC_FIRMWARE_IMAGE,
	NVME_SC_INVALID_VECTOR,
	NVME_SC_INVALID_LOG_PAGE,
	NVME_SC_INVALID_FORMAT,
	NVME_SC_FW_NEEDS_CONV_RESET,
	NVME_SC_INVALID_QUEUE,
	NVME_SC_FEATURE_NOT_SAVEABLE,
	NVME_SC_FEATURE_NOT_CHANGEABLE,
	NVME_SC_FEATURE_NOT_PER_NS,
	NVME_SC_FW_NEEDS_SUBSYS_RESET	= 0x110,
	NVME_SC_FW_NEEDS_RESET,
	NVME_SC_FW_NEEDS_MAX_TIME,
	NVME_SC_FW_ACTIVATE_PROHIBITED,
	NVME_SC_OVERLAPPING_RANGE,
	NVME_SC_NS_INSUFFICIENT_CAP,
	NVME_SC_NS_ID_UNAVAILABLE,
	NVME_SC_NS_ALREADY_ATTACHED	= 0x118,
	NVME_SC_NS_IS_PRIVATE,
	NVME_SC_NS_NOT_ATTACHED,
	NVME_SC_THIN_PROV_NOT_SUP,
	NVME_SC_CTRL_LIST_INVALID,
	NVME_SC_SELF_TEST_IN_PROGRESS,
	NVME_SC_BP_WRITE_PROHIBITED,
	NVME_SC_CTRL_ID_INVALID,
	NVME_SC_SEC_CTRL_STATE_INVALID,
	NVME_SC_CTRL_RES_NUM_INVALID,
	NVME_SC_RES_ID_INVALID,
	NVME_SC_PMR_SAN_PROHIBITED,
	NVME_SC_ANA_GROUP_ID_INVALID,
	NVME_SC_ANA_ATTACH_FAILED,

	NVME_SC_BAD_ATTRIBUTES = 0x180,
	NVME_SC_INVALID_PI,
	NVME_SC_READ_ONLY,
	NVME_SC_CMD_SIZE_LIM_EXCEEDED,

	NVME_SC_CONNECT_FORMAT = 0x180,
	NVME_SC_CONNECT_CTRL_BUSY,
	NVME_SC_CONNECT_INVALID_PARAM,
	NVME_SC_CONNECT_RESTART_DISC,
	NVME_SC_CONNECT_INVALID_HOST,

	NVME_SC_DISCOVERY_RESTART = 0x190,
	NVME_SC_AUTH_REQUIRED,

	NVME_SC_ZONE_BOUNDARY_ERROR	= 0x1B8,
	NVME_SC_ZONE_FULL,
	NVME_SC_ZONE_READ_ONLY,
	NVME_SC_ZONE_OFFLINE,
	NVME_SC_ZONE_INVALID_WRITE,
	NVME_SC_ZONE_TOO_MANY_ACTIVE,
	NVME_SC_ZONE_TOO_MANY_OPEN,
	NVME_SC_ZONE_INVALID_TRANSITION,

	NVME_SC_WRITE_FAULT = 0x280,
	NVME_SC_READ_ERROR,
	NVME_SC_GUARD_CHECK,
	NVME_SC_APPTAG_CHECK,
	NVME_SC_REFTAG_CHECK,
	NVME_SC_COMPARE_FAILED,
	NVME_SC_ACCESS_DENIED,
	NVME_SC_UNWRITTEN_BLOCK,

	NVME_SC_INTERNAL_PATH_ERROR	= 0x300,
	NVME_SC_ANA_PERSISTENT_LOSS,
	NVME_SC_ANA_INACCESSIBLE,
	NVME_SC_ANA_TRANSITION,
	NVME_SC_CTRL_PATH_ERROR	= 0x360,
	NVME_SC_HOST_PATH_ERROR	= 0x370,
	NVME_SC_HOST_ABORTED_CMD
};

enum nvme_cq_sq_flags {
	NVME_CQ_PC	= (1U << 0),
	NVME_CQ_IRQ_ENABLED	= (1U << 1),

	NVME_SQ_PRIO_URGENT	= (0U << 16),
	NVME_SQ_PRIO_HIGH = (1U << 16),
	NVME_SQ_PRIO_MEDIUM	= (2U << 16),
	NVME_SQ_PRIO_LOW = (3U << 16),
	NVME_SQ_PC = (1U << 18)
};

#define NVME_IO_QUEUE_ID 1

aos_bool nvme_init(struct AOS_Module* m) __attribute__((used));
aos_bool nvme_get_pcie(uint64_t cidx, pcie_device_t* out) __attribute__((used));
aos_bool nvme_read_blk(uint64_t cidx, uint64_t port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_write_blk(uint64_t cidx, uint64_t port_id, uint64_t lba, uint32_t count, void* buffer) __attribute__((used));
aos_bool nvme_flush(uint64_t cidx, uint64_t port_id) __attribute__((used));
aos_bool nvme_get_block_device(uint64_t cidx, uint64_t port_id, struct block_device* out) __attribute__((used));
aos_bool nvme_get_available_ports(uint64_t cidx, uint8_t* out, uint64_t out_size) __attribute__((used));