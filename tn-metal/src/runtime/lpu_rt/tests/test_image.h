/*
 * test_image.h — build a minimal RISC-V PIE operator image at runtime (no cross toolchain needed)
 *
 * The sim backend does not execute code (the exec hook does the data transform), so the image
 * only needs the loader to accept it and expose relatable entry/symbol vaddrs. Layout: Ehdr +
 * 1x PT_LOAD (offset==vaddr identity mapping, fixed by the operator ABI) + a fake code payload
 * + .shstrtab/.text/.symtab/.strtab + 5x Shdr. Only assumes a little-endian host (true in the
 * x86_64 container).
 */
#ifndef TEST_IMAGE_H
#define TEST_IMAGE_H

#include <elf.h>
#include <stdint.h>
#include <string.h>

/* old-host elf.h (Ubuntu 20.04 glibc 2.31) may lack RISC-V constants */
#ifndef EM_RISCV
#define EM_RISCV 243
#endif
#ifndef R_RISCV_RELATIVE
#define R_RISCV_RELATIVE 3
#endif

static size_t test_align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* returns the image byte count; 0 if cap is too small or the args are invalid */
static size_t lpu_test_image_build(uint8_t *buf, size_t cap, uint64_t entry_vaddr,
                                   const char *symname) {
    if (!buf || !symname || !symname[0] || cap < 2048)
        return 0;

    /* payload area: after Ehdr(0x40) + Phdr(0x38), 32B of fake code */
    const size_t payload_off = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    const size_t payload_len = 32;

    /* --- Ehdr --- */
    Elf64_Ehdr eh;
    memset(&eh, 0, sizeof(eh));
    memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS64;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_type = ET_DYN;
    eh.e_machine = EM_RISCV;
    eh.e_version = EV_CURRENT;
    eh.e_entry = entry_vaddr;
    eh.e_phoff = sizeof(Elf64_Ehdr);
    eh.e_ehsize = sizeof(Elf64_Ehdr);
    eh.e_phentsize = sizeof(Elf64_Phdr);
    eh.e_phnum = 1;
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum = 5;
    eh.e_shstrndx = 1;
    memcpy(buf, &eh, sizeof(eh));

    /* --- Phdr: single PT_LOAD identity mapping --- */
    Elf64_Phdr ph;
    memset(&ph, 0, sizeof(ph));
    ph.p_type = PT_LOAD;
    ph.p_flags = PF_R | PF_X;
    ph.p_offset = 0;
    ph.p_vaddr = 0;
    ph.p_paddr = 0;
    ph.p_filesz = payload_off + payload_len;
    ph.p_memsz = ph.p_filesz;
    ph.p_align = 0x1000;
    memcpy(buf + eh.e_phoff, &ph, sizeof(ph));

    /* --- fake code payload --- */
    for (size_t i = 0; i < payload_len; i++)
        buf[payload_off + i] = (uint8_t)(0x13u + i);

    /* --- .shstrtab: "\0.shstrtab\0.text\0.symtab\0.strtab\0" --- */
    static const char shstr[] = "\0.shstrtab\0.text\0.symtab\0.strtab\0";
    const size_t shstr_off = payload_off + payload_len;
    const size_t shstr_size = sizeof(shstr);
    memcpy(buf + shstr_off, shstr, shstr_size);
    /* name offsets: .shstrtab@1  .text@11  .symtab@17  .strtab@25 */

    /* --- .strtab: "\0<symname>\0" --- */
    size_t str_off = test_align_up(shstr_off + shstr_size, 8);
    size_t namelen = strlen(symname);
    size_t str_size = 1 + namelen + 1;
    if (str_off + str_size > cap)
        return 0;
    buf[str_off] = '\0';
    memcpy(buf + str_off + 1, symname, namelen);
    buf[str_off + 1 + namelen] = '\0';

    /* --- .symtab: null + entry symbol --- */
    size_t sym_off = test_align_up(str_off + str_size, 8);
    Elf64_Sym syms[2];
    memset(syms, 0, sizeof(syms));
    syms[1].st_name = 1;
    syms[1].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    syms[1].st_shndx = 2; /* .text */
    syms[1].st_value = entry_vaddr;
    syms[1].st_size = payload_len;
    if (sym_off + sizeof(syms) > cap)
        return 0;
    memcpy(buf + sym_off, syms, sizeof(syms));

    /* --- Shdr×5 --- */
    size_t sh_off = test_align_up(sym_off + sizeof(syms), 8);
    if (sh_off + 5 * sizeof(Elf64_Shdr) > cap)
        return 0;
    Elf64_Shdr sh[5];
    memset(sh, 0, sizeof(sh));
    sh[1].sh_name = 1; /* .shstrtab */
    sh[1].sh_type = SHT_STRTAB;
    sh[1].sh_offset = shstr_off;
    sh[1].sh_size = shstr_size;
    sh[1].sh_addralign = 1;
    sh[2].sh_name = 11; /* .text */
    sh[2].sh_type = SHT_PROGBITS;
    sh[2].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    sh[2].sh_addr = payload_off;
    sh[2].sh_offset = payload_off;
    sh[2].sh_size = payload_len;
    sh[2].sh_addralign = 4;
    sh[3].sh_name = 17; /* .symtab */
    sh[3].sh_type = SHT_SYMTAB;
    sh[3].sh_offset = sym_off;
    sh[3].sh_size = sizeof(syms);
    sh[3].sh_link = 4;
    sh[3].sh_info = 1; /* index of the first non-local symbol */
    sh[3].sh_addralign = 8;
    sh[3].sh_entsize = sizeof(Elf64_Sym);
    sh[4].sh_name = 25; /* .strtab */
    sh[4].sh_type = SHT_STRTAB;
    sh[4].sh_offset = str_off;
    sh[4].sh_size = str_size;
    sh[4].sh_addralign = 1;
    memcpy(buf + sh_off, sh, sizeof(sh));

    /* fill e_shoff back in */
    eh.e_shoff = sh_off;
    memcpy(buf, &eh, sizeof(eh));

    return sh_off + sizeof(sh);
}

#endif /* TEST_IMAGE_H */
