#pragma once

#include <aos_inttypes.h>
#include <asm.h>
#include <inc/core/pcie.h>
#include <inc/drivers/gpu/gpu.h>

// Credits: https://github.com/torvalds/linux/blob/master/include/uapi/linux/virtio_gpu.h (For the enum structure)
enum virtio_gpu_ctrl_type {
	VIRTIO_GPU_UNDEFINED = 0,

	/* 2d commands */
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
	VIRTIO_GPU_CMD_RESOURCE_UNREF,
	VIRTIO_GPU_CMD_SET_SCANOUT,
	VIRTIO_GPU_CMD_RESOURCE_FLUSH,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
	VIRTIO_GPU_CMD_GET_CAPSET_INFO,
	VIRTIO_GPU_CMD_GET_CAPSET,
	VIRTIO_GPU_CMD_GET_EDID,
	VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
	VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

	/* 3d commands */
	VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
	VIRTIO_GPU_CMD_CTX_DESTROY,
	VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
	VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
	VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
	VIRTIO_GPU_CMD_SUBMIT_3D,
	VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
	VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

	/* cursor commands */
	VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
	VIRTIO_GPU_CMD_MOVE_CURSOR,

	/* success responses */
	VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET,
	VIRTIO_GPU_RESP_OK_EDID,
	VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
	VIRTIO_GPU_RESP_OK_MAP_INFO,

	/* error responses */
	VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
	VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
	VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100

#define VIRTIO_FEATURE_MASK 0xFFFFFFFF

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

#define VIRTIO_GPU_QUEUE_INDEX 0

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01
#define VIRTIO_STATUS_DRIVER 0x02
#define VIRTIO_STATUS_DRIVER_OK 0x04
#define VIRTIO_STATUS_FEATURES_OK 0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED 0x80

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTQ_DESC_F_INDIRECT 4

#define VIRTIO_GPU_MAX_SCANOUTS 16

#define VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP (1 << 0)

#define VIRTIO_GPU_F_VIRGL 0
#define VIRTIO_F_VERSION_1 (1 << 5)

enum virtio_gpu_formats {
    VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM = 1,
    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM = 2,
    VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM = 3,
    VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM = 4,

    VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM = 67,
    VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM = 68,

    VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM = 121,
    VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM = 134,
};

struct virtio_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t ring_idx;
    uint8_t padding[3];
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one {
        struct virtio_rect r;
        uint32_t enabled;
        uint32_t flags;
    } displays[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

struct virtq_desc {
    uint64_t addr; // guest physical address
    uint32_t len; // length of the buffer
    uint16_t flags; // VIRTQ_DESC_F_NEXT, VIRTQ_DESC_F_WRITE
    uint16_t next; // index of next descriptor if F_NEXT
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
};

struct virtqueue {
    volatile struct virtq_desc* desc;
    volatile struct virtq_avail* avail;
    volatile struct virtq_used* used;
    uint16_t queue_size;
    uint16_t free_head;
    uint16_t last_used_idx;
};

struct virtio_gpu_resource_unref {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed));

struct virtio_gpu_resource_detach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((used));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t x, y, z;
    uint32_t w, h, d;
	uint64_t offset;
	uint32_t resource_id;
	uint32_t level;
	uint32_t stride;
	uint32_t layer_stride;
} __attribute__((packed));

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_cap {
    uint8_t bar;
    uint8_t cap_ptr;
    uint32_t offset;
    uint32_t length;
} __attribute__((packed));

struct virtio_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;

    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed));

struct virtio_gpu_ctx_create {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t nlen;
	uint32_t context_init;
	char debug_name[64];
} __attribute__((packed));

struct virtio_gpu_ctx_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
} __attribute__((packed));

struct virtio_gpu_ctx_resource {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_create_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t resource_id;
	uint32_t target;
	uint32_t format;
	uint32_t bind;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t array_size;
	uint32_t last_level;
	uint32_t nr_samples;
	uint32_t flags;
	uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_cmd_submit_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	uint32_t size;
	uint32_t padding;
} __attribute__((packed));

aos_bool virtio_init(struct AOS_Module* m) __attribute__((used));
aos_bool virtio_init_resources(struct gpu_device* gpu, int resource_id) __attribute__((used));
aos_bool virtio_flush(struct gpu_device* gpu, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int resource_id) __attribute__((used));
aos_bool virtio_set_mode(struct gpu_device* gpu, uint32_t w, uint32_t h, uint32_t bpp) __attribute__((used));
aos_bool virtio_switch_off(struct gpu_device* gpu) __attribute__((used));

// Virgl
#define VIRGL_CMD_HEADER(op, obj, len) ((op) | ((obj) << 8) | ((len) << 16))

#define VIRGL_MAX_COLOR_BUFS 8
#define VIRGL_MAX_CLIP_PLANES 8

#define VIRGL_OBJ_CREATE_HEADER 0
#define VIRGL_OBJ_CREATE_HANDLE 1
#define VIRGL_OBJ_BIND_HEADER 0
#define VIRGL_OBJ_BIND_HANDLE 1
#define VIRGL_OBJ_DESTROY_HANDLE 1

// Blend
#define VIRGL_OBJ_BLEND_SIZE (VIRGL_MAX_COLOR_BUFS + 3)
#define VIRGL_OBJ_BLEND_HANDLE 0
#define VIRGL_OBJ_BLEND_S0 1
#define VIRGL_OBJ_BLEND_S0_INDEPENDENT_BLEND_ENABLE(x) ((x) & 0x1 << 0)
#define VIRGL_OBJ_BLEND_S0_LOGICOP_ENABLE(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_BLEND_S0_DITHER(x) (((x) & 0x1) << 2)
#define VIRGL_OBJ_BLEND_S0_ALPHA_TO_COVERAGE(x) (((x) & 0x1) << 3)
#define VIRGL_OBJ_BLEND_S0_ALPHA_TO_ONE(x) (((x) & 0x1) << 4)
#define VIRGL_OBJ_BLEND_S1 2
#define VIRGL_OBJ_BLEND_S1_LOGICOP_FUNC(x) (((x) & 0xf) << 0)

#define VIRGL_OBJ_BLEND_S2(cbuf) (3 + (cbuf))
#define VIRGL_OBJ_BLEND_S2_RT_BLEND_ENABLE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_FUNC(x) (((x) & 0x7) << 1)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_SRC_FACTOR(x) (((x) & 0x1f) << 4)
#define VIRGL_OBJ_BLEND_S2_RT_RGB_DST_FACTOR(x) (((x) & 0x1f) << 9)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_FUNC(x) (((x) & 0x7) << 14)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_SRC_FACTOR(x) (((x) & 0x1f) << 17)
#define VIRGL_OBJ_BLEND_S2_RT_ALPHA_DST_FACTOR(x) (((x) & 0x1f) << 22)
#define VIRGL_OBJ_BLEND_S2_RT_COLORMASK(x) (((x) & 0xf) << 27)

// DSA
#define VIRGL_OBJ_DSA_SIZE 5
#define VIRGL_OBJ_DSA_HANDLE 0
#define VIRGL_OBJ_DSA_S0 1
#define VIRGL_OBJ_DSA_S0_DEPTH_ENABLE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_DSA_S0_DEPTH_WRITEMASK(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_DSA_S0_DEPTH_FUNC(x) (((x) & 0x7) << 2)
#define VIRGL_OBJ_DSA_S0_ALPHA_ENABLED(x) (((x) & 0x1) << 8)
#define VIRGL_OBJ_DSA_S0_ALPHA_FUNC(x) (((x) & 0x7) << 9)
#define VIRGL_OBJ_DSA_S1 2
#define VIRGL_OBJ_DSA_S2 3
#define VIRGL_OBJ_DSA_S1_STENCIL_ENABLED(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_DSA_S1_STENCIL_FUNC(x) (((x) & 0x7) << 1)
#define VIRGL_OBJ_DSA_S1_STENCIL_FAIL_OP(x) (((x) & 0x7) << 4)
#define VIRGL_OBJ_DSA_S1_STENCIL_ZPASS_OP(x) (((x) & 0x7) << 7)
#define VIRGL_OBJ_DSA_S1_STENCIL_ZFAIL_OP(x) (((x) & 0x7) << 10)
#define VIRGL_OBJ_DSA_S1_STENCIL_VALUEMASK(x) (((x) & 0xff) << 13)
#define VIRGL_OBJ_DSA_S1_STENCIL_WRITEMASK(x) (((x) & 0xff) << 21)
#define VIRGL_OBJ_DSA_ALPHA_REF 4

// Rasterizer
#define VIRGL_OBJ_RS_SIZE 9
#define VIRGL_OBJ_RS_HANDLE 0
#define VIRGL_OBJ_RS_S0 1
#define VIRGL_OBJ_RS_S0_FLATSHADE(x) (((x) & 0x1) << 0)
#define VIRGL_OBJ_RS_S0_DEPTH_CLIP(x) (((x) & 0x1) << 1)
#define VIRGL_OBJ_RS_S0_CLIP_HALFZ(x) (((x) & 0x1) << 2)
#define VIRGL_OBJ_RS_S0_RASTERIZER_DISCARD(x) (((x) & 0x1) << 3)
#define VIRGL_OBJ_RS_S0_FLATSHADE_FIRST(x) (((x) & 0x1) << 4)
#define VIRGL_OBJ_RS_S0_LIGHT_TWOSIZE(x) (((x) & 0x1) << 5)
#define VIRGL_OBJ_RS_S0_SPRITE_COORD_MODE(x) (((x) & 0x1) << 6)
#define VIRGL_OBJ_RS_S0_POINT_QUAD_RASTERIZATION(x) (((x) & 0x1) << 7)
#define VIRGL_OBJ_RS_S0_CULL_FACE(x) (((x) & 0x3) << 8)
#define VIRGL_OBJ_RS_S0_FILL_FRONT(x) (((x) & 0x3) << 10)
#define VIRGL_OBJ_RS_S0_FILL_BACK(x) (((x) & 0x3) << 12)
#define VIRGL_OBJ_RS_S0_SCISSOR(x) (((x) & 0x1) << 14)
#define VIRGL_OBJ_RS_S0_FRONT_CCW(x) (((x) & 0x1) << 15)
#define VIRGL_OBJ_RS_S0_CLAMP_VERTEX_COLOR(x) (((x) & 0x1) << 16)
#define VIRGL_OBJ_RS_S0_CLAMP_FRAGMENT_COLOR(x) (((x) & 0x1) << 17)
#define VIRGL_OBJ_RS_S0_OFFSET_LINE(x) (((x) & 0x1) << 18)
#define VIRGL_OBJ_RS_S0_OFFSET_POINT(x) (((x) & 0x1) << 19)
#define VIRGL_OBJ_RS_S0_OFFSET_TRI(x) (((x) & 0x1) << 20)
#define VIRGL_OBJ_RS_S0_POLY_SMOOTH(x) (((x) & 0x1) << 21)
#define VIRGL_OBJ_RS_S0_POLY_STIPPLE_ENABLE(x) (((x) & 0x1) << 22)
#define VIRGL_OBJ_RS_S0_POINT_SMOOTH(x) (((x) & 0x1) << 23)
#define VIRGL_OBJ_RS_S0_POINT_SIZE_PER_VERTEX(x) (((x) & 0x1) << 24)
#define VIRGL_OBJ_RS_S0_MULTISAMPLE(x) (((x) & 0x1) << 25)
#define VIRGL_OBJ_RS_S0_LINE_SMOOTH(x) (((x) & 0x1) << 26)
#define VIRGL_OBJ_RS_S0_LINE_STIPPLE_ENABLE(x) (((x) & 0x1) << 27)
#define VIRGL_OBJ_RS_S0_LINE_LAST_PIXEL(x) (((x) & 0x1) << 28)
#define VIRGL_OBJ_RS_S0_HALF_PIXEL_CENTER(x) (((x) & 0x1) << 29)
#define VIRGL_OBJ_RS_S0_BOTTOM_EDGE_RULE(x) (((x) & 0x1) << 30)
#define VIRGL_OBJ_RS_S0_FORCE_PERSAMPLE_INTERP(x) (((x) & 0x1) << 31)

#define VIRGL_OBJ_RS_POINT_SIZE 2
#define VIRGL_OBJ_RS_SPRITE_COORD_ENABLE 3
#define VIRGL_OBJ_RS_S3 4

#define VIRGL_OBJ_RS_S3_LINE_STIPPLE_PATTERN(x) (((x) & 0xffff) << 0)
#define VIRGL_OBJ_RS_S3_LINE_STIPPLE_FACTOR(x) (((x) & 0xff) << 16)
#define VIRGL_OBJ_RS_S3_CLIP_PLANE_ENABLE(x) (((x) & 0xff) << 24)
#define VIRGL_OBJ_RS_LINE_WIDTH 5
#define VIRGL_OBJ_RS_OFFSET_UNITS 6
#define VIRGL_OBJ_RS_OFFSET_SCALE 7
#define VIRGL_OBJ_RS_OFFSET_CLAMP 8

#define VIRGL_OBJ_CLEAR_SIZE 8
#define VIRGL_OBJ_CLEAR_BUFFERS 0
#define VIRGL_OBJ_CLEAR_COLOR_0 1 
#define VIRGL_OBJ_CLEAR_COLOR_1 2
#define VIRGL_OBJ_CLEAR_COLOR_2 3
#define VIRGL_OBJ_CLEAR_COLOR_3 4
#define VIRGL_OBJ_CLEAR_DEPTH_0 5
#define VIRGL_OBJ_CLEAR_DEPTH_1 6
#define VIRGL_OBJ_CLEAR_STENCIL 7

// Shader
#define VIRGL_OBJ_SHADER_HDR_SIZE(nso) (5 + ((nso) ? (2 * nso) + 4 : 0))
#define VIRGL_OBJ_SHADER_HANDLE 0
#define VIRGL_OBJ_SHADER_TYPE 1
#define VIRGL_OBJ_SHADER_OFFSET 2
#define VIRGL_OBJ_SHADER_OFFSET_VAL(x) (((x) & 0x7fffffff) << 0)

#define VIRGL_OBJ_SHADER_OFFSET_CONT (0x1u << 31)
#define VIRGL_OBJ_SHADER_NUM_TOKENS 3
#define VIRGL_OBJ_SHADER_SO_NUM_OUTPUTS 4
#define VIRGL_OBJ_SHADER_SO_STRIDE(x) (5 + (x))
#define VIRGL_OBJ_SHADER_SO_OUTPUT0(x) (9 + (x * 2))
#define VIRGL_OBJ_SHADER_SO_OUTPUT_REGISTER_INDEX(x) (((x) & 0xff) << 0)
#define VIRGL_OBJ_SHADER_SO_OUTPUT_START_COMPONENT(x) (((x) & 0x3) << 8)
#define VIRGL_OBJ_SHADER_SO_OUTPUT_NUM_COMPONENTS(x) (((x) & 0x7) << 10)
#define VIRGL_OBJ_SHADER_SO_OUTPUT_BUFFER(x) (((x) & 0x7) << 13)
#define VIRGL_OBJ_SHADER_SO_OUTPUT_DST_OFFSET(x) (((x) & 0xffff) << 16)
#define VIRGL_OBJ_SHADER_SO_OUTPUT0_SO(x) (10 + (x * 2))
#define VIRGL_OBJ_SHADER_SO_OUTPUT_STREAM(x) (((x) & 0x03) << 0)

// Viewport
#define VIRGL_SET_VIEWPORT_STATE_SIZE(num_viewports) ((6 * num_viewports) + 1)
#define VIRGL_SET_VIEWPORT_START_SLOT 0
#define VIRGL_SET_VIEWPORT_STATE_SCALE_0(x) (1 + (x * 6))
#define VIRGL_SET_VIEWPORT_STATE_SCALE_1(x) (2 + (x * 6))
#define VIRGL_SET_VIEWPORT_STATE_SCALE_2(x) (3 + (x * 6))
#define VIRGL_SET_VIEWPORT_STATE_TRANSLATE_0(x) (4 + (x * 6))
#define VIRGL_SET_VIEWPORT_STATE_TRANSLATE_1(x) (5 + (x * 6))
#define VIRGL_SET_VIEWPORT_STATE_TRANSLATE_2(x) (6 + (x * 6))

// Framebuffer
#define VIRGL_SET_FRAMEBUFFER_STATE_SIZE(nr_cbufs) (nr_cbufs + 2)
#define VIRGL_SET_FRAMEBUFFER_STATE_NR_CBUFS 0
#define VIRGL_SET_FRAMEBUFFER_STATE_NR_ZSURF_HANDLE 1
#define VIRGL_SET_FRAMEBUFFER_STATE_CBUF_HANDLE(x) ((x) + 2)

// Vertex Elements
#define VIRGL_OBJ_VERTEX_ELEMENTS_SIZE(num_elements) (((num_elements) * 4) + 1)
#define VIRGL_OBJ_VERTEX_ELEMENTS_HANDLE 0
#define VIRGL_OBJ_VERTEX_ELEMENTS_V0_SRC_OFFSET(x) (((x) * 4) + 1)
#define VIRGL_OBJ_VERTEX_ELEMENTS_V0_INSTANCE_DIVISOR(x) (((x) * 4) + 2)
#define VIRGL_OBJ_VERTEX_ELEMENTS_V0_VERTEX_BUFFER_INDEX(x) (((x) * 4) + 3)
#define VIRGL_OBJ_VERTEX_ELEMENTS_V0_SRC_FORMAT(x) (((x) * 4) + 4)

// Vertex Buffers
#define VIRGL_SET_VERTEX_BUFFERS_SIZE(num_buffers) ((num_buffers) * 3)
#define VIRGL_SET_VERTEX_BUFFER_STRIDE(x) (((x) * 3) + 0)
#define VIRGL_SET_VERTEX_BUFFER_OFFSET(x) (((x) * 3) + 1)
#define VIRGL_SET_VERTEX_BUFFER_HANDLE(x) (((x) * 3) + 2)

// Index Buffer
#define VIRGL_SET_INDEX_BUFFER_SIZE(ib) (((ib) ? 2 : 0) + 1)
#define VIRGL_SET_INDEX_BUFFER_HANDLE 0
#define VIRGL_SET_INDEX_BUFFER_INDEX_SIZE 1
#define VIRGL_SET_INDEX_BUFFER_OFFSET 2

// Constant Buffer
#define VIRGL_SET_CONSTANT_BUFFER_SHADER_TYPE 0
#define VIRGL_SET_CONSTANT_BUFFER_INDEX 1
#define VIRGL_SET_CONSTANT_BUFFER_DATA_START 2

#define VIRGL_SET_UNIFORM_BUFFER_SIZE 5
#define VIRGL_SET_UNIFORM_BUFFER_SHADER_TYPE 0
#define VIRGL_SET_UNIFORM_BUFFER_INDEX 1
#define VIRGL_SET_UNIFORM_BUFFER_OFFSET 2
#define VIRGL_SET_UNIFORM_BUFFER_LENGTH 3
#define VIRGL_SET_UNIFORM_BUFFER_RES_HANDLE 4

// Draw VBO
#define VIRGL_DRAW_VBO_SIZE 12
#define VIRGL_DRAW_VBO_SIZE_TESS 14
#define VIRGL_DRAW_VBO_SIZE_INDIRECT 20
#define VIRGL_DRAW_VBO_START 0
#define VIRGL_DRAW_VBO_COUNT 1
#define VIRGL_DRAW_VBO_MODE 2
#define VIRGL_DRAW_VBO_INDEXED 3
#define VIRGL_DRAW_VBO_INSTANCE_COUNT 4
#define VIRGL_DRAW_VBO_INDEX_BIAS 5
#define VIRGL_DRAW_VBO_START_INSTANCE 6
#define VIRGL_DRAW_VBO_PRIMITIVE_RESTART 7
#define VIRGL_DRAW_VBO_RESTART_INDEX 8
#define VIRGL_DRAW_VBO_MIN_INDEX 9
#define VIRGL_DRAW_VBO_MAX_INDEX 10
#define VIRGL_DRAW_VBO_COUNT_FROM_SO 11

#define VIRGL_DRAW_VBO_VERTICES_PER_PATCH 12
#define VIRGL_DRAW_VBO_DRAWID 13

#define VIRGL_DRAW_VBO_INDIRECT_HANDLE 14
#define VIRGL_DRAW_VBO_INDIRECT_OFFSET 15
#define VIRGL_DRAW_VBO_INDIRECT_STRIDE 16
#define VIRGL_DRAW_VBO_INDIRECT_DRAW_COUNT 17
#define VIRGL_DRAW_VBO_INDIRECT_DRAW_COUNT_OFFSET 18
#define VIRGL_DRAW_VBO_INDIRECT_DRAW_COUNT_HANDLE 19

// Create Surface
#define VIRGL_OBJ_SURFACE_SIZE 5
#define VIRGL_OBJ_SURFACE_HANDLE 0
#define VIRGL_OBJ_SURFACE_RES_HANDLE 1
#define VIRGL_OBJ_SURFACE_FORMAT 2
#define VIRGL_OBJ_SURFACE_BUFFER_FIRST_ELEMENT 3
#define VIRGL_OBJ_SURFACE_BUFFER_LAST_ELEMENT 4
#define VIRGL_OBJ_SURFACE_TEXTURE_LEVEL 3
#define VIRGL_OBJ_SURFACE_TEXTURE_LAYERS 4

#define VIRGL_OBJ_MSAA_SURFACE_SIZE (VIRGL_OBJ_SURFACE_SIZE + 1)
#define VIRGL_OBJ_SURFACE_SAMPLE_COUNT 5

// Create Streamout Target
#define VIRGL_OBJ_STREAMOUT_SIZE 4
#define VIRGL_OBJ_STREAMOUT_HANDLE 0
#define VIRGL_OBJ_STREAMOUT_RES_HANDLE 1
#define VIRGL_OBJ_STREAMOUT_BUFFER_OFFSET 2
#define VIRGL_OBJ_STREAMOUT_BUFFER_SIZE 3

// Sampler State
#define VIRGL_OBJ_SAMPLER_STATE_SIZE 9
#define VIRGL_OBJ_SAMPLER_STATE_HANDLE 0
#define VIRGL_OBJ_SAMPLER_STATE_S0 1
#define VIRGL_OBJ_SAMPLE_STATE_S0_WRAP_S(x) (((x) & 0x7) << 0)
#define VIRGL_OBJ_SAMPLE_STATE_S0_WRAP_T(x) (((x) & 0x7) << 3)
#define VIRGL_OBJ_SAMPLE_STATE_S0_WRAP_R(x) (((x) & 0x7) << 6)
#define VIRGL_OBJ_SAMPLE_STATE_S0_MIN_IMG_FILTER(x) (((x) & 0x3) << 9)
#define VIRGL_OBJ_SAMPLE_STATE_S0_MIN_MIP_FILTER(x) (((x) & 0x3) << 11)
#define VIRGL_OBJ_SAMPLE_STATE_S0_MAG_IMG_FILTER(x) (((x) & 0x3) << 13)
#define VIRGL_OBJ_SAMPLE_STATE_S0_COMPARE_MODE(x) (((x) & 0x1) << 15)
#define VIRGL_OBJ_SAMPLE_STATE_S0_COMPARE_FUNC(x) (((x) & 0x7) << 16)
#define VIRGL_OBJ_SAMPLE_STATE_S0_SEAMLESS_CUBE_MAP(x) (((x) & 0x1) << 19)
#define VIRGL_OBJ_SAMPLE_STATE_MAX_ANISOTROPY (((x & 0x3f)) << 20)

#define VIRGL_OBJ_SAMPLER_STATE_LOD_BIAS 2
#define VIRGL_OBJ_SAMPLER_STATE_MIN_LOD 3
#define VIRGL_OBJ_SAMPLER_STATE_MAX_LOD 4
#define VIRGL_OBJ_SAMPLER_STATE_BORDER_COLOR(x) ((x) + 5) /* 5 - 8 */

// Sampler View
#define VIRGL_OBJ_SAMPLER_VIEW_SIZE 6
#define VIRGL_OBJ_SAMPLER_VIEW_HANDLE 0
#define VIRGL_OBJ_SAMPLER_VIEW_RES_HANDLE 1
#define VIRGL_OBJ_SAMPLER_VIEW_FORMAT 2
#define VIRGL_OBJ_SAMPLER_VIEW_BUFFER_FIRST_ELEMENT 3
#define VIRGL_OBJ_SAMPLER_VIEW_BUFFER_LAST_ELEMENT 4
#define VIRGL_OBJ_SAMPLER_VIEW_TEXTURE_LAYER 3
#define VIRGL_OBJ_SAMPLER_VIEW_TEXTURE_LEVEL 4
#define VIRGL_OBJ_SAMPLER_VIEW_SWIZZLE 5
#define VIRGL_OBJ_SAMPLER_VIEW_SWIZZLE_R(x) (((x) & 0x7) << 0)
#define VIRGL_OBJ_SAMPLER_VIEW_SWIZZLE_G(x) (((x) & 0x7) << 3)
#define VIRGL_OBJ_SAMPLER_VIEW_SWIZZLE_B(x) (((x) & 0x7) << 6)
#define VIRGL_OBJ_SAMPLER_VIEW_SWIZZLE_A(x) (((x) & 0x7) << 9)

// Set Sampler Views
#define VIRGL_SET_SAMPLER_VIEWS_SIZE(num_views) ((num_views) + 2)
#define VIRGL_SET_SAMPLER_VIEWS_SHADER_TYPE 0
#define VIRGL_SET_SAMPLER_VIEWS_START_SLOT 1
#define VIRGL_SET_SAMPLER_VIEWS_V0_HANDLE 2

// Bind Sampler States
#define VIRGL_BIND_SAMPLER_STATES(num_states) ((num_states) + 2)
#define VIRGL_BIND_SAMPLER_STATES_SHADER_TYPE 0
#define VIRGL_BIND_SAMPLER_STATES_START_SLOT 1
#define VIRGL_BIND_SAMPLER_STATES_S0_HANDLE 2

// Set Stencil Reference
#define VIRGL_SET_STENCIL_REF_SIZE 1
#define VIRGL_SET_STENCIL_REF 0
#define VIRGL_STENCIL_REF_VAL(f, s) ((f & 0xff) | (((s & 0xff) << 8)))

// Set Blend Color
#define VIRGL_SET_BLEND_COLOR_SIZE 4
#define VIRGL_SET_BLEND_COLOR(x) (x)

// Set Scissor State
#define VIRGL_SET_SCISSOR_STATE_SIZE(x) (1 + 2 * x)
#define VIRGL_SET_SCISSOR_START_SLOT 0
#define VIRGL_SET_SCISSOR_MINX_MINY(x) (1 + (x * 2))
#define VIRGL_SET_SCISSOR_MAXX_MAXY(x) (2 + (x * 2))

// Resource Copy Region
#define VIRGL_CMD_RESOURCE_COPY_REGION_SIZE 13
#define VIRGL_CMD_RCR_DST_RES_HANDLE 0
#define VIRGL_CMD_RCR_DST_LEVEL 1
#define VIRGL_CMD_RCR_DST_X 2
#define VIRGL_CMD_RCR_DST_Y 3
#define VIRGL_CMD_RCR_DST_Z 4
#define VIRGL_CMD_RCR_SRC_RES_HANDLE 5
#define VIRGL_CMD_RCR_SRC_LEVEL 6
#define VIRGL_CMD_RCR_SRC_X 7
#define VIRGL_CMD_RCR_SRC_Y 8
#define VIRGL_CMD_RCR_SRC_Z 9
#define VIRGL_CMD_RCR_SRC_W 10
#define VIRGL_CMD_RCR_SRC_H 11
#define VIRGL_CMD_RCR_SRC_D 12

// Blit
#define VIRGL_CMD_BLIT_SIZE 21
#define VIRGL_CMD_BLIT_S0 0
#define VIRGL_CMD_BLIT_S0_MASK(x) (((x) & 0xff) << 0)
#define VIRGL_CMD_BLIT_S0_FILTER(x) (((x) & 0x3) << 8)
#define VIRGL_CMD_BLIT_S0_SCISSOR_ENABLE(x) (((x) & 0x1) << 10)
#define VIRGL_CMD_BLIT_S0_RENDER_CONDITION_ENABLE(x) (((x) & 0x1) << 11)
#define VIRGL_CMD_BLIT_S0_ALPHA_BLEND(x) (((x) & 0x1) << 12)
#define VIRGL_CMD_BLIT_SCISSOR_MINX_MINY 1
#define VIRGL_CMD_BLIT_SCISSOR_MAXX_MAXY 2
#define VIRGL_CMD_BLIT_DST_RES_HANDLE 3
#define VIRGL_CMD_BLIT_DST_LEVEL 4
#define VIRGL_CMD_BLIT_DST_FORMAT 5
#define VIRGL_CMD_BLIT_DST_X 6
#define VIRGL_CMD_BLIT_DST_Y 7
#define VIRGL_CMD_BLIT_DST_Z 8
#define VIRGL_CMD_BLIT_DST_W 9
#define VIRGL_CMD_BLIT_DST_H 10
#define VIRGL_CMD_BLIT_DST_D 11
#define VIRGL_CMD_BLIT_SRC_RES_HANDLE 12
#define VIRGL_CMD_BLIT_SRC_LEVEL 13
#define VIRGL_CMD_BLIT_SRC_FORMAT 14
#define VIRGL_CMD_BLIT_SRC_X 15
#define VIRGL_CMD_BLIT_SRC_Y 16
#define VIRGL_CMD_BLIT_SRC_Z 17
#define VIRGL_CMD_BLIT_SRC_W 18
#define VIRGL_CMD_BLIT_SRC_H 19
#define VIRGL_CMD_BLIT_SRC_D 20

// Query Object
#define VIRGL_OBJ_QUERY_SIZE 4
#define VIRGL_OBJ_QUERY_HANDLE 0
#define VIRGL_OBJ_QUERY_TYPE_INDEX 1
#define VIRGL_OBJ_QUERY_TYPE(x) (x & 0xffff)
#define VIRGL_OBJ_QUERY_INDEX(x) ((x & 0xffff) << 16)
#define VIRGL_OBJ_QUERY_OFFSET 2
#define VIRGL_OBJ_QUERY_RES_HANDLE 3

#define VIRGL_QUERY_BEGIN_HANDLE 0
#define VIRGL_QUERY_END_HANDLE 0

#define VIRGL_QUERY_RESULT_SIZE 2
#define VIRGL_QUERY_RESULT_HANDLE 0
#define VIRGL_QUERY_RESULT_WAIT 1

// Render Condition
#define VIRGL_RENDER_CONDITION_SIZE 3
#define VIRGL_RENDER_CONDITION_HANDLE 0
#define VIRGL_RENDER_CONDITION_CONDITION 1
#define VIRGL_RENDER_CONDITION_MODE 2

// Resource Inline Write
#define VIRGL_RESOURCE_IW_RES_HANDLE 0
#define VIRGL_RESOURCE_IW_LEVEL 1
#define VIRGL_RESOURCE_IW_USAGE 2
#define VIRGL_RESOURCE_IW_STRIDE 3
#define VIRGL_RESOURCE_IW_LAYER_STRIDE 4
#define VIRGL_RESOURCE_IW_X 5
#define VIRGL_RESOURCE_IW_Y 6
#define VIRGL_RESOURCE_IW_Z 7
#define VIRGL_RESOURCE_IW_W 8
#define VIRGL_RESOURCE_IW_H 9
#define VIRGL_RESOURCE_IW_D 10
#define VIRGL_RESOURCE_IW_DATA_START 11

// Set Streamout Targets
#define VIRGL_SET_STREAMOUT_TARGETS_APPEND_BITMASK 0
#define VIRGL_SET_STREAMOUT_TARGETS_H0 1

// Set Sample Mask
#define VIRGL_SET_SAMPLE_MASK_SIZE 1
#define VIRGL_SET_SAMPLE_MASK_MASK 0

// Set Clip State
#define VIRGL_SET_CLIP_STATE_SIZE 32
#define VIRGL_SET_CLIP_STATE_C0 0

// Polygon Stipple
#define VIRGL_POLYGON_STIPPLE_SIZE 32
#define VIRGL_POLYGON_STIPPLE_P0 0

#define VIRGL_BIND_SHADER_SIZE 2
#define VIRGL_BIND_SHADER_HANDLE 0
#define VIRGL_BIND_SHADER_TYPE 1

// TESS State
#define VIRGL_TESS_STATE_SIZE 6

// Set Min Samples
#define VIRGL_SET_MIN_SAMPLES_SIZE 1
#define VIRGL_SET_MIN_SAMPLES_MASK 0

// Set Shader Buffers
#define VIRGL_SET_SHADER_BUFFER_ELEMENT_SIZE 3
#define VIRGL_SET_SHADER_BUFFER_SIZE(x) (VIRGL_SET_SHADER_BUFFER_ELEMENT_SIZE * (x)) + 2
#define VIRGL_SET_SHADER_BUFFER_SHADER_TYPE 0
#define VIRGL_SET_SHADER_BUFFER_START_SLOT 1
#define VIRGL_SET_SHADER_BUFFER_OFFSET(x) ((x) * VIRGL_SET_SHADER_BUFFER_ELEMENT_SIZE + 2)
#define VIRGL_SET_SHADER_BUFFER_LENGTH(x) ((x) * VIRGL_SET_SHADER_BUFFER_ELEMENT_SIZE + 3)
#define VIRGL_SET_SHADER_BUFFER_RES_HANDLE(x) ((x) * VIRGL_SET_SHADER_BUFFER_ELEMENT_SIZE + 4)

// Set Shader Images
#define VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE 5
#define VIRGL_SET_SHADER_IMAGE_SIZE(x) (VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE * (x)) + 2
#define VIRGL_SET_SHADER_IMAGE_SHADER_TYPE 0
#define VIRGL_SET_SHADER_IMAGE_START_SLOT 1
#define VIRGL_SET_SHADER_IMAGE_FORMAT(x) ((x) * VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE + 2)
#define VIRGL_SET_SHADER_IMAGE_ACCESS(x) ((x) * VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE + 3)
#define VIRGL_SET_SHADER_IMAGE_LAYER_OFFSET(x) ((x) * VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE + 4)
#define VIRGL_SET_SHADER_IMAGE_LEVEL_SIZE(x) ((x) * VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE + 5)
#define VIRGL_SET_SHADER_IMAGE_RES_HANDLE(x) ((x) * VIRGL_SET_SHADER_IMAGE_ELEMENT_SIZE + 6)

// Memory Barrier
#define VIRGL_MEMORY_BARRIER_SIZE 1
#define VIRGL_MEMORY_BARRIER_FLAGS 0

// Launch Grid
#define VIRGL_LAUNCH_GRID_SIZE 8
#define VIRGL_LAUNCH_BLOCK_X 0
#define VIRGL_LAUNCH_BLOCK_Y 1
#define VIRGL_LAUNCH_BLOCK_Z 2
#define VIRGL_LAUNCH_GRID_X 3
#define VIRGL_LAUNCH_GRID_Y 4
#define VIRGL_LAUNCH_GRID_Z 5
#define VIRGL_LAUNCH_INDIRECT_HANDLE 6
#define VIRGL_LAUNCH_INDIRECT_OFFSET 7

// FB State No Attachment
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_SIZE 2
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_WIDTH_HEIGHT 0
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_WIDTH(x) (x & 0xffff)
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_HEIGHT(x) ((x >> 16) & 0xffff)
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_LAYERS_SAMPLES 1
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_LAYERS(x) (x & 0xffff)
#define VIRGL_SET_FRAMEBUFFER_STATE_NO_ATTACH_SAMPLES(x) ((x >> 16) & 0xff)

// Texture Barrier
#define VIRGL_TEXTURE_BARRIER_SIZE 1
#define VIRGL_TEXTURE_BARRIER_FLAGS 0

// Atomics (HW)
#define VIRGL_SET_ATOMIC_BUFFER_ELEMENT_SIZE 3
#define VIRGL_SET_ATOMIC_BUFFER_SIZE(x) (VIRGL_SET_ATOMIC_BUFFER_ELEMENT_SIZE * (x)) + 1
#define VIRGL_SET_ATOMIC_BUFFER_START_SLOT 0
#define VIRGL_SET_ATOMIC_BUFFER_OFFSET(x) ((x) * VIRGL_SET_ATOMIC_BUFFER_ELEMENT_SIZE + 1)
#define VIRGL_SET_ATOMIC_BUFFER_LENGTH(x) ((x) * VIRGL_SET_ATOMIC_BUFFER_ELEMENT_SIZE + 2)
#define VIRGL_SET_ATOMIC_BUFFER_RES_HANDLE(x) ((x) * VIRGL_SET_ATOMIC_BUFFER_ELEMENT_SIZE + 3)

// Set Debug Flags
#define VIRGL_SET_DEBUG_FLAGS_MIN_SIZE 2
#define VIRGL_SET_DEBUG_FLAGSTRING_OFFSET 0

// Query Buffer Object
#define VIRGL_QUERY_RESULT_QBO_SIZE 6
#define VIRGL_QUERY_RESULT_QBO_HANDLE 0
#define VIRGL_QUERY_RESULT_QBO_QBO_HANDLE 1
#define VIRGL_QUERY_RESULT_QBO_WAIT 2
#define VIRGL_QUERY_RESULT_QBO_RESULT_TYPE 3
#define VIRGL_QUERY_RESULT_QBO_OFFSET 4
#define VIRGL_QUERY_RESULT_QBO_INDEX 5

#define VIRGL_TRANSFER_TO_HOST 1
#define VIRGL_TRANSFER_FROM_HOST 2

// Transfer
#define VIRGL_TRANSFER3D_SIZE 13
#define VIRGL_TRANSFER3D_DATA_OFFSET 11
#define VIRGL_TRANSFER3D_DIRECTION 12

// Copy Transfer
#define VIRGL_COPY_TRANSFER3D_SIZE 14
#define VIRGL_COPY_TRANSFER3D_SRC_RES_HANDLE 11
#define VIRGL_COPY_TRANSFER3D_SRC_RES_OFFSET 12
#define VIRGL_COPY_TRANSFER3D_SYNCHRONIZED 13

// Set Tweak Flags
#define VIRGL_SET_TWEAKS_SIZE 2
#define VIRGL_SET_TWEAKS_ID 0
#define VIRGL_SET_TWEAKS_VALUE 1

// Clear Texture
#define VIRGL_CLEAR_TEXTURE_SIZE 12
#define VIRGL_TEXTURE_HANDLE 0
#define VIRGL_TEXTURE_LEVEL 1
#define VIRGL_TEXTURE_SRC_X 2
#define VIRGL_TEXTURE_SRC_Y 3
#define VIRGL_TEXTURE_SRC_Z 4
#define VIRGL_TEXTURE_SRC_W 5
#define VIRGL_TEXTURE_SRC_H 6
#define VIRGL_TEXTURE_SRC_D 7
#define VIRGL_TEXTURE_ARRAY_A 8
#define VIRGL_TEXTURE_ARRAY_B 9
#define VIRGL_TEXTURE_ARRAY_C 10
#define VIRGL_TEXTURE_ARRAY_D 11

// VirGL Create
#define VIRGL_PIPE_RES_CREATE_SIZE 11
#define VIRGL_PIPE_RES_CREATE_TARGET 0
#define VIRGL_PIPE_RES_CREATE_FORMAT 1
#define VIRGL_PIPE_RES_CREATE_BIND 2
#define VIRGL_PIPE_RES_CREATE_WIDTH 3
#define VIRGL_PIPE_RES_CREATE_HEIGHT 4
#define VIRGL_PIPE_RES_CREATE_DEPTH 5
#define VIRGL_PIPE_RES_CREATE_ARRAY_SIZE 6
#define VIRGL_PIPE_RES_CREATE_LAST_LEVEL 7
#define VIRGL_PIPE_RES_CREATE_NR_SAMPLES 8
#define VIRGL_PIPE_RES_CREATE_FLAGS 9
#define VIRGL_PIPE_RES_CREATE_BLOB_ID 10

// VIRGL_CCMD_PIPE_RESOURCE_SET_TYPE
#define VIRGL_PIPE_RES_SET_TYPE_SIZE(nplanes) (8 + (nplanes) * 2)
#define VIRGL_PIPE_RES_SET_TYPE_RES_HANDLE 0
#define VIRGL_PIPE_RES_SET_TYPE_FORMAT 1
#define VIRGL_PIPE_RES_SET_TYPE_BIND 2
#define VIRGL_PIPE_RES_SET_TYPE_WIDTH 3
#define VIRGL_PIPE_RES_SET_TYPE_HEIGHT 4
#define VIRGL_PIPE_RES_SET_TYPE_USAGE 5
#define VIRGL_PIPE_RES_SET_TYPE_MODIFIER_LO 6
#define VIRGL_PIPE_RES_SET_TYPE_MODIFIER_HI 7
#define VIRGL_PIPE_RES_SET_TYPE_PLANE_STRIDE(plane) (8 + (plane) * 2)
#define VIRGL_PIPE_RES_SET_TYPE_PLANE_OFFSET(plane) (9 + (plane) * 2)

// Send String Marker
#define VIRGL_SEND_STRING_MARKER_MIN_SIZE 2
#define VIRGL_SEND_STRING_MARKER_STRING_SIZE 0
#define VIRGL_SEND_STRING_MARKER_OFFSET 1

enum virgl_object_types {
   VIRTIO_VIRGL_OBJECT_NULL,
   VIRTIO_VIRGL_OBJECT_BLEND,
   VIRTIO_VIRGL_OBJECT_RASTERIZER,
   VIRTIO_VIRGL_OBJECT_DSA,
   VIRTIO_VIRGL_OBJECT_SHADER,
   VIRTIO_VIRGL_OBJECT_VERTEX_ELEMENTS,
   VIRTIO_VIRGL_OBJECT_SAMPLER_VIEW,
   VIRTIO_VIRGL_OBJECT_SAMPLER_STATE,
   VIRTIO_VIRGL_OBJECT_SURFACE,
   VIRTIO_VIRGL_OBJECT_QUERY,
   VIRTIO_VIRGL_OBJECT_STREAMOUT_TARGET,
   VIRTIO_VIRGL_OBJECT_MSAA_SURFACE,
   VIRTIO_VIRGL_MAX_OBJECTS,
};

enum pipe_tex_filter {
    PIPE_TEX_FILTER_NEAREST = 0,
    PIPE_TEX_FILTER_LINEAR = 1,
};

enum virgl_context_cmd {
   VIRTIO_VIRGL_CCMD_NOP = 0,
   VIRTIO_VIRGL_CCMD_CREATE_OBJECT = 1,
   VIRTIO_VIRGL_CCMD_BIND_OBJECT,
   VIRTIO_VIRGL_CCMD_DESTROY_OBJECT,
   VIRTIO_VIRGL_CCMD_SET_VIEWPORT_STATE,
   VIRTIO_VIRGL_CCMD_SET_FRAMEBUFFER_STATE,
   VIRTIO_VIRGL_CCMD_SET_VERTEX_BUFFERS,
   VIRTIO_VIRGL_CCMD_CLEAR,
   VIRTIO_VIRGL_CCMD_DRAW_VBO,
   VIRTIO_VIRGL_CCMD_RESOURCE_INLINE_WRITE,
   VIRTIO_VIRGL_CCMD_SET_SAMPLER_VIEWS,
   VIRTIO_VIRGL_CCMD_SET_INDEX_BUFFER,
   VIRTIO_VIRGL_CCMD_SET_CONSTANT_BUFFER,
   VIRTIO_VIRGL_CCMD_SET_STENCIL_REF,
   VIRTIO_VIRGL_CCMD_SET_BLEND_COLOR,
   VIRTIO_VIRGL_CCMD_SET_SCISSOR_STATE,
   VIRTIO_VIRGL_CCMD_BLIT,
   VIRTIO_VIRGL_CCMD_RESOURCE_COPY_REGION,
   VIRTIO_VIRGL_CCMD_BIND_SAMPLER_STATES,
   VIRTIO_VIRGL_CCMD_BEGIN_QUERY,
   VIRTIO_VIRGL_CCMD_END_QUERY,
   VIRTIO_VIRGL_CCMD_GET_QUERY_RESULT,
   VIRTIO_VIRGL_CCMD_SET_POLYGON_STIPPLE,
   VIRTIO_VIRGL_CCMD_SET_CLIP_STATE,
   VIRTIO_VIRGL_CCMD_SET_SAMPLE_MASK,
   VIRTIO_VIRGL_CCMD_SET_STREAMOUT_TARGETS,
   VIRTIO_VIRGL_CCMD_SET_RENDER_CONDITION,
   VIRTIO_VIRGL_CCMD_SET_UNIFORM_BUFFER,
   VIRTIO_VIRGL_CCMD_SET_SUB_CTX,
   VIRTIO_VIRGL_CCMD_CREATE_SUB_CTX,
   VIRTIO_VIRGL_CCMD_DESTROY_SUB_CTX,
   VIRTIO_VIRGL_CCMD_BIND_SHADER,
   VIRTIO_VIRGL_CCMD_SET_TESS_STATE,
   VIRTIO_VIRGL_CCMD_SET_MIN_SAMPLES,
   VIRTIO_VIRGL_CCMD_SET_SHADER_BUFFERS,
   VIRTIO_VIRGL_CCMD_SET_SHADER_IMAGES,
   VIRTIO_VIRGL_CCMD_MEMORY_BARRIER,
   VIRTIO_VIRGL_CCMD_LAUNCH_GRID,
   VIRTIO_VIRGL_CCMD_SET_FRAMEBUFFER_STATE_NO_ATTACH,
   VIRTIO_VIRGL_CCMD_TEXTURE_BARRIER,
   VIRTIO_VIRGL_CCMD_SET_ATOMIC_BUFFERS,
   VIRTIO_VIRGL_CCMD_SET_DEBUG_FLAGS,
   VIRTIO_VIRGL_CCMD_GET_QUERY_RESULT_QBO,
   VIRTIO_VIRGL_CCMD_TRANSFER3D,
   VIRTIO_VIRGL_CCMD_END_TRANSFERS,
   VIRTIO_VIRGL_CCMD_COPY_TRANSFER3D,
   VIRTIO_VIRGL_CCMD_SET_TWEAKS,
   VIRTIO_VIRGL_CCMD_CLEAR_TEXTURE,
   VIRTIO_VIRGL_CCMD_PIPE_RESOURCE_CREATE,
   VIRTIO_VIRGL_CCMD_PIPE_RESOURCE_SET_TYPE,
   VIRTIO_VIRGL_CCMD_GET_MEMORY_INFO,
   VIRTIO_VIRGL_CCMD_SEND_STRING_MARKER,
   VIRTIO_VIRGL_MAX_COMMANDS
};

// Credits - https://github.com/PojavLauncherTeam/virglrenderer/blob/master/src/virgl_hw.h
/* formats known by the HW device - based on gallium subset */
enum virgl_formats {
   VIRGL_FORMAT_NONE                    = 0,
   VIRGL_FORMAT_B8G8R8A8_UNORM          = 1,
   VIRGL_FORMAT_B8G8R8X8_UNORM          = 2,
   VIRGL_FORMAT_A8R8G8B8_UNORM          = 3,
   VIRGL_FORMAT_X8R8G8B8_UNORM          = 4,
   VIRGL_FORMAT_B5G5R5A1_UNORM          = 5,
   VIRGL_FORMAT_B4G4R4A4_UNORM          = 6,
   VIRGL_FORMAT_B5G6R5_UNORM            = 7,
   VIRGL_FORMAT_R10G10B10A2_UNORM       = 8,
   VIRGL_FORMAT_L8_UNORM                = 9,    /**< ubyte luminance */
   VIRGL_FORMAT_A8_UNORM                = 10,   /**< ubyte alpha */
   VIRGL_FORMAT_I8_UNORM                = 11,
   VIRGL_FORMAT_L8A8_UNORM              = 12,   /**< ubyte alpha, luminance */
   VIRGL_FORMAT_L16_UNORM               = 13,   /**< ushort luminance */
   VIRGL_FORMAT_UYVY                    = 14,
   VIRGL_FORMAT_YUYV                    = 15,
   VIRGL_FORMAT_Z16_UNORM               = 16,
   VIRGL_FORMAT_Z32_UNORM               = 17,
   VIRGL_FORMAT_Z32_FLOAT               = 18,
   VIRGL_FORMAT_Z24_UNORM_S8_UINT       = 19,
   VIRGL_FORMAT_S8_UINT_Z24_UNORM       = 20,
   VIRGL_FORMAT_Z24X8_UNORM             = 21,
   VIRGL_FORMAT_X8Z24_UNORM             = 22,
   VIRGL_FORMAT_S8_UINT                 = 23,   /**< ubyte stencil */
   VIRGL_FORMAT_R64_FLOAT               = 24,
   VIRGL_FORMAT_R64G64_FLOAT            = 25,
   VIRGL_FORMAT_R64G64B64_FLOAT         = 26,
   VIRGL_FORMAT_R64G64B64A64_FLOAT      = 27,
   VIRGL_FORMAT_R32_FLOAT               = 28,
   VIRGL_FORMAT_R32G32_FLOAT            = 29,
   VIRGL_FORMAT_R32G32B32_FLOAT         = 30,
   VIRGL_FORMAT_R32G32B32A32_FLOAT      = 31,

   VIRGL_FORMAT_R32_UNORM               = 32,
   VIRGL_FORMAT_R32G32_UNORM            = 33,
   VIRGL_FORMAT_R32G32B32_UNORM         = 34,
   VIRGL_FORMAT_R32G32B32A32_UNORM      = 35,
   VIRGL_FORMAT_R32_USCALED             = 36,
   VIRGL_FORMAT_R32G32_USCALED          = 37,
   VIRGL_FORMAT_R32G32B32_USCALED       = 38,
   VIRGL_FORMAT_R32G32B32A32_USCALED    = 39,
   VIRGL_FORMAT_R32_SNORM               = 40,
   VIRGL_FORMAT_R32G32_SNORM            = 41,
   VIRGL_FORMAT_R32G32B32_SNORM         = 42,
   VIRGL_FORMAT_R32G32B32A32_SNORM      = 43,
   VIRGL_FORMAT_R32_SSCALED             = 44,
   VIRGL_FORMAT_R32G32_SSCALED          = 45,
   VIRGL_FORMAT_R32G32B32_SSCALED       = 46,
   VIRGL_FORMAT_R32G32B32A32_SSCALED    = 47,

   VIRGL_FORMAT_R16_UNORM               = 48,
   VIRGL_FORMAT_R16G16_UNORM            = 49,
   VIRGL_FORMAT_R16G16B16_UNORM         = 50,
   VIRGL_FORMAT_R16G16B16A16_UNORM      = 51,

   VIRGL_FORMAT_R16_USCALED             = 52,
   VIRGL_FORMAT_R16G16_USCALED          = 53,
   VIRGL_FORMAT_R16G16B16_USCALED       = 54,
   VIRGL_FORMAT_R16G16B16A16_USCALED    = 55,

   VIRGL_FORMAT_R16_SNORM               = 56,
   VIRGL_FORMAT_R16G16_SNORM            = 57,
   VIRGL_FORMAT_R16G16B16_SNORM         = 58,
   VIRGL_FORMAT_R16G16B16A16_SNORM      = 59,

   VIRGL_FORMAT_R16_SSCALED             = 60,
   VIRGL_FORMAT_R16G16_SSCALED          = 61,
   VIRGL_FORMAT_R16G16B16_SSCALED       = 62,
   VIRGL_FORMAT_R16G16B16A16_SSCALED    = 63,

   VIRGL_FORMAT_R8_UNORM                = 64,
   VIRGL_FORMAT_R8G8_UNORM              = 65,
   VIRGL_FORMAT_R8G8B8_UNORM            = 66,
   VIRGL_FORMAT_R8G8B8A8_UNORM          = 67,
   VIRGL_FORMAT_X8B8G8R8_UNORM          = 68,

   VIRGL_FORMAT_R8_USCALED              = 69,
   VIRGL_FORMAT_R8G8_USCALED            = 70,
   VIRGL_FORMAT_R8G8B8_USCALED          = 71,
   VIRGL_FORMAT_R8G8B8A8_USCALED        = 72,

   VIRGL_FORMAT_R8_SNORM                = 74,
   VIRGL_FORMAT_R8G8_SNORM              = 75,
   VIRGL_FORMAT_R8G8B8_SNORM            = 76,
   VIRGL_FORMAT_R8G8B8A8_SNORM          = 77,

   VIRGL_FORMAT_R8_SSCALED              = 82,
   VIRGL_FORMAT_R8G8_SSCALED            = 83,
   VIRGL_FORMAT_R8G8B8_SSCALED          = 84,
   VIRGL_FORMAT_R8G8B8A8_SSCALED        = 85,

   VIRGL_FORMAT_R32_FIXED               = 87,
   VIRGL_FORMAT_R32G32_FIXED            = 88,
   VIRGL_FORMAT_R32G32B32_FIXED         = 89,
   VIRGL_FORMAT_R32G32B32A32_FIXED      = 90,

   VIRGL_FORMAT_R16_FLOAT               = 91,
   VIRGL_FORMAT_R16G16_FLOAT            = 92,
   VIRGL_FORMAT_R16G16B16_FLOAT         = 93,
   VIRGL_FORMAT_R16G16B16A16_FLOAT      = 94,

   VIRGL_FORMAT_L8_SRGB                 = 95,
   VIRGL_FORMAT_L8A8_SRGB               = 96,
   VIRGL_FORMAT_R8G8B8_SRGB             = 97,
   VIRGL_FORMAT_A8B8G8R8_SRGB           = 98,
   VIRGL_FORMAT_X8B8G8R8_SRGB           = 99,
   VIRGL_FORMAT_B8G8R8A8_SRGB           = 100,
   VIRGL_FORMAT_B8G8R8X8_SRGB           = 101,
   VIRGL_FORMAT_A8R8G8B8_SRGB           = 102,
   VIRGL_FORMAT_X8R8G8B8_SRGB           = 103,
   VIRGL_FORMAT_R8G8B8A8_SRGB           = 104,

   /* compressed formats */
   VIRGL_FORMAT_DXT1_RGB                = 105,
   VIRGL_FORMAT_DXT1_RGBA               = 106,
   VIRGL_FORMAT_DXT3_RGBA               = 107,
   VIRGL_FORMAT_DXT5_RGBA               = 108,

   /* sRGB, compressed */
   VIRGL_FORMAT_DXT1_SRGB               = 109,
   VIRGL_FORMAT_DXT1_SRGBA              = 110,
   VIRGL_FORMAT_DXT3_SRGBA              = 111,
   VIRGL_FORMAT_DXT5_SRGBA              = 112,

   /* rgtc compressed */
   VIRGL_FORMAT_RGTC1_UNORM             = 113,
   VIRGL_FORMAT_RGTC1_SNORM             = 114,
   VIRGL_FORMAT_RGTC2_UNORM             = 115,
   VIRGL_FORMAT_RGTC2_SNORM             = 116,

   VIRGL_FORMAT_R8G8_B8G8_UNORM         = 117,
   VIRGL_FORMAT_G8R8_G8B8_UNORM         = 118,

   VIRGL_FORMAT_R8SG8SB8UX8U_NORM       = 119,
   VIRGL_FORMAT_R5SG5SB6U_NORM          = 120,

   VIRGL_FORMAT_A8B8G8R8_UNORM          = 121,
   VIRGL_FORMAT_B5G5R5X1_UNORM          = 122,
   VIRGL_FORMAT_R10G10B10A2_USCALED     = 123,
   VIRGL_FORMAT_R11G11B10_FLOAT         = 124,
   VIRGL_FORMAT_R9G9B9E5_FLOAT          = 125,
   VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT    = 126,
   VIRGL_FORMAT_R1_UNORM                = 127,
   VIRGL_FORMAT_R10G10B10X2_USCALED     = 128,
   VIRGL_FORMAT_R10G10B10X2_SNORM       = 129,

   VIRGL_FORMAT_L4A4_UNORM              = 130,
   VIRGL_FORMAT_B10G10R10A2_UNORM       = 131,
   VIRGL_FORMAT_R10SG10SB10SA2U_NORM    = 132,
   VIRGL_FORMAT_R8G8Bx_SNORM            = 133,
   VIRGL_FORMAT_R8G8B8X8_UNORM          = 134,
   VIRGL_FORMAT_B4G4R4X4_UNORM          = 135,
   VIRGL_FORMAT_X24S8_UINT              = 136,
   VIRGL_FORMAT_S8X24_UINT              = 137,
   VIRGL_FORMAT_X32_S8X24_UINT          = 138,
   VIRGL_FORMAT_B2G3R3_UNORM            = 139,

   VIRGL_FORMAT_L16A16_UNORM            = 140,
   VIRGL_FORMAT_A16_UNORM               = 141,
   VIRGL_FORMAT_I16_UNORM               = 142,

   VIRGL_FORMAT_LATC1_UNORM             = 143,
   VIRGL_FORMAT_LATC1_SNORM             = 144,
   VIRGL_FORMAT_LATC2_UNORM             = 145,
   VIRGL_FORMAT_LATC2_SNORM             = 146,

   VIRGL_FORMAT_A8_SNORM                = 147,
   VIRGL_FORMAT_L8_SNORM                = 148,
   VIRGL_FORMAT_L8A8_SNORM              = 149,
   VIRGL_FORMAT_I8_SNORM                = 150,
   VIRGL_FORMAT_A16_SNORM               = 151,
   VIRGL_FORMAT_L16_SNORM               = 152,
   VIRGL_FORMAT_L16A16_SNORM            = 153,
   VIRGL_FORMAT_I16_SNORM               = 154,

   VIRGL_FORMAT_A16_FLOAT               = 155,
   VIRGL_FORMAT_L16_FLOAT               = 156,
   VIRGL_FORMAT_L16A16_FLOAT            = 157,
   VIRGL_FORMAT_I16_FLOAT               = 158,
   VIRGL_FORMAT_A32_FLOAT               = 159,
   VIRGL_FORMAT_L32_FLOAT               = 160,
   VIRGL_FORMAT_L32A32_FLOAT            = 161,
   VIRGL_FORMAT_I32_FLOAT               = 162,

   VIRGL_FORMAT_YV12                    = 163,
   VIRGL_FORMAT_YV16                    = 164,
   VIRGL_FORMAT_IYUV                    = 165,  /**< aka I420 */
   VIRGL_FORMAT_NV12                    = 166,
   VIRGL_FORMAT_NV21                    = 167,

   VIRGL_FORMAT_A4R4_UNORM              = 168,
   VIRGL_FORMAT_R4A4_UNORM              = 169,
   VIRGL_FORMAT_R8A8_UNORM              = 170,
   VIRGL_FORMAT_A8R8_UNORM              = 171,

   VIRGL_FORMAT_R10G10B10A2_SSCALED     = 172,
   VIRGL_FORMAT_R10G10B10A2_SNORM       = 173,
   VIRGL_FORMAT_B10G10R10A2_USCALED     = 174,
   VIRGL_FORMAT_B10G10R10A2_SSCALED     = 175,
   VIRGL_FORMAT_B10G10R10A2_SNORM       = 176,

   VIRGL_FORMAT_R8_UINT                 = 177,
   VIRGL_FORMAT_R8G8_UINT               = 178,
   VIRGL_FORMAT_R8G8B8_UINT             = 179,
   VIRGL_FORMAT_R8G8B8A8_UINT           = 180,

   VIRGL_FORMAT_R8_SINT                 = 181,
   VIRGL_FORMAT_R8G8_SINT               = 182,
   VIRGL_FORMAT_R8G8B8_SINT             = 183,
   VIRGL_FORMAT_R8G8B8A8_SINT           = 184,

   VIRGL_FORMAT_R16_UINT                = 185,
   VIRGL_FORMAT_R16G16_UINT             = 186,
   VIRGL_FORMAT_R16G16B16_UINT          = 187,
   VIRGL_FORMAT_R16G16B16A16_UINT       = 188,

   VIRGL_FORMAT_R16_SINT                = 189,
   VIRGL_FORMAT_R16G16_SINT             = 190,
   VIRGL_FORMAT_R16G16B16_SINT          = 191,
   VIRGL_FORMAT_R16G16B16A16_SINT       = 192,
   VIRGL_FORMAT_R32_UINT                = 193,
   VIRGL_FORMAT_R32G32_UINT             = 194,
   VIRGL_FORMAT_R32G32B32_UINT          = 195,
   VIRGL_FORMAT_R32G32B32A32_UINT       = 196,

   VIRGL_FORMAT_R32_SINT                = 197,
   VIRGL_FORMAT_R32G32_SINT             = 198,
   VIRGL_FORMAT_R32G32B32_SINT          = 199,
   VIRGL_FORMAT_R32G32B32A32_SINT       = 200,

   VIRGL_FORMAT_A8_UINT                 = 201,
   VIRGL_FORMAT_I8_UINT                 = 202,
   VIRGL_FORMAT_L8_UINT                 = 203,
   VIRGL_FORMAT_L8A8_UINT               = 204,

   VIRGL_FORMAT_A8_SINT                 = 205,
   VIRGL_FORMAT_I8_SINT                 = 206,
   VIRGL_FORMAT_L8_SINT                 = 207,
   VIRGL_FORMAT_L8A8_SINT               = 208,

   VIRGL_FORMAT_A16_UINT                = 209,
   VIRGL_FORMAT_I16_UINT                = 210,
   VIRGL_FORMAT_L16_UINT                = 211,
   VIRGL_FORMAT_L16A16_UINT             = 212,

   VIRGL_FORMAT_A16_SINT                = 213,
   VIRGL_FORMAT_I16_SINT                = 214,
   VIRGL_FORMAT_L16_SINT                = 215,
   VIRGL_FORMAT_L16A16_SINT             = 216,

   VIRGL_FORMAT_A32_UINT                = 217,
   VIRGL_FORMAT_I32_UINT                = 218,
   VIRGL_FORMAT_L32_UINT                = 219,
   VIRGL_FORMAT_L32A32_UINT             = 220,

   VIRGL_FORMAT_A32_SINT                = 221,
   VIRGL_FORMAT_I32_SINT                = 222,
   VIRGL_FORMAT_L32_SINT                = 223,
   VIRGL_FORMAT_L32A32_SINT             = 224,

   VIRGL_FORMAT_B10G10R10A2_UINT        = 225,
   VIRGL_FORMAT_ETC1_RGB8               = 226,
   VIRGL_FORMAT_R8G8_R8B8_UNORM         = 227,
   VIRGL_FORMAT_G8R8_B8R8_UNORM         = 228,
   VIRGL_FORMAT_R8G8B8X8_SNORM          = 229,

   VIRGL_FORMAT_R8G8B8X8_SRGB           = 230,

   VIRGL_FORMAT_R8G8B8X8_UINT           = 231,
   VIRGL_FORMAT_R8G8B8X8_SINT           = 232,
   VIRGL_FORMAT_B10G10R10X2_UNORM       = 233,
   VIRGL_FORMAT_R16G16B16X16_UNORM      = 234,
   VIRGL_FORMAT_R16G16B16X16_SNORM      = 235,
   VIRGL_FORMAT_R16G16B16X16_FLOAT      = 236,
   VIRGL_FORMAT_R16G16B16X16_UINT       = 237,
   VIRGL_FORMAT_R16G16B16X16_SINT       = 238,
   VIRGL_FORMAT_R32G32B32X32_FLOAT      = 239,
   VIRGL_FORMAT_R32G32B32X32_UINT       = 240,
   VIRGL_FORMAT_R32G32B32X32_SINT       = 241,
   VIRGL_FORMAT_R8A8_SNORM              = 242,
   VIRGL_FORMAT_R16A16_UNORM            = 243,
   VIRGL_FORMAT_R16A16_SNORM            = 244,
   VIRGL_FORMAT_R16A16_FLOAT            = 245,
   VIRGL_FORMAT_R32A32_FLOAT            = 246,
   VIRGL_FORMAT_R8A8_UINT               = 247,
   VIRGL_FORMAT_R8A8_SINT               = 248,
   VIRGL_FORMAT_R16A16_UINT             = 249,
   VIRGL_FORMAT_R16A16_SINT             = 250,
   VIRGL_FORMAT_R32A32_UINT             = 251,
   VIRGL_FORMAT_R32A32_SINT             = 252,

   VIRGL_FORMAT_R10G10B10A2_UINT        = 253,
   VIRGL_FORMAT_B5G6R5_SRGB             = 254,

   VIRGL_FORMAT_BPTC_RGBA_UNORM         = 255,
   VIRGL_FORMAT_BPTC_SRGBA              = 256,
   VIRGL_FORMAT_BPTC_RGB_FLOAT          = 257,
   VIRGL_FORMAT_BPTC_RGB_UFLOAT         = 258,

   VIRGL_FORMAT_A16L16_UNORM            = 262,

   VIRGL_FORMAT_G8R8_UNORM              = 263,
   VIRGL_FORMAT_G8R8_SNORM              = 264,
   VIRGL_FORMAT_G16R16_UNORM            = 265,
   VIRGL_FORMAT_G16R16_SNORM            = 266,
   VIRGL_FORMAT_A8B8G8R8_SNORM          = 267,

   VIRGL_FORMAT_A8L8_UNORM              = 259,
   VIRGL_FORMAT_A8L8_SNORM              = 260,
   VIRGL_FORMAT_A8L8_SRGB               = 261,

   VIRGL_FORMAT_X8B8G8R8_SNORM          = 268,


   /* etc2 compressed */
   VIRGL_FORMAT_ETC2_RGB8               = 269,
   VIRGL_FORMAT_ETC2_SRGB8              = 270,
   VIRGL_FORMAT_ETC2_RGB8A1             = 271,
   VIRGL_FORMAT_ETC2_SRGB8A1            = 272,
   VIRGL_FORMAT_ETC2_RGBA8              = 273,
   VIRGL_FORMAT_ETC2_SRGBA8             = 274,
   VIRGL_FORMAT_ETC2_R11_UNORM          = 275,
   VIRGL_FORMAT_ETC2_R11_SNORM          = 276,
   VIRGL_FORMAT_ETC2_RG11_UNORM         = 277,
   VIRGL_FORMAT_ETC2_RG11_SNORM         = 278,

   VIRGL_FORMAT_ASTC_4x4                = 279,
   VIRGL_FORMAT_ASTC_5x4                = 280,
   VIRGL_FORMAT_ASTC_5x5                = 281,
   VIRGL_FORMAT_ASTC_6x5                = 282,
   VIRGL_FORMAT_ASTC_6x6                = 283,
   VIRGL_FORMAT_ASTC_8x5                = 284,
   VIRGL_FORMAT_ASTC_8x6                = 285,
   VIRGL_FORMAT_ASTC_8x8                = 286,
   VIRGL_FORMAT_ASTC_10x5               = 287,
   VIRGL_FORMAT_ASTC_10x6               = 288,
   VIRGL_FORMAT_ASTC_10x8               = 289,
   VIRGL_FORMAT_ASTC_10x10              = 290,
   VIRGL_FORMAT_ASTC_12x10              = 291,
   VIRGL_FORMAT_ASTC_12x12              = 292,
   VIRGL_FORMAT_ASTC_4x4_SRGB           = 293,
   VIRGL_FORMAT_ASTC_5x4_SRGB           = 294,
   VIRGL_FORMAT_ASTC_5x5_SRGB           = 295,
   VIRGL_FORMAT_ASTC_6x5_SRGB           = 296,
   VIRGL_FORMAT_ASTC_6x6_SRGB           = 297,
   VIRGL_FORMAT_ASTC_8x5_SRGB           = 298,
   VIRGL_FORMAT_ASTC_8x6_SRGB           = 299,
   VIRGL_FORMAT_ASTC_8x8_SRGB           = 300,
   VIRGL_FORMAT_ASTC_10x5_SRGB          = 301,
   VIRGL_FORMAT_ASTC_10x6_SRGB          = 302,
   VIRGL_FORMAT_ASTC_10x8_SRGB          = 303,
   VIRGL_FORMAT_ASTC_10x10_SRGB         = 304,
   VIRGL_FORMAT_ASTC_12x10_SRGB         = 305,
   VIRGL_FORMAT_ASTC_12x12_SRGB         = 306,

   VIRGL_FORMAT_R10G10B10X2_UNORM       = 308,
   VIRGL_FORMAT_A4B4G4R4_UNORM          = 311,

   VIRGL_FORMAT_R8_SRGB                 = 312,
   VIRGL_FORMAT_MAX /* = PIPE_FORMAT_COUNT */,

   /* Below formats must not be used in the guest. */
   VIRGL_FORMAT_B8G8R8X8_UNORM_EMULATED,
   VIRGL_FORMAT_B8G8R8A8_UNORM_EMULATED,
   VIRGL_FORMAT_MAX_EXTENDED
};

// Pipe Format
#define PIPE_FORMAT_NONE VIRGL_FORMAT_NONE
#define PIPE_FORMAT_B8G8R8A8_UNORM VIRGL_FORMAT_B8G8R8A8_UNORM
#define PIPE_FORMAT_B8G8R8X8_UNORM VIRGL_FORMAT_B8G8R8X8_UNORM
#define PIPE_FORMAT_A8R8G8B8_UNORM VIRGL_FORMAT_A8R8G8B8_UNORM
#define PIPE_FORMAT_X8R8G8B8_UNORM VIRGL_FORMAT_X8R8G8B8_UNORM
#define PIPE_FORMAT_B5G5R5A1_UNORM VIRGL_FORMAT_B5G5R5A1_UNORM
#define PIPE_FORMAT_B4G4R4A4_UNORM VIRGL_FORMAT_B4G4R4A4_UNORM
#define PIPE_FORMAT_B5G6R5_UNORM VIRGL_FORMAT_B5G6R5_UNORM
#define PIPE_FORMAT_R10G10B10A2_UNORM VIRGL_FORMAT_R10G10B10A2_UNORM
#define PIPE_FORMAT_L8_UNORM VIRGL_FORMAT_L8_UNORM
#define PIPE_FORMAT_A8_UNORM VIRGL_FORMAT_A8_UNORM
#define PIPE_FORMAT_I8_UNORM VIRGL_FORMAT_I8_UNORM
#define PIPE_FORMAT_L8A8_UNORM VIRGL_FORMAT_L8A8_UNORM
#define PIPE_FORMAT_L16_UNORM VIRGL_FORMAT_L16_UNORM
#define PIPE_FORMAT_UYVY VIRGL_FORMAT_UYVY
#define PIPE_FORMAT_YUYV VIRGL_FORMAT_YUYV
#define PIPE_FORMAT_Z16_UNORM VIRGL_FORMAT_Z16_UNORM
#define PIPE_FORMAT_Z32_UNORM VIRGL_FORMAT_Z32_UNORM
#define PIPE_FORMAT_Z32_FLOAT VIRGL_FORMAT_Z32_FLOAT
#define PIPE_FORMAT_Z24_UNORM_S8_UINT VIRGL_FORMAT_Z24_UNORM_S8_UINT
#define PIPE_FORMAT_S8_UINT_Z24_UNORM VIRGL_FORMAT_S8_UINT_Z24_UNORM
#define PIPE_FORMAT_Z24X8_UNORM VIRGL_FORMAT_Z24X8_UNORM
#define PIPE_FORMAT_X8Z24_UNORM VIRGL_FORMAT_X8Z24_UNORM
#define PIPE_FORMAT_S8_UINT VIRGL_FORMAT_S8_UINT
#define PIPE_FORMAT_R64_FLOAT VIRGL_FORMAT_R64_FLOAT
#define PIPE_FORMAT_R64G64_FLOAT VIRGL_FORMAT_R64G64_FLOAT
#define PIPE_FORMAT_R64G64B64_FLOAT VIRGL_FORMAT_R64G64B64_FLOAT
#define PIPE_FORMAT_R64G64B64A64_FLOAT VIRGL_FORMAT_R64G64B64A64_FLOAT
#define PIPE_FORMAT_R32_FLOAT VIRGL_FORMAT_R32_FLOAT
#define PIPE_FORMAT_R32G32_FLOAT VIRGL_FORMAT_R32G32_FLOAT
#define PIPE_FORMAT_R32G32B32_FLOAT VIRGL_FORMAT_R32G32B32_FLOAT
#define PIPE_FORMAT_R32G32B32A32_FLOAT VIRGL_FORMAT_R32G32B32A32_FLOAT
#define PIPE_FORMAT_R32_UNORM VIRGL_FORMAT_R32_UNORM
#define PIPE_FORMAT_R32G32_UNORM VIRGL_FORMAT_R32G32_UNORM
#define PIPE_FORMAT_R32G32B32_UNORM VIRGL_FORMAT_R32G32B32_UNORM
#define PIPE_FORMAT_R32G32B32A32_UNORM VIRGL_FORMAT_R32G32B32A32_UNORM
#define PIPE_FORMAT_R32_USCALED VIRGL_FORMAT_R32_USCALED
#define PIPE_FORMAT_R32G32_USCALED VIRGL_FORMAT_R32G32_USCALED
#define PIPE_FORMAT_R32G32B32_USCALED VIRGL_FORMAT_R32G32B32_USCALED
#define PIPE_FORMAT_R32G32B32A32_USCALED VIRGL_FORMAT_R32G32B32A32_USCALED
#define PIPE_FORMAT_R32_SNORM VIRGL_FORMAT_R32_SNORM
#define PIPE_FORMAT_R32G32_SNORM VIRGL_FORMAT_R32G32_SNORM
#define PIPE_FORMAT_R32G32B32_SNORM VIRGL_FORMAT_R32G32B32_SNORM
#define PIPE_FORMAT_R32G32B32A32_SNORM VIRGL_FORMAT_R32G32B32A32_SNORM
#define PIPE_FORMAT_R32_SSCALED VIRGL_FORMAT_R32_SSCALED
#define PIPE_FORMAT_R32G32_SSCALED VIRGL_FORMAT_R32G32_SSCALED
#define PIPE_FORMAT_R32G32B32_SSCALED VIRGL_FORMAT_R32G32B32_SSCALED
#define PIPE_FORMAT_R32G32B32A32_SSCALED VIRGL_FORMAT_R32G32B32A32_SSCALED
#define PIPE_FORMAT_R16_UNORM VIRGL_FORMAT_R16_UNORM
#define PIPE_FORMAT_R16G16_UNORM VIRGL_FORMAT_R16G16_UNORM
#define PIPE_FORMAT_R16G16B16_UNORM VIRGL_FORMAT_R16G16B16_UNORM
#define PIPE_FORMAT_R16G16B16A16_UNORM VIRGL_FORMAT_R16G16B16A16_UNORM
#define PIPE_FORMAT_R16_USCALED VIRGL_FORMAT_R16_USCALED
#define PIPE_FORMAT_R16G16_USCALED VIRGL_FORMAT_R16G16_USCALED
#define PIPE_FORMAT_R16G16B16_USCALED VIRGL_FORMAT_R16G16B16_USCALED
#define PIPE_FORMAT_R16G16B16A16_USCALED VIRGL_FORMAT_R16G16B16A16_USCALED
#define PIPE_FORMAT_R16_SNORM VIRGL_FORMAT_R16_SNORM
#define PIPE_FORMAT_R16G16_SNORM VIRGL_FORMAT_R16G16_SNORM
#define PIPE_FORMAT_R16G16B16_SNORM VIRGL_FORMAT_R16G16B16_SNORM
#define PIPE_FORMAT_R16G16B16A16_SNORM VIRGL_FORMAT_R16G16B16A16_SNORM
#define PIPE_FORMAT_R16_SSCALED VIRGL_FORMAT_R16_SSCALED
#define PIPE_FORMAT_R16G16_SSCALED VIRGL_FORMAT_R16G16_SSCALED
#define PIPE_FORMAT_R16G16B16_SSCALED VIRGL_FORMAT_R16G16B16_SSCALED
#define PIPE_FORMAT_R16G16B16A16_SSCALED VIRGL_FORMAT_R16G16B16A16_SSCALED
#define PIPE_FORMAT_R8_UNORM VIRGL_FORMAT_R8_UNORM
#define PIPE_FORMAT_R8G8_UNORM VIRGL_FORMAT_R8G8_UNORM
#define PIPE_FORMAT_R8G8B8_UNORM VIRGL_FORMAT_R8G8B8_UNORM
#define PIPE_FORMAT_R8G8B8A8_UNORM VIRGL_FORMAT_R8G8B8A8_UNORM
#define PIPE_FORMAT_X8B8G8R8_UNORM VIRGL_FORMAT_X8B8G8R8_UNORM
#define PIPE_FORMAT_R8_USCALED VIRGL_FORMAT_R8_USCALED
#define PIPE_FORMAT_R8G8_USCALED VIRGL_FORMAT_R8G8_USCALED
#define PIPE_FORMAT_R8G8B8_USCALED VIRGL_FORMAT_R8G8B8_USCALED
#define PIPE_FORMAT_R8G8B8A8_USCALED VIRGL_FORMAT_R8G8B8A8_USCALED
#define PIPE_FORMAT_R8_SNORM VIRGL_FORMAT_R8_SNORM
#define PIPE_FORMAT_R8G8_SNORM VIRGL_FORMAT_R8G8_SNORM
#define PIPE_FORMAT_R8G8B8_SNORM VIRGL_FORMAT_R8G8B8_SNORM
#define PIPE_FORMAT_R8G8B8A8_SNORM VIRGL_FORMAT_R8G8B8A8_SNORM
#define PIPE_FORMAT_R8_SSCALED VIRGL_FORMAT_R8_SSCALED
#define PIPE_FORMAT_R8G8_SSCALED VIRGL_FORMAT_R8G8_SSCALED
#define PIPE_FORMAT_R8G8B8_SSCALED VIRGL_FORMAT_R8G8B8_SSCALED
#define PIPE_FORMAT_R8G8B8A8_SSCALED VIRGL_FORMAT_R8G8B8A8_SSCALED
#define PIPE_FORMAT_R32_FIXED VIRGL_FORMAT_R32_FIXED
#define PIPE_FORMAT_R32G32_FIXED VIRGL_FORMAT_R32G32_FIXED
#define PIPE_FORMAT_R32G32B32_FIXED VIRGL_FORMAT_R32G32B32_FIXED
#define PIPE_FORMAT_R32G32B32A32_FIXED VIRGL_FORMAT_R32G32B32A32_FIXED
#define PIPE_FORMAT_R16_FLOAT VIRGL_FORMAT_R16_FLOAT
#define PIPE_FORMAT_R16G16_FLOAT VIRGL_FORMAT_R16G16_FLOAT
#define PIPE_FORMAT_R16G16B16_FLOAT VIRGL_FORMAT_R16G16B16_FLOAT
#define PIPE_FORMAT_R16G16B16A16_FLOAT VIRGL_FORMAT_R16G16B16A16_FLOAT

#define PIPE_FORMAT_L8_SRGB VIRGL_FORMAT_L8_SRGB
#define PIPE_FORMAT_L8A8_SRGB VIRGL_FORMAT_L8A8_SRGB
#define PIPE_FORMAT_R8G8B8_SRGB VIRGL_FORMAT_R8G8B8_SRGB
#define PIPE_FORMAT_A8B8G8R8_SRGB VIRGL_FORMAT_A8B8G8R8_SRGB
#define PIPE_FORMAT_X8B8G8R8_SRGB VIRGL_FORMAT_X8B8G8R8_SRGB
#define PIPE_FORMAT_B8G8R8A8_SRGB VIRGL_FORMAT_B8G8R8A8_SRGB
#define PIPE_FORMAT_B8G8R8X8_SRGB VIRGL_FORMAT_B8G8R8X8_SRGB
#define PIPE_FORMAT_A8R8G8B8_SRGB VIRGL_FORMAT_A8R8G8B8_SRGB
#define PIPE_FORMAT_X8R8G8B8_SRGB VIRGL_FORMAT_X8R8G8B8_SRGB
#define PIPE_FORMAT_R8G8B8A8_SRGB VIRGL_FORMAT_R8G8B8A8_SRGB

#define PIPE_FORMAT_DXT1_RGB VIRGL_FORMAT_DXT1_RGB
#define PIPE_FORMAT_DXT1_RGBA VIRGL_FORMAT_DXT1_RGBA
#define PIPE_FORMAT_DXT3_RGBA VIRGL_FORMAT_DXT3_RGBA
#define PIPE_FORMAT_DXT5_RGBA VIRGL_FORMAT_DXT5_RGBA

#define PIPE_FORMAT_DXT1_SRGB VIRGL_FORMAT_DXT1_SRGB
#define PIPE_FORMAT_DXT1_SRGBA VIRGL_FORMAT_DXT1_SRGBA
#define PIPE_FORMAT_DXT3_SRGBA VIRGL_FORMAT_DXT3_SRGBA
#define PIPE_FORMAT_DXT5_SRGBA VIRGL_FORMAT_DXT5_SRGBA

#define PIPE_FORMAT_RGTC1_UNORM VIRGL_FORMAT_RGTC1_UNORM
#define PIPE_FORMAT_RGTC1_SNORM VIRGL_FORMAT_RGTC1_SNORM
#define PIPE_FORMAT_RGTC2_UNORM VIRGL_FORMAT_RGTC2_UNORM
#define PIPE_FORMAT_RGTC2_SNORM VIRGL_FORMAT_RGTC2_SNORM

#define PIPE_FORMAT_R8G8_B8G8_UNORM VIRGL_FORMAT_R8G8_B8G8_UNORM
#define PIPE_FORMAT_G8R8_G8B8_UNORM VIRGL_FORMAT_G8R8_G8B8_UNORM

#define PIPE_FORMAT_R8SG8SB8UX8U_NORM VIRGL_FORMAT_R8SG8SB8UX8U_NORM
#define PIPE_FORMAT_R5SG5SB6U_NORM VIRGL_FORMAT_R5SG5SB6U_NORM

#define PIPE_FORMAT_A8B8G8R8_UNORM VIRGL_FORMAT_A8B8G8R8_UNORM
#define PIPE_FORMAT_B5G5R5X1_UNORM VIRGL_FORMAT_B5G5R5X1_UNORM
#define PIPE_FORMAT_R10G10B10A2_USCALED VIRGL_FORMAT_R10G10B10A2_USCALED
#define PIPE_FORMAT_R11G11B10_FLOAT VIRGL_FORMAT_R11G11B10_FLOAT
#define PIPE_FORMAT_R9G9B9E5_FLOAT VIRGL_FORMAT_R9G9B9E5_FLOAT
#define PIPE_FORMAT_Z32_FLOAT_S8X24_UINT VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT
#define PIPE_FORMAT_R1_UNORM VIRGL_FORMAT_R1_UNORM
#define PIPE_FORMAT_R10G10B10X2_USCALED VIRGL_FORMAT_R10G10B10X2_USCALED
#define PIPE_FORMAT_R10G10B10X2_SNORM VIRGL_FORMAT_R10G10B10X2_SNORM
#define PIPE_FORMAT_L4A4_UNORM VIRGL_FORMAT_L4A4_UNORM
#define PIPE_FORMAT_B10G10R10A2_UNORM VIRGL_FORMAT_B10G10R10A2_UNORM
#define PIPE_FORMAT_R10SG10SB10SA2U_NORM VIRGL_FORMAT_R10SG10SB10SA2U_NORM
#define PIPE_FORMAT_R8G8Bx_SNORM VIRGL_FORMAT_R8G8Bx_SNORM
#define PIPE_FORMAT_R8G8B8X8_UNORM VIRGL_FORMAT_R8G8B8X8_UNORM
#define PIPE_FORMAT_B4G4R4X4_UNORM VIRGL_FORMAT_B4G4R4X4_UNORM

#define PIPE_FORMAT_X24S8_UINT VIRGL_FORMAT_X24S8_UINT
#define PIPE_FORMAT_S8X24_UINT VIRGL_FORMAT_S8X24_UINT
#define PIPE_FORMAT_X32_S8X24_UINT VIRGL_FORMAT_X32_S8X24_UINT

#define PIPE_FORMAT_B2G3R3_UNORM VIRGL_FORMAT_B2G3R3_UNORM
#define PIPE_FORMAT_L16A16_UNORM VIRGL_FORMAT_L16A16_UNORM
#define PIPE_FORMAT_A16_UNORM VIRGL_FORMAT_A16_UNORM
#define PIPE_FORMAT_I16_UNORM VIRGL_FORMAT_I16_UNORM

#define PIPE_FORMAT_LATC1_UNORM VIRGL_FORMAT_LATC1_UNORM
#define PIPE_FORMAT_LATC1_SNORM VIRGL_FORMAT_LATC1_SNORM
#define PIPE_FORMAT_LATC2_UNORM VIRGL_FORMAT_LATC2_UNORM
#define PIPE_FORMAT_LATC2_SNORM VIRGL_FORMAT_LATC2_SNORM

#define PIPE_FORMAT_A8_SNORM VIRGL_FORMAT_A8_SNORM
#define PIPE_FORMAT_L8_SNORM VIRGL_FORMAT_L8_SNORM
#define PIPE_FORMAT_L8A8_SNORM VIRGL_FORMAT_L8A8_SNORM
#define PIPE_FORMAT_I8_SNORM VIRGL_FORMAT_I8_SNORM
#define PIPE_FORMAT_A16_SNORM VIRGL_FORMAT_A16_SNORM
#define PIPE_FORMAT_L16_SNORM VIRGL_FORMAT_L16_SNORM
#define PIPE_FORMAT_L16A16_SNORM VIRGL_FORMAT_L16A16_SNORM
#define PIPE_FORMAT_I16_SNORM VIRGL_FORMAT_I16_SNORM

#define PIPE_FORMAT_A16_FLOAT VIRGL_FORMAT_A16_FLOAT
#define PIPE_FORMAT_L16_FLOAT VIRGL_FORMAT_L16_FLOAT
#define PIPE_FORMAT_L16A16_FLOAT VIRGL_FORMAT_L16A16_FLOAT
#define PIPE_FORMAT_I16_FLOAT VIRGL_FORMAT_I16_FLOAT
#define PIPE_FORMAT_A32_FLOAT VIRGL_FORMAT_A32_FLOAT
#define PIPE_FORMAT_L32_FLOAT VIRGL_FORMAT_L32_FLOAT
#define PIPE_FORMAT_L32A32_FLOAT VIRGL_FORMAT_L32A32_FLOAT
#define PIPE_FORMAT_I32_FLOAT VIRGL_FORMAT_I32_FLOAT

#define PIPE_FORMAT_YV12 VIRGL_FORMAT_YV12
#define PIPE_FORMAT_YV16 VIRGL_FORMAT_YV16
#define PIPE_FORMAT_IYUV VIRGL_FORMAT_IYUV // Also known as I420
#define PIPE_FORMAT_NV12 VIRGL_FORMAT_NV12
#define PIPE_FORMAT_NV21 VIRGL_FORMAT_NV21

#define PIPE_FORMAT_A4R4_UNORM VIRGL_FORMAT_A4R4_UNORM
#define PIPE_FORMAT_R4A4_UNORM VIRGL_FORMAT_R4A4_UNORM
#define PIPE_FORMAT_R8A8_UNORM VIRGL_FORMAT_R8A8_UNORM
#define PIPE_FORMAT_A8R8_UNORM VIRGL_FORMAT_A8R8_UNORM

#define PIPE_FORMAT_R10G10B10A2_SSCALED VIRGL_FORMAT_R10G10B10A2_SSCALED
#define PIPE_FORMAT_R10G10B10A2_SNORM VIRGL_FORMAT_R10G10B10A2_SNORM

#define PIPE_FORMAT_B10G10R10A2_USCALED VIRGL_FORMAT_B10G10R10A2_USCALED
#define PIPE_FORMAT_B10G10R10A2_SSCALED VIRGL_FORMAT_B10G10R10A2_SSCALED
#define PIPE_FORMAT_B10G10R10A2_SNORM VIRGL_FORMAT_B10G10R10A2_SNORM

#define PIPE_FORMAT_R8_UINT VIRGL_FORMAT_R8_UINT
#define PIPE_FORMAT_R8G8_UINT VIRGL_FORMAT_R8G8_UINT
#define PIPE_FORMAT_R8G8B8_UINT VIRGL_FORMAT_R8G8B8_UINT
#define PIPE_FORMAT_R8G8B8A8_UINT VIRGL_FORMAT_R8G8B8A8_UINT

#define PIPE_FORMAT_R8_SINT VIRGL_FORMAT_R8_SINT
#define PIPE_FORMAT_R8G8_SINT VIRGL_FORMAT_R8G8_SINT
#define PIPE_FORMAT_R8G8B8_SINT VIRGL_FORMAT_R8G8B8_SINT
#define PIPE_FORMAT_R8G8B8A8_SINT VIRGL_FORMAT_R8G8B8A8_SINT

#define PIPE_FORMAT_R16_UINT VIRGL_FORMAT_R16_UINT
#define PIPE_FORMAT_R16G16_UINT VIRGL_FORMAT_R16G16_UINT
#define PIPE_FORMAT_R16G16B16_UINT VIRGL_FORMAT_R16G16B16_UINT
#define PIPE_FORMAT_R16G16B16A16_UINT VIRGL_FORMAT_R16G16B16A16_UINT

#define PIPE_FORMAT_R16_SINT VIRGL_FORMAT_R16_SINT
#define PIPE_FORMAT_R16G16_SINT VIRGL_FORMAT_R16G16_SINT
#define PIPE_FORMAT_R16G16B16_SINT VIRGL_FORMAT_R16G16B16_SINT
#define PIPE_FORMAT_R16G16B16A16_SINT VIRGL_FORMAT_R16G16B16A16_SINT

#define PIPE_FORMAT_R32_UINT VIRGL_FORMAT_R32_UINT
#define PIPE_FORMAT_R32G32_UINT VIRGL_FORMAT_R32G32_UINT
#define PIPE_FORMAT_R32G32B32_UINT VIRGL_FORMAT_R32G32B32_UINT
#define PIPE_FORMAT_R32G32B32A32_UINT VIRGL_FORMAT_R32G32B32A32_UINT

#define PIPE_FORMAT_R32_SINT VIRGL_FORMAT_R32_SINT
#define PIPE_FORMAT_R32G32_SINT VIRGL_FORMAT_R32G32_SINT
#define PIPE_FORMAT_R32G32B32_SINT VIRGL_FORMAT_R32G32B32_SINT
#define PIPE_FORMAT_R32G32B32A32_SINT VIRGL_FORMAT_R32G32B32A32_SINT

#define PIPE_FORMAT_A8_UINT VIRGL_FORMAT_A8_UINT
#define PIPE_FORMAT_I8_UINT VIRGL_FORMAT_I8_UINT
#define PIPE_FORMAT_L8_UINT VIRGL_FORMAT_L8_UINT
#define PIPE_FORMAT_L8A8_UINT VIRGL_FORMAT_L8A8_UINT

#define PIPE_FORMAT_A8_SINT VIRGL_FORMAT_A8_SINT
#define PIPE_FORMAT_I8_SINT VIRGL_FORMAT_I8_SINT
#define PIPE_FORMAT_L8_SINT VIRGL_FORMAT_L8_SINT
#define PIPE_FORMAT_L8A8_SINT VIRGL_FORMAT_L8A8_SINT

#define PIPE_FORMAT_A16_UINT VIRGL_FORMAT_A16_UINT
#define PIPE_FORMAT_I16_UINT VIRGL_FORMAT_I16_UINT
#define PIPE_FORMAT_L16_UINT VIRGL_FORMAT_L16_UINT
#define PIPE_FORMAT_L16A16_UINT VIRGL_FORMAT_L16A16_UINT

#define PIPE_FORMAT_A16_SINT VIRGL_FORMAT_A16_SINT
#define PIPE_FORMAT_I16_SINT VIRGL_FORMAT_I16_SINT
#define PIPE_FORMAT_L16_SINT VIRGL_FORMAT_L16_SINT
#define PIPE_FORMAT_L16A16_SINT VIRGL_FORMAT_L16A16_SINT

#define PIPE_FORMAT_A32_UINT VIRGL_FORMAT_A32_UINT
#define PIPE_FORMAT_I32_UINT VIRGL_FORMAT_I32_UINT
#define PIPE_FORMAT_L32_UINT VIRGL_FORMAT_L32_UINT
#define PIPE_FORMAT_L32A32_UINT VIRGL_FORMAT_L32A32_UINT

#define PIPE_FORMAT_A32_SINT VIRGL_FORMAT_A32_SINT
#define PIPE_FORMAT_I32_SINT VIRGL_FORMAT_I32_SINT
#define PIPE_FORMAT_L32_SINT VIRGL_FORMAT_L32_SINT
#define PIPE_FORMAT_L32A32_SINT VIRGL_FORMAT_L32A32_SINT

#define PIPE_FORMAT_B10G10R10A2_UINT VIRGL_FORMAT_B10G10R10A2_UINT

#define PIPE_FORMAT_ETC1_RGB8 VIRGL_FORMAT_ETC1_RGB8

#define PIPE_FORMAT_R8G8_R8B8_UNORM VIRGL_FORMAT_R8G8_R8B8_UNORM
#define PIPE_FORMAT_G8R8_B8R8_UNORM VIRGL_FORMAT_G8R8_B8R8_UNORM

#define PIPE_FORMAT_R8G8B8X8_SNORM VIRGL_FORMAT_R8G8B8X8_SNORM
#define PIPE_FORMAT_R8G8B8X8_SRGB VIRGL_FORMAT_R8G8B8X8_SRGB
#define PIPE_FORMAT_R8G8B8X8_UINT VIRGL_FORMAT_R8G8B8X8_UINT
#define PIPE_FORMAT_R8G8B8X8_SINT VIRGL_FORMAT_R8G8B8X8_SINT
#define PIPE_FORMAT_B10G10R10X2_UNORM VIRGL_FORMAT_B10G10R10X2_UNORM
#define PIPE_FORMAT_R16G16B16X16_UNORM VIRGL_FORMAT_R16G16B16X16_UNORM
#define PIPE_FORMAT_R16G16B16X16_SNORM VIRGL_FORMAT_R16G16B16X16_SNORM
#define PIPE_FORMAT_R16G16B16X16_FLOAT VIRGL_FORMAT_R16G16B16X16_FLOAT
#define PIPE_FORMAT_R16G16B16X16_UINT VIRGL_FORMAT_R16G16B16X16_UINT
#define PIPE_FORMAT_R16G16B16X16_SINT VIRGL_FORMAT_R16G16B16X16_SINT
#define PIPE_FORMAT_R32G32B32X32_FLOAT VIRGL_FORMAT_R32G32B32X32_FLOAT
#define PIPE_FORMAT_R32G32B32X32_UINT VIRGL_FORMAT_R32G32B32X32_UINT
#define PIPE_FORMAT_R32G32B32X32_SINT VIRGL_FORMAT_R32G32B32X32_SINT

#define PIPE_FORMAT_R8A8_SNORM VIRGL_FORMAT_R8A8_SNORM
#define PIPE_FORMAT_R16A16_UNORM VIRGL_FORMAT_R16A16_UNORM
#define PIPE_FORMAT_R16A16_SNORM VIRGL_FORMAT_R16A16_SNORM
#define PIPE_FORMAT_R16A16_FLOAT VIRGL_FORMAT_R16A16_FLOAT
#define PIPE_FORMAT_R32A32_FLOAT VIRGL_FORMAT_R32A32_FLOAT
#define PIPE_FORMAT_R8A8_UINT VIRGL_FORMAT_R8A8_UINT
#define PIPE_FORMAT_R8A8_SINT VIRGL_FORMAT_R8A8_SINT
#define PIPE_FORMAT_R16A16_UINT VIRGL_FORMAT_R16A16_UINT
#define PIPE_FORMAT_R16A16_SINT VIRGL_FORMAT_R16A16_SINT
#define PIPE_FORMAT_R32A32_UINT VIRGL_FORMAT_R32A32_UINT
#define PIPE_FORMAT_R32A32_SINT VIRGL_FORMAT_R32A32_SINT
#define PIPE_FORMAT_R10G10B10A2_UINT VIRGL_FORMAT_R10G10B10A2_UINT

#define PIPE_FORMAT_B5G6R5_SRGB VIRGL_FORMAT_B5G6R5_SRGB

#define PIPE_FORMAT_BPTC_RGBA_UNORM VIRGL_FORMAT_BPTC_RGBA_UNORM
#define PIPE_FORMAT_BPTC_SRGBA VIRGL_FORMAT_BPTC_SRGBA
#define PIPE_FORMAT_BPTC_RGB_FLOAT VIRGL_FORMAT_BPTC_RGB_FLOAT
#define PIPE_FORMAT_BPTC_RGB_UFLOAT VIRGL_FORMAT_BPTC_RGB_UFLOAT

#define PIPE_FORMAT_A8L8_UNORM VIRGL_FORMAT_A8L8_UNORM
#define PIPE_FORMAT_A8L8_SNORM VIRGL_FORMAT_A8L8_SNORM
#define PIPE_FORMAT_A8L8_SRGB VIRGL_FORMAT_A8L8_SRGB
#define PIPE_FORMAT_A16L16_UNORM VIRGL_FORMAT_A16L16_UNORM

#define PIPE_FORMAT_G8R8_UNORM VIRGL_FORMAT_G8R8_UNORM
#define PIPE_FORMAT_G8R8_SNORM VIRGL_FORMAT_G8R8_SNORM
#define PIPE_FORMAT_G16R16_UNORM VIRGL_FORMAT_G16R16_UNORM
#define PIPE_FORMAT_G16R16_SNORM VIRGL_FORMAT_G16R16_SNORM

#define PIPE_FORMAT_A8B8G8R8_SNORM VIRGL_FORMAT_A8B8G8R8_SNORM
#define PIPE_FORMAT_X8B8G8R8_SNORM VIRGL_FORMAT_X8B8G8R8_SNORM

#define PIPE_FORMAT_ETC2_RGB8 VIRGL_FORMAT_ETC2_RGB8
#define PIPE_FORMAT_ETC2_SRGB8 VIRGL_FORMAT_ETC2_SRGB8
#define PIPE_FORMAT_ETC2_RGB8A1 VIRGL_FORMAT_ETC2_RGB8A1
#define PIPE_FORMAT_ETC2_SRGB8A1 VIRGL_FORMAT_ETC2_SRGB8A1
#define PIPE_FORMAT_ETC2_RGBA8 VIRGL_FORMAT_ETC2_RGBA8
#define PIPE_FORMAT_ETC2_SRGBA8 VIRGL_FORMAT_ETC2_SRGBA8
#define PIPE_FORMAT_ETC2_R11_UNORM VIRGL_FORMAT_ETC2_R11_UNORM
#define PIPE_FORMAT_ETC2_R11_SNORM VIRGL_FORMAT_ETC2_R11_SNORM
#define PIPE_FORMAT_ETC2_RG11_UNORM VIRGL_FORMAT_ETC2_RG11_UNORM
#define PIPE_FORMAT_ETC2_RG11_SNORM VIRGL_FORMAT_ETC2_RG11_SNORM

#define PIPE_FORMAT_ASTC_4x4 VIRGL_FORMAT_ASTC_4x4
#define PIPE_FORMAT_ASTC_5x4 VIRGL_FORMAT_ASTC_5x4
#define PIPE_FORMAT_ASTC_5x5 VIRGL_FORMAT_ASTC_5x5
#define PIPE_FORMAT_ASTC_6x5 VIRGL_FORMAT_ASTC_6x5
#define PIPE_FORMAT_ASTC_6x6 VIRGL_FORMAT_ASTC_6x6
#define PIPE_FORMAT_ASTC_8x5 VIRGL_FORMAT_ASTC_8x5
#define PIPE_FORMAT_ASTC_8x6 VIRGL_FORMAT_ASTC_8x6
#define PIPE_FORMAT_ASTC_8x8 VIRGL_FORMAT_ASTC_8x8
#define PIPE_FORMAT_ASTC_10x5 VIRGL_FORMAT_ASTC_10x5
#define PIPE_FORMAT_ASTC_10x6 VIRGL_FORMAT_ASTC_10x6
#define PIPE_FORMAT_ASTC_10x8 VIRGL_FORMAT_ASTC_10x8
#define PIPE_FORMAT_ASTC_10x10 VIRGL_FORMAT_ASTC_10x10
#define PIPE_FORMAT_ASTC_12x10 VIRGL_FORMAT_ASTC_12x10
#define PIPE_FORMAT_ASTC_12x12 VIRGL_FORMAT_ASTC_12x12

#define PIPE_FORMAT_ASTC_4x4_SRGB VIRGL_FORMAT_ASTC_4x4_SRGB
#define PIPE_FORMAT_ASTC_5x4_SRGB VIRGL_FORMAT_ASTC_5x4_SRGB
#define PIPE_FORMAT_ASTC_5x5_SRGB VIRGL_FORMAT_ASTC_5x5_SRGB
#define PIPE_FORMAT_ASTC_6x5_SRGB VIRGL_FORMAT_ASTC_6x5_SRGB
#define PIPE_FORMAT_ASTC_6x6_SRGB VIRGL_FORMAT_ASTC_6x6_SRGB
#define PIPE_FORMAT_ASTC_8x5_SRGB VIRGL_FORMAT_ASTC_8x5_SRGB
#define PIPE_FORMAT_ASTC_8x6_SRGB VIRGL_FORMAT_ASTC_8x6_SRGB
#define PIPE_FORMAT_ASTC_8x8_SRGB VIRGL_FORMAT_ASTC_8x8_SRGB
#define PIPE_FORMAT_ASTC_10x5_SRGB VIRGL_FORMAT_ASTC_10x5_SRGB
#define PIPE_FORMAT_ASTC_10x6_SRGB VIRGL_FORMAT_ASTC_10x6_SRGB
#define PIPE_FORMAT_ASTC_10x8_SRGB VIRGL_FORMAT_ASTC_10x8_SRGB
#define PIPE_FORMAT_ASTC_10x10_SRGB VIRGL_FORMAT_ASTC_10x10_SRGB
#define PIPE_FORMAT_ASTC_12x10_SRGB VIRGL_FORMAT_ASTC_12x10_SRGB
#define PIPE_FORMAT_ASTC_12x12_SRGB VIRGL_FORMAT_ASTC_12x12_SRGB

#define PIPE_FORMAT_P016 VIRGL_FORMAT_P016

#define PIPE_FORMAT_R10G10B10X2_UNORM VIRGL_FORMAT_R10G10B10X2_UNORM
#define PIPE_FORMAT_A1B5G5R5_UNORM VIRGL_FORMAT_A1B5G5R5_UNORM
#define PIPE_FORMAT_X1B5G5R5_UNORM VIRGL_FORMAT_X1B5G5R5_UNORM
#define PIPE_FORMAT_A4B4G4R4_UNORM VIRGL_FORMAT_A4B4G4R4_UNORM

#define PIPE_FORMAT_R8_SRGB VIRGL_FORMAT_R8_SRGB

#define PIPE_FORMAT_COUNT VIRGL_FORMAT_MAX

#define PIPE_FORMAT_RGBA8888_UNORM PIPE_FORMAT_R8G8B8A8_UNORM
#define PIPE_FORMAT_RGBX8888_UNORM PIPE_FORMAT_R8G8B8X8_UNORM
#define PIPE_FORMAT_BGRA8888_UNORM PIPE_FORMAT_B8G8R8A8_UNORM
#define PIPE_FORMAT_BGRX8888_UNORM PIPE_FORMAT_B8G8R8X8_UNORM
#define PIPE_FORMAT_ARGB8888_UNORM PIPE_FORMAT_A8R8G8B8_UNORM
#define PIPE_FORMAT_XRGB8888_UNORM PIPE_FORMAT_X8R8G8B8_UNORM
#define PIPE_FORMAT_ABGR8888_UNORM PIPE_FORMAT_A8B8G8R8_UNORM
#define PIPE_FORMAT_XBGR8888_UNORM PIPE_FORMAT_X8B8G8R8_UNORM
#define PIPE_FORMAT_RGBA8888_SNORM PIPE_FORMAT_R8G8B8A8_SNORM
#define PIPE_FORMAT_RGBX8888_SNORM PIPE_FORMAT_R8G8B8X8_SNORM
#define PIPE_FORMAT_ABGR8888_SNORM PIPE_FORMAT_A8B8G8R8_SNORM
#define PIPE_FORMAT_XBGR8888_SNORM PIPE_FORMAT_X8B8G8R8_SNORM
#define PIPE_FORMAT_RGBA8888_SRGB PIPE_FORMAT_R8G8B8A8_SRGB
#define PIPE_FORMAT_RGBX8888_SRGB PIPE_FORMAT_R8G8B8X8_SRGB
#define PIPE_FORMAT_BGRA8888_SRGB PIPE_FORMAT_B8G8R8A8_SRGB
#define PIPE_FORMAT_BGRX8888_SRGB PIPE_FORMAT_B8G8R8X8_SRGB
#define PIPE_FORMAT_ARGB8888_SRGB PIPE_FORMAT_A8R8G8B8_SRGB
#define PIPE_FORMAT_XRGB8888_SRGB PIPE_FORMAT_X8R8G8B8_SRGB
#define PIPE_FORMAT_ABGR8888_SRGB PIPE_FORMAT_A8B8G8R8_SRGB
#define PIPE_FORMAT_XBGR8888_SRGB PIPE_FORMAT_X8B8G8R8_SRGB
#define PIPE_FORMAT_LA88_UNORM PIPE_FORMAT_L8A8_UNORM
#define PIPE_FORMAT_AL88_UNORM PIPE_FORMAT_A8L8_UNORM
#define PIPE_FORMAT_LA88_SNORM PIPE_FORMAT_L8A8_SNORM
#define PIPE_FORMAT_AL88_SNORM PIPE_FORMAT_A8L8_SNORM
#define PIPE_FORMAT_LA88_SRGB PIPE_FORMAT_L8A8_SRGB
#define PIPE_FORMAT_AL88_SRGB PIPE_FORMAT_A8L8_SRGB
#define PIPE_FORMAT_LA1616_UNORM PIPE_FORMAT_L16A16_UNORM
#define PIPE_FORMAT_AL1616_UNORM PIPE_FORMAT_A16L16_UNORM
#define PIPE_FORMAT_RG88_UNORM PIPE_FORMAT_R8G8_UNORM
#define PIPE_FORMAT_GR88_UNORM PIPE_FORMAT_G8R8_UNORM
#define PIPE_FORMAT_RG88_SNORM PIPE_FORMAT_R8G8_SNORM
#define PIPE_FORMAT_GR88_SNORM PIPE_FORMAT_G8R8_SNORM
#define PIPE_FORMAT_RG1616_UNORM PIPE_FORMAT_R16G16_UNORM
#define PIPE_FORMAT_GR1616_UNORM PIPE_FORMAT_G16R16_UNORM
#define PIPE_FORMAT_RG1616_SNORM PIPE_FORMAT_R16G16_SNORM
#define PIPE_FORMAT_GR1616_SNORM PIPE_FORMAT_G16R16_SNORM

enum pipe_video_chroma_format {
   PIPE_VIDEO_CHROMA_FORMAT_400,
   PIPE_VIDEO_CHROMA_FORMAT_420,
   PIPE_VIDEO_CHROMA_FORMAT_422,
   PIPE_VIDEO_CHROMA_FORMAT_444,
   PIPE_VIDEO_CHROMA_FORMAT_NONE
};

enum pipe_texture_target {
   PIPE_BUFFER = 0,
   PIPE_TEXTURE_1D = 1,
   PIPE_TEXTURE_2D = 2,
   PIPE_TEXTURE_3D = 3,
   PIPE_TEXTURE_CUBE = 4,
   PIPE_TEXTURE_RECT = 5,
   PIPE_TEXTURE_1D_ARRAY = 6,
   PIPE_TEXTURE_2D_ARRAY = 7,
   PIPE_TEXTURE_CUBE_ARRAY = 8,
   PIPE_MAX_TEXTURE_TYPES
};


/**
 * Transfer object usage flags
 */
enum pipe_transfer_usage {
   /**
    * Resource contents read back (or accessed directly) at transfer
    * create time.
    */
   PIPE_TRANSFER_READ = (1 << 0),
   
   /**
    * Resource contents will be written back at transfer_unmap
    * time (or modified as a result of being accessed directly).
    */
   PIPE_TRANSFER_WRITE = (1 << 1),

   /**
    * Read/modify/write
    */
   PIPE_TRANSFER_READ_WRITE = PIPE_TRANSFER_READ | PIPE_TRANSFER_WRITE,

   /** 
    * The transfer should map the texture storage directly. The driver may
    * return NULL if that isn't possible, and the state tracker needs to cope
    * with that and use an alternative path without this flag.
    *
    * E.g. the state tracker could have a simpler path which maps textures and
    * does read/modify/write cycles on them directly, and a more complicated
    * path which uses minimal read and write transfers.
    */
   PIPE_TRANSFER_MAP_DIRECTLY = (1 << 2),

   /**
    * Discards the memory within the mapped region.
    *
    * It should not be used with PIPE_TRANSFER_READ.
    *
    * See also:
    * - OpenGL's ARB_map_buffer_range extension, MAP_INVALIDATE_RANGE_BIT flag.
    */
   PIPE_TRANSFER_DISCARD_RANGE = (1 << 8),

   /**
    * Fail if the resource cannot be mapped immediately.
    *
    * See also:
    * - Direct3D's D3DLOCK_DONOTWAIT flag.
    * - Mesa3D's MESA_MAP_NOWAIT_BIT flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.DonotWait flag.
    */
   PIPE_TRANSFER_DONTBLOCK = (1 << 9),

   /**
    * Do not attempt to synchronize pending operations on the resource when mapping.
    *
    * It should not be used with PIPE_TRANSFER_READ.
    *
    * See also:
    * - OpenGL's ARB_map_buffer_range extension, MAP_UNSYNCHRONIZED_BIT flag.
    * - Direct3D's D3DLOCK_NOOVERWRITE flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.IgnoreSync flag.
    */
   PIPE_TRANSFER_UNSYNCHRONIZED = (1 << 10),

   /**
    * Written ranges will be notified later with
    * pipe_context::transfer_flush_region.
    *
    * It should not be used with PIPE_TRANSFER_READ.
    *
    * See also:
    * - pipe_context::transfer_flush_region
    * - OpenGL's ARB_map_buffer_range extension, MAP_FLUSH_EXPLICIT_BIT flag.
    */
   PIPE_TRANSFER_FLUSH_EXPLICIT = (1 << 11),

   /**
    * Discards all memory backing the resource.
    *
    * It should not be used with PIPE_TRANSFER_READ.
    *
    * This is equivalent to:
    * - OpenGL's ARB_map_buffer_range extension, MAP_INVALIDATE_BUFFER_BIT
    * - BufferData(NULL) on a GL buffer
    * - Direct3D's D3DLOCK_DISCARD flag.
    * - WDDM's D3DDDICB_LOCKFLAGS.Discard flag.
    * - D3D10 DDI's D3D10_DDI_MAP_WRITE_DISCARD flag
    * - D3D10's D3D10_MAP_WRITE_DISCARD flag.
    */
   PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE = (1 << 12),

   /**
    * Allows the resource to be used for rendering while mapped.
    *
    * PIPE_RESOURCE_FLAG_MAP_PERSISTENT must be set when creating
    * the resource.
    *
    * If COHERENT is not set, memory_barrier(PIPE_BARRIER_MAPPED_BUFFER)
    * must be called to ensure the device can see what the CPU has written.
    */
   PIPE_TRANSFER_PERSISTENT = (1 << 13),

   /**
    * If PERSISTENT is set, this ensures any writes done by the device are
    * immediately visible to the CPU and vice versa.
    *
    * PIPE_RESOURCE_FLAG_MAP_COHERENT must be set when creating
    * the resource.
    */
   PIPE_TRANSFER_COHERENT = (1 << 14)
};

#define PIPE_BIND_DEPTH_STENCIL        (1 << 0) /* create_surface */
#define PIPE_BIND_RENDER_TARGET        (1 << 1) /* create_surface */
#define PIPE_BIND_BLENDABLE            (1 << 2) /* create_surface */
#define PIPE_BIND_SAMPLER_VIEW         (1 << 3) /* create_sampler_view */
#define PIPE_BIND_VERTEX_BUFFER        (1 << 4) /* set_vertex_buffers */
#define PIPE_BIND_INDEX_BUFFER         (1 << 5) /* draw_elements */
#define PIPE_BIND_CONSTANT_BUFFER      (1 << 6) /* set_constant_buffer */
#define PIPE_BIND_DISPLAY_TARGET       (1 << 8) /* flush_front_buffer */
#define PIPE_BIND_TRANSFER_WRITE       (1 << 9) /* transfer_map */
#define PIPE_BIND_TRANSFER_READ        (1 << 10) /* transfer_map */
#define PIPE_BIND_STREAM_OUTPUT        (1 << 11) /* set_stream_output_buffers */
#define PIPE_BIND_CURSOR               (1 << 16) /* mouse cursor */
#define PIPE_BIND_CUSTOM               (1 << 17) /* state-tracker/winsys usages */
#define PIPE_BIND_GLOBAL               (1 << 18) /* set_global_binding */
#define PIPE_BIND_SHADER_RESOURCE      (1 << 19) /* set_shader_resources */
#define PIPE_BIND_COMPUTE_RESOURCE     (1 << 20) /* set_compute_resources */
#define PIPE_BIND_COMMAND_ARGS_BUFFER  (1 << 21) /* pipe_draw_info.indirect */
#define PIPE_BIND_QUERY_BUFFER         (1 << 22) /* get_query_result_resource */

/* The first two flags above were previously part of the amorphous
 * TEXTURE_USAGE, most of which are now descriptions of the ways a
 * particular texture can be bound to the gallium pipeline.  The two flags
 * below do not fit within that and probably need to be migrated to some
 * other place.
 *
 * It seems like scanout is used by the Xorg state tracker to ask for
 * a texture suitable for actual scanout (hence the name), which
 * implies extra layout constraints on some hardware.  It may also
 * have some special meaning regarding mouse cursor images.
 *
 * The shared flag is quite underspecified, but certainly isn't a
 * binding flag - it seems more like a message to the winsys to create
 * a shareable allocation.
 * 
 * The third flag has been added to be able to force textures to be created
 * in linear mode (no tiling).
 */
#define PIPE_BIND_SCANOUT     (1 << 14) /*  */
#define PIPE_BIND_SHARED      (1 << 15) /* get_texture_handle ??? */
#define PIPE_BIND_LINEAR      (1 << 21)

enum pipe_error {
   PIPE_OK = 0,
   PIPE_ERROR = -1,    /**< Generic error */
   PIPE_ERROR_BAD_INPUT = -2,
   PIPE_ERROR_OUT_OF_MEMORY = -3,
   PIPE_ERROR_RETRY = -4
   /* TODO */
};


#define PIPE_BLENDFACTOR_ONE                 0x1
#define PIPE_BLENDFACTOR_SRC_COLOR           0x2
#define PIPE_BLENDFACTOR_SRC_ALPHA           0x3
#define PIPE_BLENDFACTOR_DST_ALPHA           0x4
#define PIPE_BLENDFACTOR_DST_COLOR           0x5
#define PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE  0x6
#define PIPE_BLENDFACTOR_CONST_COLOR         0x7
#define PIPE_BLENDFACTOR_CONST_ALPHA         0x8
#define PIPE_BLENDFACTOR_SRC1_COLOR          0x9
#define PIPE_BLENDFACTOR_SRC1_ALPHA          0x0A
#define PIPE_BLENDFACTOR_ZERO                0x11
#define PIPE_BLENDFACTOR_INV_SRC_COLOR       0x12
#define PIPE_BLENDFACTOR_INV_SRC_ALPHA       0x13
#define PIPE_BLENDFACTOR_INV_DST_ALPHA       0x14
#define PIPE_BLENDFACTOR_INV_DST_COLOR       0x15
#define PIPE_BLENDFACTOR_INV_CONST_COLOR     0x17
#define PIPE_BLENDFACTOR_INV_CONST_ALPHA     0x18
#define PIPE_BLENDFACTOR_INV_SRC1_COLOR      0x19
#define PIPE_BLENDFACTOR_INV_SRC1_ALPHA      0x1A

#define PIPE_BLEND_ADD               0
#define PIPE_BLEND_SUBTRACT          1
#define PIPE_BLEND_REVERSE_SUBTRACT  2
#define PIPE_BLEND_MIN               3
#define PIPE_BLEND_MAX               4


enum pipe_logicop {
   PIPE_LOGICOP_CLEAR,
   PIPE_LOGICOP_NOR,
   PIPE_LOGICOP_AND_INVERTED,
   PIPE_LOGICOP_COPY_INVERTED,
   PIPE_LOGICOP_AND_REVERSE,
   PIPE_LOGICOP_INVERT,
   PIPE_LOGICOP_XOR,
   PIPE_LOGICOP_NAND,
   PIPE_LOGICOP_AND,
   PIPE_LOGICOP_EQUIV,
   PIPE_LOGICOP_NOOP,
   PIPE_LOGICOP_OR_INVERTED,
   PIPE_LOGICOP_COPY,
   PIPE_LOGICOP_OR_REVERSE,
   PIPE_LOGICOP_OR,
   PIPE_LOGICOP_SET,
};

#define PIPE_MASK_R  0x1
#define PIPE_MASK_G  0x2
#define PIPE_MASK_B  0x4
#define PIPE_MASK_A  0x8
#define PIPE_MASK_RGBA 0xf
#define PIPE_MASK_Z  0x10
#define PIPE_MASK_S  0x20
#define PIPE_MASK_ZS 0x30
#define PIPE_MASK_RGBAZS (PIPE_MASK_RGBA|PIPE_MASK_ZS)


/**
 * Inequality functions.  Used for depth test, stencil compare, alpha
 * test, shadow compare, etc.
 */
#define PIPE_FUNC_NEVER    0
#define PIPE_FUNC_LESS     1
#define PIPE_FUNC_EQUAL    2
#define PIPE_FUNC_LEQUAL   3
#define PIPE_FUNC_GREATER  4
#define PIPE_FUNC_NOTEQUAL 5
#define PIPE_FUNC_GEQUAL   6
#define PIPE_FUNC_ALWAYS   7

/** Polygon fill mode */
#define PIPE_POLYGON_MODE_FILL  0
#define PIPE_POLYGON_MODE_LINE  1
#define PIPE_POLYGON_MODE_POINT 2

/** Polygon face specification, eg for culling */
#define PIPE_FACE_NONE           0
#define PIPE_FACE_FRONT          1
#define PIPE_FACE_BACK           2
#define PIPE_FACE_FRONT_AND_BACK (PIPE_FACE_FRONT | PIPE_FACE_BACK)

/** Stencil ops */
#define PIPE_STENCIL_OP_KEEP       0
#define PIPE_STENCIL_OP_ZERO       1
#define PIPE_STENCIL_OP_REPLACE    2
#define PIPE_STENCIL_OP_INCR       3
#define PIPE_STENCIL_OP_DECR       4
#define PIPE_STENCIL_OP_INCR_WRAP  5
#define PIPE_STENCIL_OP_DECR_WRAP  6
#define PIPE_STENCIL_OP_INVERT     7

/** Texture types.
 * See the documentation for info on PIPE_TEXTURE_RECT vs PIPE_TEXTURE_2D */
enum pipe_texture_target {
   PIPE_BUFFER           = 0,
   PIPE_TEXTURE_1D       = 1,
   PIPE_TEXTURE_2D       = 2,
   PIPE_TEXTURE_3D       = 3,
   PIPE_TEXTURE_CUBE     = 4,
   PIPE_TEXTURE_RECT     = 5,
   PIPE_TEXTURE_1D_ARRAY = 6,
   PIPE_TEXTURE_2D_ARRAY = 7,
   PIPE_TEXTURE_CUBE_ARRAY = 8,
   PIPE_MAX_TEXTURE_TYPES
};

#define PIPE_TEX_FACE_POS_X 0
#define PIPE_TEX_FACE_NEG_X 1
#define PIPE_TEX_FACE_POS_Y 2
#define PIPE_TEX_FACE_NEG_Y 3
#define PIPE_TEX_FACE_POS_Z 4
#define PIPE_TEX_FACE_NEG_Z 5
#define PIPE_TEX_FACE_MAX   6

#define PIPE_TEX_WRAP_REPEAT                   0
#define PIPE_TEX_WRAP_CLAMP                    1
#define PIPE_TEX_WRAP_CLAMP_TO_EDGE            2
#define PIPE_TEX_WRAP_CLAMP_TO_BORDER          3
#define PIPE_TEX_WRAP_MIRROR_REPEAT            4
#define PIPE_TEX_WRAP_MIRROR_CLAMP             5
#define PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE     6
#define PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER   7

/* Between mipmaps, ie mipfilter
 */
#define PIPE_TEX_MIPFILTER_NEAREST  0
#define PIPE_TEX_MIPFILTER_LINEAR   1
#define PIPE_TEX_MIPFILTER_NONE     2

/* Within a mipmap, ie min/mag filter 
 */
#define PIPE_TEX_FILTER_NEAREST      0
#define PIPE_TEX_FILTER_LINEAR       1

#define PIPE_TEX_COMPARE_NONE          0
#define PIPE_TEX_COMPARE_R_TO_TEXTURE  1

/**
 * Clear buffer bits
 */
#define PIPE_CLEAR_DEPTH        (1 << 0)
#define PIPE_CLEAR_STENCIL      (1 << 1)
#define PIPE_CLEAR_COLOR0       (1 << 2)
#define PIPE_CLEAR_COLOR1       (1 << 3)
#define PIPE_CLEAR_COLOR2       (1 << 4)
#define PIPE_CLEAR_COLOR3       (1 << 5)
#define PIPE_CLEAR_COLOR4       (1 << 6)
#define PIPE_CLEAR_COLOR5       (1 << 7)
#define PIPE_CLEAR_COLOR6       (1 << 8)
#define PIPE_CLEAR_COLOR7       (1 << 9)
/** Combined flags */
/** All color buffers currently bound */
#define PIPE_CLEAR_COLOR        (PIPE_CLEAR_COLOR0 | PIPE_CLEAR_COLOR1 | \
                                 PIPE_CLEAR_COLOR2 | PIPE_CLEAR_COLOR3 | \
                                 PIPE_CLEAR_COLOR4 | PIPE_CLEAR_COLOR5 | \
                                 PIPE_CLEAR_COLOR6 | PIPE_CLEAR_COLOR7)
#define PIPE_CLEAR_DEPTHSTENCIL (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)

// Pyrion
#include <inc/drivers/gpu/apis/pyrion.h>

aos_bool pyrion_init_virtio(void) __attribute__((used));
void pyrion_finish_virtio(void) __attribute__((used));
struct pyrion_ctx* pyrion_create_ctx_virtio(struct pyrion_create_ctx_info ctx_info) __attribute__((used));
void pyrion_destroy_ctx_virtio(struct pyrion_ctx* ctx) __attribute__((used));
void pyrion_unuse_device_virtio(struct pyrion_ctx* ctx) __attribute__((used));
aos_bool pyrion_enumerate_physical_devices_virtio(size_t* count, size_t idx, struct pyrion_physical_device* out) __attribute__((used));
aos_bool pyrion_use_device_virtio(struct pyrion_ctx* ctx, struct pyrion_physical_device* dev) __attribute__((used));
aos_bool pyrion_submit_cmd_stream_virtio(struct pyrion_ctx* ctx, struct pyrion_cmd_stream* stream) __attribute__((used));
aos_bool pyrion_viewport_virtio(struct pyrion_ctx* ctx, struct pyrion_rect viewport) __attribute__((used));
aos_bool pyrion_flush_virtio(struct pyrion_ctx* ctx) __attribute__((used));
aos_bool pyrion_clear_virtio(struct pyrion_ctx* ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) __attribute__((used));
aos_bool pyrion_pixel_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) __attribute__((used));
aos_bool pyrion_draw_char_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t codepoint, struct pyrion_font* font, struct pyrion_glyph* glyph) __attribute__((used));
void pyrion_destroy_font_virtio(struct pyrion_ctx* ctx, struct pyrion_font* font) __attribute__((used));
aos_bool pyrion_upload_font_virtio(struct pyrion_ctx* ctx, struct pyrion_font font, struct pyrion_font* out) __attribute__((used));
aos_bool pyrion_rect_virtio(struct pyrion_ctx* ctx, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) __attribute__((used));
aos_bool pyrion_blit_virtio(struct pyrion_ctx *ctx, uint32_t dst_res, uint32_t src_res, uint32_t width, uint32_t height) __attribute__((used));
