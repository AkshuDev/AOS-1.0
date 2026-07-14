#include <aos_inttypes.h>

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <elf.h>

#include <freestanding.h>
#include <pefilib.h>

#include <elf_uefi.h>

#ifndef ELF_CUR_ARCH
	#define ELF_CUR_ARCH EM_X86_64
#endif

typedef struct {
	aos_bool valid; // Is it valid
	aos_bool b64; // 64-bit?
	aos_bool le; // Little-Endian?
	aos_bool reloc; // Relocatable?

	uint64_t size;
	uint8_t* data;

	uint64_t entry;
	uint64_t stack_base;
	uint64_t stack_top;
} ELF_CTX;

EFIAPI static aos_bool elf64_verify(Elf64_Ehdr* ehdr, uint64_t size) {
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return AOS_FALSE;

	if (ehdr->e_phoff >= size) return AOS_FALSE;
	if (ehdr->e_phnum == 0) return AOS_FALSE;
	if (ehdr->e_phentsize != sizeof(Elf64_Phdr)) return AOS_FALSE;
	if (ehdr->e_phnum > (UINT64_MAX - ehdr->e_phoff) / sizeof(Elf64_Phdr)) return AOS_FALSE;
	if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr) > size) return AOS_FALSE;

	// if (ehdr->e_shoff >= size) return AOS_FALSE;
	// if (ehdr->e_shnum == 0) return AOS_FALSE;
	// if (ehdr->e_shentsize != sizeof(Elf64_Shdr)) return AOS_FALSE;
	// if (ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf64_Shdr) > size) return AOS_FALSE;
	return AOS_TRUE;
}

EFIAPI static aos_bool elf32_verify(Elf32_Ehdr* ehdr, uint64_t size) {
	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return AOS_FALSE;

	if (ehdr->e_phoff >= size) return AOS_FALSE;
	if (ehdr->e_phnum == 0) return AOS_FALSE;
	if (ehdr->e_phentsize != sizeof(Elf32_Phdr)) return AOS_FALSE;
	if (ehdr->e_phnum > (UINT64_MAX - ehdr->e_phoff) / sizeof(Elf32_Phdr)) return AOS_FALSE;
	if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf32_Phdr) > size) return AOS_FALSE;

	// if (ehdr->e_shoff >= size) return AOS_FALSE;
	// if (ehdr->e_shnum == 0) return AOS_FALSE;
	// if (ehdr->e_shentsize != sizeof(Elf32_Shdr)) return AOS_FALSE;
	// if (ehdr->e_shoff + ehdr->e_shnum * sizeof(Elf32_Shdr) > size) return AOS_FALSE;
	return AOS_TRUE;
}

EFIAPI static aos_bool elf_make_ctx(uint8_t* data, uint64_t size, ELF_CTX* out) {
	out->valid = AOS_FALSE;

	Elf32_Ehdr* ehdr32 = (Elf32_Ehdr*)data;
	if (size < sizeof(Elf32_Ehdr)) return AOS_FALSE;
	if (memcmp(ehdr32->e_ident, ELFMAG, SELFMAG) != 0) return AOS_FALSE;
	if (ehdr32->e_ident[EI_VERSION] != EV_CURRENT) return AOS_FALSE;

	switch (ehdr32->e_ident[EI_CLASS]) {
		case ELFCLASS64: out->b64 = AOS_TRUE; break;
		case ELFCLASS32: out->b64 = AOS_FALSE; break;
		default: return AOS_FALSE;
	}

	switch (ehdr32->e_ident[EI_DATA]) {
		case ELFDATA2LSB: out->le = AOS_TRUE; break;
		case ELFDATA2MSB: out->le = AOS_FALSE; break;
		default: return AOS_FALSE;
	}

	Elf64_Ehdr* ehdr64 = (Elf64_Ehdr*)data;
	uint64_t type = 0;

	if (out->b64) {
		if (ehdr64->e_machine != ELF_CUR_ARCH) return AOS_FALSE;
		if (ehdr64->e_version != EV_CURRENT) return AOS_FALSE;

		if (size < sizeof(Elf64_Ehdr)) return AOS_FALSE;
		if (!elf64_verify(ehdr64, size)) return AOS_FALSE;

		type = ehdr64->e_type;
		out->entry = (uint64_t)ehdr64->e_entry;
	} else {
		if (ehdr32->e_machine != ELF_CUR_ARCH) return AOS_FALSE;
		if (ehdr32->e_version != EV_CURRENT) return AOS_FALSE;

		if (!elf32_verify(ehdr32, size)) return AOS_FALSE;

		type = ehdr32->e_type;
		out->entry = (uint64_t)ehdr32->e_entry;
	}

	switch (type) {
		case ET_EXEC: out->reloc = AOS_FALSE; break;
		//case ET_DYN: out->reloc = AOS_TRUE; break;
		default: return AOS_FALSE;
	}

	out->size = size;
	out->data = data;

	out->valid = AOS_TRUE;

	#undef ehdr

	return AOS_TRUE;
}

EFIAPI static aos_bool elf64_load(ELF_CTX* ctx) {
	if (!ctx->valid || !ctx->data || ctx->size == 0 || !ctx->b64) return AOS_FALSE;

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)ctx->data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)(ctx->data + ehdr->e_phoff);

	aos_bool entry_valid = AOS_FALSE;
	uint64_t end_space = 0;
	for (size_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* ph = &phdr[i];
		if (ph->p_type != PT_LOAD) continue;
		if (ph->p_memsz == 0) continue;

		if (ph->p_align == 0) return AOS_FALSE;
		if ((ph->p_align & (ph->p_align - 1)) != 0) return AOS_FALSE;
		if (ph->p_filesz > ph->p_memsz) return AOS_FALSE;
		if (ph->p_offset > ctx->size) return AOS_FALSE;
		if (ph->p_filesz > ctx->size - ph->p_offset) return AOS_FALSE;
		if (ph->p_align > 1 && ((ph->p_vaddr - ph->p_offset) & (ph->p_align - 1)) != 0) return AOS_FALSE;

		for (size_t j = 0; j < ehdr->e_phnum; j++) {
			if (j == i) continue;

			Elf64_Phdr* xph = &phdr[j];
			if (xph->p_type != PT_LOAD) continue;
			if (xph->p_memsz == 0) continue;
			if (xph->p_align == 0) continue;
			if ((xph->p_align & (xph->p_align - 1)) != 0) continue;
			if (xph->p_filesz > xph->p_memsz) continue;
			if (xph->p_offset > ctx->size) continue;
			if (xph->p_filesz > ctx->size - xph->p_offset) continue;
			if (xph->p_align > 1 && ((xph->p_vaddr - xph->p_offset) & (xph->p_align - 1)) != 0) continue;

			if (ph->p_memsz > UINT64_MAX - ph->p_vaddr) return AOS_FALSE;
			if (xph->p_memsz > UINT64_MAX - xph->p_vaddr) return AOS_FALSE;

			uint64_t a_start = ph->p_vaddr;
			uint64_t a_end = ph->p_vaddr + ph->p_memsz;

			uint64_t b_start = xph->p_vaddr;
			uint64_t b_end = xph->p_vaddr + xph->p_memsz;

			if (a_start < b_end && b_start < a_end) return AOS_FALSE;
		}

		EFI_VIRTUAL_ADDRESS addr = ph->p_vaddr;
		if (addr + ph->p_memsz > end_space) end_space = addr + ph->p_memsz;

		EFI_MEMORY_TYPE type = EfiUnusableMemory;
		if (ph->p_flags & PF_X) type = EfiLoaderCode;
		else if (ph->p_flags & PF_W || ph->p_flags & PF_R) type = EfiLoaderData;

		if (type == EfiUnusableMemory) return AOS_FALSE;
		if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAddress, type, (ALIGN_UP(ph->p_memsz, 0x1000)) / 0x1000, &addr))) return AOS_FALSE;

		if (ph->p_filesz > 0) {
			memcpy((void*)addr, ctx->data + ph->p_offset, ph->p_filesz);
		}
		if (ph->p_memsz > ph->p_filesz) {
    		memset((void*)(addr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
		}
	
		if (type == EfiLoaderCode && !entry_valid) {
			if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
		}
	}

	if (end_space == 0) return AOS_FALSE;
	ctx->stack_top = ALIGN_UP(end_space + 1 + 0x10000, 0x1000);
	ctx->stack_base = ALIGN_UP(end_space + 1, 0x1000);

	if (!entry_valid) return AOS_FALSE;
	return AOS_TRUE;
}

EFIAPI static aos_bool elf32_load(ELF_CTX* ctx) {
	if (!ctx->valid || !ctx->data || ctx->size == 0 || ctx->b64) return AOS_FALSE;

	Elf32_Ehdr* ehdr = (Elf32_Ehdr*)ctx->data;
	Elf32_Phdr* phdr = (Elf32_Phdr*)(ctx->data + ehdr->e_phoff);

	aos_bool entry_valid = AOS_FALSE;
	uint64_t end_space = 0;
	for (size_t i = 0; i < ehdr->e_phnum; i++) {
		Elf32_Phdr* ph = &phdr[i];
		if (ph->p_type != PT_LOAD) continue;
		if (ph->p_memsz == 0) continue;

		if (ph->p_align == 0) return AOS_FALSE;
		if ((ph->p_align & (ph->p_align - 1)) != 0) return AOS_FALSE;
		if (ph->p_filesz > ph->p_memsz) return AOS_FALSE;
		if (ph->p_offset > ctx->size) return AOS_FALSE;
		if (ph->p_filesz > ctx->size - ph->p_offset) return AOS_FALSE;
		if (ph->p_align > 1 && ((ph->p_vaddr - ph->p_offset) & (ph->p_align - 1)) != 0) return AOS_FALSE;

		for (size_t j = 0; j < ehdr->e_phnum; j++) {
			if (j == i) continue;

			Elf32_Phdr* xph = &phdr[j];
			if (xph->p_type != PT_LOAD) continue;
			if (xph->p_memsz == 0) continue;
			if (xph->p_align == 0) continue;
			if ((xph->p_align & (xph->p_align - 1)) != 0) continue;
			if (xph->p_filesz > xph->p_memsz) continue;
			if (xph->p_offset > ctx->size) continue;
			if (xph->p_filesz > ctx->size - xph->p_offset) continue;
			if (xph->p_align > 1 && ((xph->p_vaddr - xph->p_offset) & (xph->p_align - 1)) != 0) continue;

			if (ph->p_memsz > UINT64_MAX - ph->p_vaddr) return AOS_FALSE;
			if (xph->p_memsz > UINT64_MAX - xph->p_vaddr) return AOS_FALSE;

			uint64_t a_start = ph->p_vaddr;
			uint64_t a_end = ph->p_vaddr + ph->p_memsz;

			uint64_t b_start = xph->p_vaddr;
			uint64_t b_end = xph->p_vaddr + xph->p_memsz;

			if (a_start < b_end && b_start < a_end) return AOS_FALSE;
		}

		EFI_VIRTUAL_ADDRESS addr = ph->p_vaddr;
		if (addr + ph->p_memsz > end_space) end_space = addr + ph->p_memsz;

		EFI_MEMORY_TYPE type = EfiUnusableMemory;
		if (ph->p_flags & PF_X) type = EfiLoaderCode;
		else if (ph->p_flags & PF_W || ph->p_flags & PF_R) type = EfiLoaderData;

		if (type == EfiUnusableMemory) return AOS_FALSE;
		if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAddress, type, (ALIGN_UP(ph->p_memsz, 0x1000)) / 0x1000, &addr))) return AOS_FALSE;

		if (ph->p_filesz > 0) {
			memcpy((void*)addr, ctx->data + ph->p_offset, ph->p_filesz);
		}
		if (ph->p_memsz > ph->p_filesz) {
    		memset((void*)(addr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
		}
	
		if (type == EfiLoaderCode && !entry_valid) {
			if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
		}
	}

	if (end_space == 0) return AOS_FALSE;
	ctx->stack_top = ALIGN_UP(end_space + 1 + 0x10000, 0x1000);
	ctx->stack_base = ALIGN_UP(end_space + 1, 0x1000);

	if (!entry_valid) return AOS_FALSE;
	return AOS_TRUE;
}

EFIAPI static aos_bool elf_load(ELF_CTX* ctx) {
	if (!ctx->valid || !ctx->data || ctx->size == 0) return AOS_FALSE;
	if (ctx->b64) {
		return elf64_load(ctx);
	} else {
		return elf32_load(ctx);
	}
}

EFIAPI aos_bool try_load_elf(uint8_t* data, uint64_t size, uint64_t* entry, uint64_t* stack_base_out, uint64_t* stack_top_out) {
	if (pefi_state.initialized != 1) return AOS_FALSE;

	ELF_CTX ctx = {0};
	if (!elf_make_ctx(data, size, &ctx)) return AOS_FALSE;
	if (!ctx.valid) return AOS_FALSE;

	if (!elf_load(&ctx)) return AOS_FALSE;
	if (entry) *entry = ctx.entry;
	if (stack_base_out) *stack_base_out = ctx.stack_base;
	if (stack_top_out) *stack_top_out = ctx.stack_top;
	return AOS_TRUE;
}
