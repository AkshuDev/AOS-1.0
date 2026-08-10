#include <aos_inttypes.h>
#include <asm.h>

#include <limits.h>

#include <inc/core/kfuncs.h>
#include <inc/mm/avmf.h>
#include <inc/mm/pager.h>

#include <inc/aml/aml_parser.h>

typedef struct {
    uint8_t* start;
    uint8_t* ptr;
    uint8_t* end;
} aml_stream_t;

typedef struct {
    uint8_t arg_count;

    uint8_t* start;
    uint8_t* end;
} aml_method_t;

typedef struct {
    uint8_t* pc;
    uint8_t* end;

    struct aml_object* args[7];
    struct aml_object* locals[8];
    struct aml_object* stack[256];
    int stack_top;
} aml_frame_t;

enum aml_object_type {
    AML_OBJ_UNINITIALIZED,
    AML_OBJ_INTEGER,
    AML_OBJ_STRING,
    AML_OBJ_BUFFER,
    AML_OBJ_PACKAGE,
    AML_OBJ_METHOD,
    AML_OBJ_DEVICE,
    AML_OBJ_SCOPE,
    AML_OBJ_REGION,
    AML_OBJ_FIELD,
    AML_OBJ_REFERENCE
};

typedef struct aml_object {
    enum aml_object_type type;

    union {
        uint64_t integer;
        struct {
            char* ptr;
            size_t length;
        } string;
        aml_method_t* method;
    };
} aml_object_t;

enum aml_node_type_t {
	AML_NODE_NULL = 0,

	AML_NODE_ROOT,

	AML_NODE_NAME_SEG,
	AML_NODE_DUAL_NAME_SEG,
	AML_NODE_MULTI_NAME_SEG,
	AML_NODE_NULL_NAME,

    AML_NODE_INTEGER,
    AML_NODE_STRING,
};

typedef struct aml_node {
	enum aml_node_type_t type;
    
	union {
		struct {
            char* ptr;
            size_t length;
        } string;

		struct {
			uint64_t v;
			uint8_t size_in_bytes;
		} integer;

		struct {
			uint8_t count;
			char (*segments)[4];
		} multi_name_seg;

		char name_seg[5];
		char dual_name_seg[9];
	};

    struct aml_node* parent;
    struct aml_node* child;
    struct aml_node* sibling;

    aml_object_t* object;
} aml_node_t;

#define OBJECTS_PER_PAGE (PAGE_SIZE / sizeof(aml_object_t))
#define NODES_PER_PAGE (PAGE_SIZE / sizeof(aml_node_t))

static aml_object_t* object_pool;
static uint64_t object_pool_cap;
static uint64_t object_pool_count;

static aml_node_t* node_pool;
static uint64_t node_pool_cap;
static uint64_t node_pool_count;

static uint8_t aml_peek(aml_stream_t* s, aos_bool* valid) {
	if (!s) {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
	if (s->ptr > s->end) {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
	*valid = AOS_TRUE;
    return *s->ptr;
}

static uint8_t aml_next(aml_stream_t* s, aos_bool* valid) {
	if (!s) {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
	if (s->ptr >= s->end) {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}

    *valid = AOS_TRUE;
    return *s->ptr++;
}

static aos_bool aml_eof(aml_stream_t* s, aos_bool* valid) {
	if (!s) {
		if (valid) *valid = AOS_FALSE;
		return AOS_TRUE; // Use as EOF to prevent program continueing
	}
	if (valid) *valid = AOS_TRUE;
    return s->ptr >= s->end;
}

static void aml_push(aml_frame_t* frame, aml_object_t* obj, aos_bool* valid) {
	if (!frame || !obj) goto error;
	if (frame->stack_top >= 256) goto error;

	if (valid) *valid = AOS_TRUE;
    frame->stack[frame->stack_top++] = obj;
	return;

	error: {
		if (valid) *valid = AOS_FALSE;
		return;
	}
}

static struct aml_object* aml_pop(aml_frame_t* frame, aos_bool* valid) {
	if (!frame) goto error;
	if (valid) *valid = AOS_TRUE;

	if (frame->stack_top == 0) return NULL;
    return frame->stack[--frame->stack_top];

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_object_t* get_new_object(void) {
	if (!object_pool) {
		object_pool = (aml_object_t*)avmf_alloc(sizeof(aml_object_t)*OBJECTS_PER_PAGE, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
		if (!object_pool) return NULL;
		object_pool_cap = OBJECTS_PER_PAGE;
		object_pool_count = 0;
	} else {
		if (object_pool_cap < object_pool_count) object_pool_count = object_pool_cap;
		if (object_pool_count >= object_pool_cap) {
			aml_object_t* nptr = (aml_object_t*)avmf_alloc(sizeof(aml_object_t)*(object_pool_cap + OBJECTS_PER_PAGE), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
			if (!nptr) return NULL;
			memcpy(nptr, object_pool, sizeof(aml_object_t)*object_pool_count);
			avmf_free((uint64_t)object_pool);
			object_pool = nptr;
			object_pool_cap += OBJECTS_PER_PAGE;
		}
	}

	return &object_pool[object_pool_count++];
}

static aml_node_t* get_new_node(void) {
	if (!node_pool) {
		node_pool = (aml_node_t*)avmf_alloc(sizeof(aml_node_t)*NODES_PER_PAGE, MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
		if (!node_pool) return NULL;
		node_pool_cap = NODES_PER_PAGE;
		node_pool_count = 0;
	} else {
		if (node_pool_cap < node_pool_count) node_pool_count = node_pool_cap;
		if (node_pool_count >= node_pool_cap) {
			aml_node_t* nptr = (aml_node_t*)avmf_alloc(sizeof(aml_node_t)*(node_pool_cap + NODES_PER_PAGE), MALLOC_TYPE_KERNEL, AVMF_FLAG_RW, NULL);
			if (!nptr) return NULL;
			memcpy(nptr, node_pool, sizeof(aml_node_t)*node_pool_count);
			avmf_free((uint64_t)node_pool);
			node_pool = nptr;
			node_pool_cap += NODES_PER_PAGE;
		}
	}

	return &node_pool[node_pool_count++];
}

static aos_bool aml_is_lead_name_char(uint8_t c) {
	return (c >= 'A' && c <= 'Z') || c == '_';
}

static aos_bool aml_is_name_char(uint8_t c) {
	return aml_is_lead_name_char(c) || (c >= '0' && c <= '9');
}


static uint32_t aml_parse_pkglen(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;
    uint8_t lead = aml_next(s, &v);
	if (!v) goto error;

    uint8_t bytes_follow = lead >> 6;
    if (bytes_follow == 0) {
		if (valid) *valid = AOS_TRUE;
		return lead & 0b00111111;
	}
	else if (bytes_follow > 3) goto error;
	if (((lead >> 4) & 0b11) != 0) goto error;

    uint32_t length = lead & 0b00001111;
    for (uint8_t i = 0; i < bytes_follow; i++) {
		uint8_t val =  aml_next(s, &v);
		if (!v) goto error;

        length |= ((uint32_t)val) << (4 + 8 * i);
    }

	if (valid) *valid = AOS_TRUE;
    return length;

	error: {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
}

static uint8_t aml_parse_byte(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	return aml_next(s, valid);
	error: {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
}

static uint16_t aml_parse_word(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;

	uint16_t value = aml_next(s, &v);
	if (!v) goto error;

	value |= ((uint16_t)aml_next(s, &v)) << 8;
	if (!v) goto error;

	if (valid) *valid = AOS_TRUE;
	return value;

	error: {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
}

static uint32_t aml_parse_dword(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;

	uint32_t value = aml_next(s, &v);
	if (!v) goto error;

	value |= ((uint32_t)aml_next(s, &v)) << 8;
	if (!v) goto error;

	value |= ((uint32_t)aml_next(s, &v)) << 16;
	if (!v) goto error;

	value |= ((uint32_t)aml_next(s, &v)) << 24;
	if (!v) goto error;

	if (valid) *valid = AOS_TRUE;
	return value;

	error: {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
}

static uint64_t aml_parse_qword(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;

	uint64_t value = aml_next(s, &v);
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 8;
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 16;
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 24;
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 32;
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 40;
	if (!v) goto error;

	value |= ((uint64_t)aml_next(s, &v)) << 48;
	if (!v) goto error;
	
	value |= ((uint64_t)aml_next(s, &v)) << 56;
	if (!v) goto error;

	if (valid) *valid = AOS_TRUE;
	return value;

	error: {
		if (valid) *valid = AOS_FALSE;
		return 0;
	}
}

static aml_node_t* aml_parse_zero_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_ZERO_OP || !prefix_valid) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = 0;
	n->integer.size_in_bytes = sizeof(uint8_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_one_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_ONE_OP || !prefix_valid) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = 1;
	n->integer.size_in_bytes = sizeof(uint8_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_ones_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_ONES_OP || !prefix_valid) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = UINT64_MAX;
	n->integer.size_in_bytes = sizeof(uint8_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_byte_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_BYTE_PREFIX || !prefix_valid) goto error;

	aos_bool v = AOS_TRUE;
	uint8_t value = aml_parse_byte(s, &v);
	if (!v) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = value;
	n->integer.size_in_bytes = sizeof(uint8_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_word_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_WORD_PREFIX || !prefix_valid) goto error;

	aos_bool v = AOS_TRUE;
	uint16_t value = aml_parse_word(s, &v);
	if (!v) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = value;
	n->integer.size_in_bytes = sizeof(uint16_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_dword_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_DWORD_PREFIX || !prefix_valid) goto error;

	aos_bool v = AOS_TRUE;
	uint32_t value = aml_parse_dword(s, &v);
	if (!v) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = value;
	n->integer.size_in_bytes = sizeof(uint32_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_qword_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_QWORD_PREFIX || !prefix_valid) goto error;

	aos_bool v = AOS_TRUE;
	uint64_t value = aml_parse_qword(s, &v);
	if (!v) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_INTEGER;
	n->integer.v = value;
	n->integer.size_in_bytes = sizeof(uint64_t);

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_string_const(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_STRING_PREFIX || !prefix_valid) goto error;

	size_t len = 0;
	uint8_t* start = s->ptr;
	aos_bool terminated = AOS_FALSE;

	while (s->ptr < s->end) {
		aos_bool v = AOS_TRUE;
        uint8_t c = aml_next(s, &v);
        if (!v) goto error;

        if (c == 0x00) {
			terminated = AOS_TRUE;
			break;
		}
        if (c < 0x01 || c > 0x7F) goto error;
    }
	if (!terminated) goto error;
	len = (size_t)(s->ptr - start - 1);

	aml_node_t* n = get_new_node();
	if (!n) goto error;

	n->type = AML_NODE_STRING;
	n->string.ptr = (char*)start;
	n->string.length = len;

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_nameseg(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;
	
	aos_bool v = AOS_TRUE;
	uint8_t c0 = aml_next(s, &v);
	if (!v || !aml_is_lead_name_char(c0)) goto error;
	
	uint8_t c1 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c1)) goto error;
	
	uint8_t c2 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c2)) goto error;
	
	uint8_t c3 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c3)) goto error;
	
	aml_node_t* n = get_new_node();
	if (!n) goto error;
	
	n->type = AML_NODE_NAME_SEG;
	n->name_seg[0] = (char)c0;
	n->name_seg[1] = (char)c1;
	n->name_seg[2] = (char)c2;
	n->name_seg[3] = (char)c3;
	n->name_seg[4] = '\0';
	
	if (valid) *valid = AOS_TRUE;
	return n;
	
	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_dual_namepath(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_DUAL_NAME_PREFIX || !prefix_valid) goto error;
	
	aos_bool v = AOS_TRUE;
	uint8_t c0 = aml_next(s, &v);
	if (!v || !aml_is_lead_name_char(c0)) goto error;
	
	uint8_t c1 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c1)) goto error;
	
	uint8_t c2 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c2)) goto error;
	
	uint8_t c3 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c3)) goto error;

	uint8_t c4 = aml_next(s, &v);
	if (!v || !aml_is_lead_name_char(c4)) goto error;
	
	uint8_t c5 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c5)) goto error;
	
	uint8_t c6 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c6)) goto error;
	
	uint8_t c7 = aml_next(s, &v);
	if (!v || !aml_is_name_char(c7)) goto error;
	
	aml_node_t* n = get_new_node();
	if (!n) goto error;
	
	n->type = AML_NODE_DUAL_NAME_SEG;
	n->dual_name_seg[0] = (char)c0;
	n->dual_name_seg[1] = (char)c1;
	n->dual_name_seg[2] = (char)c2;
	n->dual_name_seg[3] = (char)c3;
	n->dual_name_seg[4] = (char)c4;
	n->dual_name_seg[5] = (char)c5;
	n->dual_name_seg[6] = (char)c6;
	n->dual_name_seg[7] = (char)c7;
	n->dual_name_seg[8] = '\0';
	
	if (valid) *valid = AOS_TRUE;
	return n;
	
	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_multi_namepath(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_MULTI_NAME_PREFIX || !prefix_valid) goto error;
	
	aos_bool v = AOS_TRUE;
	uint8_t segments = aml_next(s, &v);
	if (!v || segments < 1) goto error;

	uint8_t* start = s->ptr;

	uint8_t segments_consumed = 0;
	uint8_t bytes_per_seg_consumed = 0;
	while (s->ptr < s->end) {
		aos_bool v = AOS_TRUE;
        uint8_t c = aml_next(s, &v);
        if (!v) goto error;

		if (bytes_per_seg_consumed == 0) { // First byte of nameseg
			if (!aml_is_lead_name_char(c)) goto error;
		} else {
			if (!aml_is_name_char(c)) goto error;
		}

		if (++bytes_per_seg_consumed == 4) {
			bytes_per_seg_consumed = 0;
			if (++segments_consumed == segments) break;
		}
    }
	
	
	aml_node_t* n = get_new_node();
	if (!n) goto error;
	
	n->type = AML_NODE_MULTI_NAME_SEG;
	n->multi_name_seg.count = segments;
	n->multi_name_seg.segments = (char(*)[4])start;
	
	if (valid) *valid = AOS_TRUE;
	return n;
	
	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_null_namepath(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != 0 || !prefix_valid) goto error;
	
	aml_node_t* n = get_new_node();
	if (!n) goto error;
	
	n->type = AML_NODE_NULL_NAME;
	
	if (valid) *valid = AOS_TRUE;
	return n;
	
	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_namepath(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;
	uint8_t enc = aml_peek(s, &v);
	if (!v) goto error;

	switch (enc) {
		case AML_DUAL_NAME_PREFIX: return aml_parse_dual_namepath(s, valid);
		case AML_MULTI_NAME_PREFIX: return aml_parse_multi_namepath(s, valid);
		case 0: return aml_parse_null_namepath(s, valid);

		default: return aml_parse_nameseg(s, valid);
	}

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse(aml_stream_t* s, aml_node_t* parent, aos_bool* valid);
static aml_node_t* aml_parse_root(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool prefix_valid = AOS_TRUE;
	if (aml_next(s, &prefix_valid) != AML_ROOT_CHAR || !prefix_valid) goto error;
	
	aml_node_t* n = get_new_node();
	if (!n) goto error;
	
	n->type = AML_NODE_ROOT;

	aos_bool v = AOS_TRUE;
	aml_node_t* children = aml_parse(s, n, &v);
	if (!v || !children) goto error;

	n->child = children;
	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_data_object(aml_stream_t* s, aos_bool* valid) {
	if (!s) goto error;

	aos_bool v = AOS_TRUE;
	uint8_t enc = aml_peek(s, &v);
	if (!v) goto error;

	switch (enc) {
		case AML_ZERO_OP: return aml_parse_zero_const(s, valid);
		case AML_ONE_OP: return aml_parse_one_const(s, valid);
		case AML_ONES_OP: return aml_parse_ones_const(s, valid);

		case AML_BYTE_PREFIX: return aml_parse_byte_const(s, valid);
		case AML_WORD_PREFIX: return aml_parse_word_const(s, valid);
		case AML_DWORD_PREFIX: return aml_parse_dword_const(s, valid);
		case AML_QWORD_PREFIX: return aml_parse_qword_const(s, valid);

		case AML_STRING_PREFIX: return aml_parse_string_const(s, valid);



		default: goto error;
	}

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse_term(aml_stream_t* s, aml_node_t* parent, aos_bool* valid) {
    if (!s || !parent) goto error;

    aos_bool v = AOS_TRUE;
    uint8_t op = aml_peek(s, &v);
    if (!v) goto error;

    switch (op) {
        case AML_ZERO_OP:
        case AML_ONE_OP:
        case AML_ONES_OP:
        case AML_BYTE_PREFIX:
        case AML_WORD_PREFIX:
        case AML_DWORD_PREFIX:
        case AML_QWORD_PREFIX:
        case AML_STRING_PREFIX: {
            aml_node_t* n = aml_parse_data_object(s, valid);
			if (!n) goto error;

			n->parent = parent;
			return n;
		}

        default: {
			if (valid) *valid = AOS_TRUE;
			return NULL; // Could be runtime
		}
    }

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_node_t* aml_parse(aml_stream_t* s, aml_node_t* parent, aos_bool* valid) {
	if (!s) goto error;

	aml_node_t* n = get_new_node();
	if (!n) goto error;
	n->type = AML_NODE_NULL;
	if (parent) n->parent = parent;

	aml_node_t* prev = NULL;

	aos_bool eof_valid = AOS_TRUE;
	while (!aml_eof(s, &eof_valid) && eof_valid) {
		aos_bool v = AOS_TRUE;
		aml_node_t* cnode = aml_parse_term(s, n, &v);
		if (!v) goto error;
		if (v && !cnode) {
			// Discarded data
			continue;
		}

		if (!prev) {
			n->child = cnode;
		} else {
			prev->sibling = cnode;
		}

		prev = cnode;
	}

	if (valid) *valid = AOS_TRUE;
	return n;

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_object_t* aml_eval_arg(aml_frame_t* frame, uint8_t opcode, aos_bool* valid) {
	if (!frame) goto error;

    uint8_t idx = opcode - AML_ARG_0_OP;
    if (idx >= 7) goto error;

	if (valid) *valid = AOS_TRUE;
    return frame->args[idx];

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_object_t* aml_eval_local(aml_frame_t* frame, uint8_t opcode, aos_bool* valid) {
	if (!frame) goto error;

    uint8_t idx = opcode - AML_LOCAL_0_OP;
    if (idx >= 8) goto error;

	if (valid) *valid = AOS_TRUE;
    return frame->locals[idx];

	error: {
		if (valid) *valid = AOS_FALSE;
		return NULL;
	}
}

static aml_object_t* aml_make_integer(uint64_t value) {
    aml_object_t* obj = get_new_object();
    if (!obj) return NULL;

    obj->type = AML_OBJ_INTEGER;
    obj->integer = value;
    return obj;
}

static uint64_t aml_get_integer(aml_object_t* obj) {
    if (!obj)
        return 0;

    switch (obj->type) {
        case AML_OBJ_INTEGER:
            return obj->integer;

        default:
            return 0;
    }
}

static aml_object_t* aml_eval_byte(aml_frame_t* frame) {
    uint8_t value = *frame->pc++;
    return aml_make_integer(value);
}

static aml_object_t* aml_eval_word(aml_frame_t* frame) {
    uint16_t value = *(uint16_t*)frame->pc;
    frame->pc += 2;

    return aml_make_integer(value);
}

static aml_object_t* aml_eval_dword(aml_frame_t* frame) {
    uint32_t value = *(uint32_t*)frame->pc;
    frame->pc += 4;

    return aml_make_integer(value);
}

static aml_object_t* aml_eval_qword(aml_frame_t* frame) {
    uint64_t value = *(uint64_t*)frame->pc;
    frame->pc += 8;

    return aml_make_integer(value);
}

static aml_object_t* aml_eval_simple_name(aml_frame_t* frame) {
    uint8_t opcode = *frame->pc;

    if (opcode >= AML_ARG_0_OP && opcode <= AML_ARG_6_OP) {
        frame->pc++;
        return aml_eval_arg(frame, opcode, NULL);
    }
    if (opcode >= AML_LOCAL_0_OP && opcode <= AML_LOCAL_7_OP){
        frame->pc++;
        return aml_eval_local(frame, opcode, NULL);
    }

	return NULL; // will fix
}

static aml_object_t* aml_eval_supername(aml_frame_t* frame) {
    return aml_eval_simple_name(frame);
}

static aml_object_t* aml_eval_termarg(aml_frame_t* frame) {
    uint8_t opcode = *frame->pc++;

    switch(opcode) {
        case AML_ZERO_OP:
            return aml_make_integer(0);

        case AML_ONE_OP:
            return aml_make_integer(1);

        case AML_ONES_OP:
            return aml_make_integer(UINT64_MAX);

        case AML_BYTE_PREFIX:
            return aml_eval_byte(frame);

        case AML_WORD_PREFIX:
            return aml_eval_word(frame);

        case AML_DWORD_PREFIX:
            return aml_eval_dword(frame);

        case AML_QWORD_PREFIX:
            return aml_eval_qword(frame);

        default: {
            frame->pc--;
            return aml_eval_supername(frame);
        }
    }
}
