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

	uint64_t image_base;
	uint64_t image_size;

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
		case ET_DYN: out->reloc = AOS_TRUE; break;
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
	uint64_t image_base = UINT64_MAX;
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
		if (ph->p_align > 1 && (ph->p_vaddr % ph->p_align) != (ph->p_offset % ph->p_align)) return AOS_FALSE;
		if (ph->p_memsz > UINT64_MAX - ph->p_vaddr) return AOS_FALSE;

		for (size_t j = 0; j < ehdr->e_phnum; j++) {
			if (j == i) continue;

			Elf64_Phdr* xph = &phdr[j];
			if (xph->p_type != PT_LOAD) continue;
			if (xph->p_memsz == 0) continue;
			if (xph->p_align == 0) return AOS_FALSE;
			if ((xph->p_align & (xph->p_align - 1)) != 0) return AOS_FALSE;
			if (xph->p_filesz > xph->p_memsz) return AOS_FALSE;
			if (xph->p_offset > ctx->size) return AOS_FALSE;
			if (xph->p_filesz > ctx->size - xph->p_offset) return AOS_FALSE;
			if (xph->p_align > 1 && (xph->p_vaddr % xph->p_align) != (xph->p_offset % xph->p_align)) return AOS_FALSE;
			if (xph->p_memsz > UINT64_MAX - xph->p_vaddr) return AOS_FALSE;

			uint64_t a_start = ph->p_vaddr;
			uint64_t a_end = ph->p_vaddr + ph->p_memsz;

			uint64_t b_start = xph->p_vaddr;
			uint64_t b_end = xph->p_vaddr + xph->p_memsz;

			if (a_start < b_end && b_start < a_end) return AOS_FALSE;
		}

		uint64_t seg_base = ALIGN_DOWN(ph->p_vaddr, 0x1000);
		uint64_t seg_end  = ALIGN_UP(ph->p_vaddr + ph->p_memsz, 0x1000);

		if (seg_base < image_base) image_base = seg_base;
		if (seg_end > end_space) end_space = seg_end;

		EFI_VIRTUAL_ADDRESS addr = ph->p_vaddr;

		if ((ph->p_flags & PF_X) && !entry_valid) {
			if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
		}

		if (!ctx->reloc) {
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
		}
	}

	if (image_base == UINT64_MAX) return AOS_FALSE;
	if (end_space <= image_base) return AOS_FALSE;

	ctx->image_size = end_space - image_base;
	if (ctx->reloc) {
		EFI_PHYSICAL_ADDRESS load_base = 0;
		if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAnyPages, EfiLoaderCode, (ALIGN_UP(ctx->image_size, 0x1000)) / 0x1000, &load_base))) return AOS_FALSE;
		ctx->image_base = (uint64_t)load_base;

		ctx->entry = ctx->image_base + (ctx->entry - image_base);
		for (size_t i = 0; i < ehdr->e_phnum; i++) {
			Elf64_Phdr* ph = &phdr[i];
			if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

			uint64_t addr = ctx->image_base + (ph->p_vaddr - image_base);
			if (ph->p_filesz > 0) {
				memcpy((void*)addr, ctx->data + ph->p_offset, ph->p_filesz);
			}
			if (ph->p_memsz > ph->p_filesz) {
				memset((void*)(addr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
			}

			if ((ph->p_flags & PF_X) && !entry_valid) {
				if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
			}
		}
	} else ctx->image_base = image_base;

	if (!entry_valid) return AOS_FALSE;
	return AOS_TRUE;
}

EFIAPI static aos_bool elf32_load(ELF_CTX* ctx) {
	if (!ctx->valid || !ctx->data || ctx->size == 0 || ctx->b64) return AOS_FALSE;

	Elf32_Ehdr* ehdr = (Elf32_Ehdr*)ctx->data;
	Elf32_Phdr* phdr = (Elf32_Phdr*)(ctx->data + ehdr->e_phoff);

	aos_bool entry_valid = AOS_FALSE;
	uint64_t image_base = UINT64_MAX;
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
		if (ph->p_align > 1 && (ph->p_vaddr % ph->p_align) != (ph->p_offset % ph->p_align)) return AOS_FALSE;
		if ((uint64_t)ph->p_memsz > UINT64_MAX - (uint64_t)ph->p_vaddr) return AOS_FALSE;

		for (size_t j = 0; j < ehdr->e_phnum; j++) {
			if (j == i) continue;

			Elf32_Phdr* xph = &phdr[j];
			if (xph->p_type != PT_LOAD) continue;
			if (xph->p_memsz == 0) continue;
			if (xph->p_align == 0) return AOS_FALSE;
			if ((xph->p_align & (xph->p_align - 1)) != 0) return AOS_FALSE;
			if (xph->p_filesz > xph->p_memsz) return AOS_FALSE;
			if (xph->p_offset > ctx->size) return AOS_FALSE;
			if (xph->p_filesz > ctx->size - xph->p_offset) return AOS_FALSE;
			if (xph->p_align > 1 && (xph->p_vaddr % xph->p_align) != (xph->p_offset % xph->p_align)) return AOS_FALSE;
			if ((uint64_t)xph->p_memsz > UINT64_MAX - (uint64_t)xph->p_vaddr) return AOS_FALSE;

			uint64_t a_start = ph->p_vaddr;
			uint64_t a_end = ph->p_vaddr + ph->p_memsz;

			uint64_t b_start = xph->p_vaddr;
			uint64_t b_end = xph->p_vaddr + xph->p_memsz;

			if (a_start < b_end && b_start < a_end) return AOS_FALSE;
		}

		uint64_t seg_base = ALIGN_DOWN((uint64_t)ph->p_vaddr, 0x1000);
		uint64_t seg_end  = ALIGN_UP((uint64_t)ph->p_vaddr + (uint64_t)ph->p_memsz, 0x1000);

		if (seg_base < image_base) image_base = seg_base;
		if (seg_end > end_space) end_space = seg_end;

		EFI_VIRTUAL_ADDRESS addr = (EFI_VIRTUAL_ADDRESS)ph->p_vaddr;

		if ((ph->p_flags & PF_X) && !entry_valid) {
			if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
		}

		if (!ctx->reloc) {
			EFI_MEMORY_TYPE type = EfiUnusableMemory;
			if (ph->p_flags & PF_X) type = EfiLoaderCode;
			else if (ph->p_flags & PF_W || ph->p_flags & PF_R) type = EfiLoaderData;

			if (type == EfiUnusableMemory) return AOS_FALSE;
			if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAddress, type, (ALIGN_UP((uint64_t)ph->p_memsz, 0x1000)) / 0x1000, &addr))) return AOS_FALSE;

			if (ph->p_filesz > 0) {
				memcpy((void*)addr, ctx->data + ph->p_offset, (size_t)ph->p_filesz);
			}
			if (ph->p_memsz > ph->p_filesz) {
				memset((void*)(addr + ph->p_filesz), 0, (size_t)ph->p_memsz - (size_t)ph->p_filesz);
			}
		}
	}

	if (image_base == UINT64_MAX) return AOS_FALSE;
	if (end_space <= image_base) return AOS_FALSE;

	ctx->image_size = end_space - image_base;
	if (ctx->reloc) {
		EFI_PHYSICAL_ADDRESS load_base = 0;
		if (EFI_ERROR(pefi_state.boot_services->AllocatePages(AllocateAnyPages, EfiLoaderCode, (ALIGN_UP(ctx->image_size, 0x1000)) / 0x1000, &load_base))) return AOS_FALSE;
		ctx->image_base = (uint64_t)load_base;

		ctx->entry = ctx->image_base + (ctx->entry - image_base);
		for (size_t i = 0; i < ehdr->e_phnum; i++) {
			Elf32_Phdr* ph = &phdr[i];
			if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

			uint64_t addr = ctx->image_base + (ph->p_vaddr - image_base);
			if (ph->p_filesz > 0) {
				memcpy((void*)addr, ctx->data + ph->p_offset, (size_t)ph->p_filesz);
			}
			if (ph->p_memsz > ph->p_filesz) {
				memset((void*)(addr + ph->p_filesz), 0, (size_t)ph->p_memsz - (size_t)ph->p_filesz);
			}

			if ((ph->p_flags & PF_X) && !entry_valid) {
				if (ctx->entry < addr + ph->p_memsz && ctx->entry >= addr) entry_valid = AOS_TRUE;
			}
		}
	} else ctx->image_base = image_base;

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

EFIAPI static aos_bool elf64_relocate(ELF_CTX* ctx) {
	if (!ctx->valid || !ctx->data || !ctx->b64 || !ctx->reloc) return AOS_FALSE;

	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)ctx->data;
	Elf64_Phdr* phdr = (Elf64_Phdr*)(ctx->data + ehdr->e_phoff);

	Elf64_Dyn* dynamic = NULL;
	uint64_t dynamic_size = 0;
	for (size_t i = 0; i < ehdr->e_phnum; i++) {
		Elf64_Phdr* ph = &phdr[i];

		if (ph->p_type != PT_DYNAMIC) continue;
		if (ph->p_filesz < sizeof(Elf64_Dyn)) return AOS_FALSE;
		if (ph->p_offset > ctx->size) return AOS_FALSE;
		if (ph->p_filesz > ctx->size - ph->p_offset) return AOS_FALSE;

		dynamic = (Elf64_Dyn*)(ctx->data + ph->p_offset);
		dynamic_size = ph->p_filesz;
		break;
	}
	if (!dynamic) return AOS_FALSE;

	uint64_t rela_offset = 0;
	uint64_t rela_size = 0;
	uint64_t rela_ent = 0;
	aos_bool have_rela = AOS_FALSE;
	aos_bool have_relasz = AOS_FALSE;
	aos_bool have_relaent = AOS_FALSE;

	size_t dynamic_count = dynamic_size / sizeof(Elf64_Dyn);
	for (size_t i = 0; i < dynamic_count; i++) {
		Elf64_Dyn* dyn = &dynamic[i];
		if (dyn->d_tag == DT_NULL) break;

		switch (dyn->d_tag) {
			case DT_RELA: {
				rela_offset = dyn->d_un.d_ptr;
				have_rela = AOS_TRUE;
				break;
			}

			case DT_RELASZ: {
				rela_size = dyn->d_un.d_val;
				have_relasz = AOS_TRUE;
				break;
			}

			case DT_RELAENT: {
				rela_ent = dyn->d_un.d_val;
				have_relaent = AOS_TRUE;
				break;
			}

			default: break;
		}
	}

	if (!have_rela && !have_relasz && !have_relaent) return AOS_TRUE;
	if (!have_rela || !have_relasz || !have_relaent) return AOS_FALSE;
	if (rela_ent != sizeof(Elf64_Rela)) return AOS_FALSE;
	if (rela_size == 0) return AOS_TRUE;
	if (rela_size % rela_ent != 0) return AOS_FALSE;

	uint64_t rela_end;
	if (rela_offset > UINT64_MAX - rela_size) return AOS_FALSE;
	rela_end = rela_offset + rela_size;

	if (rela_offset < 0) return AOS_FALSE;
	if (rela_offset < ctx->image_base || rela_end > ctx->image_base + ctx->image_size) return AOS_FALSE;

	Elf64_Rela* rela = (Elf64_Rela*)(ctx->image_base + rela_offset);
	size_t count = rela_size / rela_ent;
	for (size_t i = 0; i < count; i++) {
		Elf64_Rela* r = &rela[i];
		uint32_t type = ELF64_R_TYPE(r->r_info);

		if (type != R_X86_64_RELATIVE) return AOS_FALSE;
		if (r->r_offset > ctx->image_size - sizeof(uint64_t)) return AOS_FALSE;

		uint64_t* target = (uint64_t*)(ctx->image_base + r->r_offset);
		*target = ctx->image_base + r->r_addend;
	}

	return AOS_TRUE;
}

EFIAPI aos_bool try_load_elf(uint8_t* data, uint64_t size, uint64_t* entry, uint64_t* stack_base_out, uint64_t* stack_top_out) {
	if (pefi_state.initialized != 1) return AOS_FALSE;

	ELF_CTX ctx = {0};
	if (!elf_make_ctx(data, size, &ctx)) return AOS_FALSE;
	if (!ctx.valid) return AOS_FALSE;

	if (!elf_load(&ctx)) return AOS_FALSE;

	ctx.stack_top = ALIGN_UP((ctx.image_base + ctx.image_size) + 1 + 0x10000, 0x1000);
	ctx.stack_base = ALIGN_UP((ctx.image_base + ctx.image_size) + 1, 0x1000);

	if (entry) *entry = ctx.entry;
	if (stack_base_out) *stack_base_out = ctx.stack_base;
	if (stack_top_out) *stack_top_out = ctx.stack_top;
	return AOS_TRUE;
}
