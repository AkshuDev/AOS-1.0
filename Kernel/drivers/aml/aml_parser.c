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

typedef struct aml_node {
    char name[5];

    struct aml_node* parent;
    struct aml_node* child;
    struct aml_node* sibling;

    aml_object_t* object;
} aml_node_t;

#define OBJECTS_PER_PAGE PAGE_SIZE / sizeof(aml_object_t)

static aml_object_t* object_pool;
static uint64_t object_pool_cap;
static uint64_t object_pool_count;

static uint8_t aml_peek(aml_stream_t* s) {
    return *s->ptr;
}

static uint8_t aml_next(aml_stream_t* s) {
    return *s->ptr++;
}

static uint8_t aml_eof(aml_stream_t* s) {
    return s->ptr >= s->end;
}

static uint32_t aml_parse_pkglen(aml_stream_t* s) {
    uint8_t lead = aml_next(s);

    uint8_t bytes_follow = lead >> 6;
    if (bytes_follow == 0) return lead & 0b00111111;

    uint32_t length = lead & 0b00001111;
    for (uint8_t i = 0; i < bytes_follow; i++) {
        length |= ((uint32_t)aml_next(s)) << (4 + 8 * i);
    }

    return length;
}

static void aml_push(aml_frame_t* frame, aml_object_t* obj) {
	if (frame->stack_top >= 256) return;
    frame->stack[frame->stack_top++] = obj;
}

static struct aml_object* aml_pop(aml_frame_t* frame) {
	if (frame->stack_top == 0) return NULL;
    return frame->stack[--frame->stack_top];
}

static aml_object_t* aml_eval_arg(aml_frame_t* frame, uint8_t opcode) {
    uint8_t idx = opcode - AML_ARG_0_OP;
    if (idx >= 7) return NULL;

    return frame->args[idx];
}

static aml_object_t* aml_eval_local(aml_frame_t* frame, uint8_t opcode) {
    uint8_t idx = opcode - AML_LOCAL_0_OP;
    if (idx >= 8) return NULL;

    return frame->locals[idx];
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
        return aml_eval_arg(frame, opcode);
    }
    if (opcode >= AML_LOCAL_0_OP && opcode <= AML_LOCAL_7_OP){
        frame->pc++;
        return aml_eval_local(frame, opcode);
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