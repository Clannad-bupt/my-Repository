/* rt_module.c — operator DSO loader + fw export table (host side applies relocations; fw has no ELF
 * loader)
 *
 * Operator ABI (docs/rt/lpu-sw-stack.md §4.6–§4.8):
 *  - ELF64 / little-endian / EM_RISCV / ET_DYN (-fPIC -shared DSO,
 *    loadable at any aligned position in DRAM; the same image is reusable)
 *  - only three relocation types: R_RISCV_RELATIVE (within the image),
 *    R_RISCV_64 (GOT, referencing fw globals), R_RISCV_JUMP_SLOT (PLT,
 *    calling fw functions) — any other type (including every TLS type) =
 *    ABI violation, reject the whole image
 *  - symbol resolution order = module's own .dynsym (defined) → fw export
 *    table (UND; not found → LPU_RT_ERROR_SYMBOL_NOT_FOUND, whole image
 *    rejected — strict ABI, weak UND rejected the same way); any DT_NEEDED →
 *    reject (freestanding)
 *  - entry = e_entry (GetFunction(NULL)), or STT_FUNC global symbols in a
 *    non-stripped image's .symtab (looked up by name; the symbol table is
 *    extracted and cached at load time — section headers are not inside
 *    PT_LOAD, so they are not in the window after loading)
 *
 * fw export table (lpuRtSetFirmwareElf): the lpu_fw_-prefixed STT_FUNC
 * global symbols of the fw ELF .symtab (the prefix filter is a curated API
 * surface; fw-internal fw_* symbols and newlib globals are not exported).
 * The fw link address is fixed (fw single image → identical on every core),
 * st_value is the absolute address → the JUMP_SLOT single-value property
 * holds.
 *
 * Address convention (rt_internal.h): mod->base = the device-side address
 * where vaddr 0 lands, i.e. allocated block address - min_vaddr;
 * fn->entry_off = the raw vaddr, entry_pc = base + entry_off.
 */
#include "lpu/lpu_fw_api.h"
#include "rt_internal.h"

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Old hosts (Ubuntu 20.04, glibc 2.31) may have an elf.h missing the RISC-V constants */
#ifndef EM_RISCV
#define EM_RISCV 243
#endif
#ifndef R_RISCV_RELATIVE
#define R_RISCV_RELATIVE 3
#endif
#ifndef R_RISCV_64
#define R_RISCV_64 2
#endif
#ifndef R_RISCV_JUMP_SLOT
#define R_RISCV_JUMP_SLOT 5
#endif

#define RT_MODULE_MAX_FUNCS 64

/* .dynsym resolved at load time (one value per symbol) */
struct rt_dynsym {
    uint64_t v; /* defined: ELF vaddr (folded to base+v when used); UND: fw absolute address */
    bool defined;
};

/* in-image range check */
static bool in_image(size_t size, uint64_t off, uint64_t len) {
    return off <= size && len <= size - off;
}

/* ELF header validation. require_dyn: operator modules accept only ET_DYN;
 * the fw ELF accepts ET_EXEC (fixed link address) or ET_DYN. */
static bool hdr_ok(const Elf64_Ehdr *eh, size_t size, bool require_dyn) {
    if (!in_image(size, 0, sizeof(*eh)))
        return false;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0)
        return false;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return false;
    if (eh->e_machine != EM_RISCV)
        return false; /* RISC-V only */
    if (require_dyn && eh->e_type != ET_DYN)
        return false;
    if (!require_dyn && eh->e_type != ET_EXEC && eh->e_type != ET_DYN)
        return false;
    if (eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_shentsize != sizeof(Elf64_Shdr))
        return false;
    return true;
}

/* PT_DYNAMIC d_ptr is a link-time vaddr → mapped back to a file offset via PT_LOAD */
static bool vaddr_to_off(const Elf64_Phdr *ph, int phnum, uint64_t vaddr, size_t *off) {
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (vaddr >= ph[i].p_vaddr && vaddr < ph[i].p_vaddr + ph[i].p_filesz) {
            *off = ph[i].p_offset + (vaddr - ph[i].p_vaddr);
            return true;
        }
    }
    return false;
}

/* Extract the STT_FUNC global symbol table from the image's section headers (once, at load time) */
static lpuRtError parse_symbols(const uint8_t *img, size_t size, const Elf64_Ehdr *eh,
                                struct lpu_function **funcs, int *nfuncs) {
    *funcs = NULL;
    *nfuncs = 0;
    if (eh->e_shoff == 0 || eh->e_shnum == 0)
        return LPU_RT_SUCCESS; /* no symbol table: only GetFunction(NULL) works */

    if (!in_image(size, eh->e_shoff, (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr)))
        return LPU_RT_ERROR_MODULE_INVALID;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(img + eh->e_shoff);

    struct lpu_function *tab = calloc(RT_MODULE_MAX_FUNCS, sizeof(*tab));
    if (!tab)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    int n = 0;

    for (int i = 0; i < eh->e_shnum && n < RT_MODULE_MAX_FUNCS; i++) {
        if (sh[i].sh_type != SHT_SYMTAB)
            continue;
        if (sh[i].sh_link >= eh->e_shnum)
            continue;
        const Elf64_Shdr *str = &sh[sh[i].sh_link];
        if (!in_image(size, sh[i].sh_offset, sh[i].sh_size) ||
            !in_image(size, str->sh_offset, str->sh_size) || sh[i].sh_entsize != sizeof(Elf64_Sym))
            continue;

        const Elf64_Sym *sym = (const Elf64_Sym *)(img + sh[i].sh_offset);
        size_t nsym = sh[i].sh_size / sizeof(Elf64_Sym);
        const char *strtab = (const char *)(img + str->sh_offset);

        for (size_t s = 0; s < nsym && n < RT_MODULE_MAX_FUNCS; s++) {
            if (ELF64_ST_TYPE(sym[s].st_info) != STT_FUNC)
                continue;
            if (ELF64_ST_BIND(sym[s].st_info) != STB_GLOBAL &&
                ELF64_ST_BIND(sym[s].st_info) != STB_WEAK)
                continue;
            if (sym[s].st_shndx == SHN_UNDEF)
                continue;
            if (sym[s].st_name >= str->sh_size)
                continue;
            const char *name = strtab + sym[s].st_name;
            if (!name[0])
                continue;
            snprintf(tab[n].name, sizeof(tab[n].name), "%s", name);
            tab[n].entry_off = sym[s].st_value;
            n++;
        }
    }

    if (n == 0) {
        free(tab);
        return LPU_RT_SUCCESS;
    }
    *funcs = tab;
    *nfuncs = n;
    return LPU_RT_SUCCESS;
}

/* Parse .dynsym from the image's section headers and resolve every symbol
 * (done before DRAM allocation; failure has zero side effects).
 * defined → record the vaddr (folded to base+v when used); UND → look up the
 * fw export table and record the absolute address (not found =
 * SYMBOL_NOT_FOUND; weak UND rejected the same way). */
static lpuRtError parse_dynsyms(lpu_ctx_t *c, const uint8_t *img, size_t size, const Elf64_Ehdr *eh,
                                struct rt_dynsym **out, int *nout) {
    *out = NULL;
    *nout = 0;
    if (eh->e_shoff == 0 || eh->e_shnum == 0)
        return LPU_RT_SUCCESS; /* no .dynsym: RELATIVE-only image */

    if (!in_image(size, eh->e_shoff, (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr)))
        return LPU_RT_ERROR_MODULE_INVALID;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(img + eh->e_shoff);

    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_DYNSYM)
            continue;
        if (sh[i].sh_link >= eh->e_shnum)
            continue;
        const Elf64_Shdr *str = &sh[sh[i].sh_link];
        if (!in_image(size, sh[i].sh_offset, sh[i].sh_size) ||
            !in_image(size, str->sh_offset, str->sh_size) || sh[i].sh_entsize != sizeof(Elf64_Sym))
            continue;

        int n = (int)(sh[i].sh_size / sizeof(Elf64_Sym));
        if (n <= 0)
            return LPU_RT_SUCCESS;
        struct rt_dynsym *tab = calloc((size_t)n, sizeof(*tab));
        if (!tab)
            return LPU_RT_ERROR_OUT_OF_MEMORY;

        const Elf64_Sym *sym = (const Elf64_Sym *)(img + sh[i].sh_offset);
        const char *strtab = (const char *)(img + str->sh_offset);

        for (int s = 1; s < n; s++) { /* index 0 reserved: null symbol */
            if (sym[s].st_shndx == SHN_UNDEF) {
                /* UND → fw export table (fw has no ELF loader; host resolves everything) */
                if (sym[s].st_name >= str->sh_size) {
                    free(tab);
                    return LPU_RT_ERROR_MODULE_INVALID;
                }
                const char *name = strtab + sym[s].st_name;
                bool hit = false;
                for (int k = 0; k < c->nfw_exports; k++) {
                    if (strcmp(c->fw_exports[k].name, name) == 0) {
                        tab[s].v = c->fw_exports[k].addr;
                        tab[s].defined = false;
                        hit = true;
                        break;
                    }
                }
                if (!hit) {
                    fprintf(stderr, "lpu_rt: operator UND symbol '%s' not in fw export table\n",
                            name);
                    free(tab);
                    return LPU_RT_ERROR_SYMBOL_NOT_FOUND;
                }
            } else {
                tab[s].v = sym[s].st_value;
                tab[s].defined = true;
            }
        }
        *out = tab;
        *nout = n;
        return LPU_RT_SUCCESS;
    }
    return LPU_RT_SUCCESS;
}

/* Apply one relocation region (.rela.dyn / .rela.plt); host side writes into the load window in
 * place */
static lpuRtError apply_rela(uint8_t *win, struct lpu_module *m, const struct rt_dynsym *syms,
                             int nsyms, const Elf64_Rela *rela, size_t rela_sz) {
    for (size_t off = 0; off + sizeof(Elf64_Rela) <= rela_sz; off += sizeof(Elf64_Rela)) {
        const Elf64_Rela *r = (const Elf64_Rela *)((const uint8_t *)rela + off);
        uint32_t type = (uint32_t)ELF64_R_TYPE(r->r_info);
        uint32_t si = (uint32_t)ELF64_R_SYM(r->r_info);
        uint64_t symval;

        if (r->r_offset < m->min_vaddr || r->r_offset >= m->min_vaddr + m->span_vaddr)
            return LPU_RT_ERROR_MODULE_INVALID;

        if (type == R_RISCV_RELATIVE) {
            symval = m->base; /* RELATIVE: base + addend */
        } else if (type == R_RISCV_64 || type == R_RISCV_JUMP_SLOT) {
            if (si == 0 || (int)si >= nsyms || !syms)
                return LPU_RT_ERROR_MODULE_INVALID;
            symval = syms[si].defined ? m->base + syms[si].v : syms[si].v;
        } else {
            return LPU_RT_ERROR_MODULE_INVALID; /* anything else (incl. TLS) = operator ABI
                                                   violation */
        }

        *(uint64_t *)(win + (r->r_offset - m->min_vaddr)) = symval + r->r_addend;
    }
    return LPU_RT_SUCCESS;
}

/* ---------------- fw export table (lpuRtSetFirmwareElf) ---------------- */

LPU_RT_API lpuRtError lpuRtSetFirmwareElf(const void *fw_elf_image, size_t size) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!fw_elf_image || size < sizeof(Elf64_Ehdr))
        return LPU_RT_ERROR_INVALID_VALUE;

    const uint8_t *img = fw_elf_image;
    const Elf64_Ehdr *eh = fw_elf_image;
    if (!hdr_ok(eh, size, false)) {
        rt_set_last_error(LPU_RT_ERROR_MODULE_INVALID);
        return LPU_RT_ERROR_MODULE_INVALID;
    }
    if (eh->e_shoff == 0 || eh->e_shnum == 0)
        goto invalid; /* no symbol table: no export table to speak of */
    if (!in_image(size, eh->e_shoff, (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr)))
        goto invalid;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(img + eh->e_shoff);

    struct lpu_fw_export *tab = calloc(RT_FW_MAX_EXPORTS, sizeof(*tab));
    if (!tab)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    int n = 0;
    size_t pfxlen = strlen(LPU_FW_API_PREFIX);

    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_SYMTAB)
            continue;
        if (sh[i].sh_link >= eh->e_shnum)
            continue;
        const Elf64_Shdr *str = &sh[sh[i].sh_link];
        if (!in_image(size, sh[i].sh_offset, sh[i].sh_size) ||
            !in_image(size, str->sh_offset, str->sh_size) || sh[i].sh_entsize != sizeof(Elf64_Sym))
            continue;

        const Elf64_Sym *sym = (const Elf64_Sym *)(img + sh[i].sh_offset);
        size_t nsym = sh[i].sh_size / sizeof(Elf64_Sym);
        const char *strtab = (const char *)(img + str->sh_offset);

        for (size_t s = 0; s < nsym; s++) {
            if (ELF64_ST_TYPE(sym[s].st_info) != STT_FUNC)
                continue;
            if (ELF64_ST_BIND(sym[s].st_info) != STB_GLOBAL &&
                ELF64_ST_BIND(sym[s].st_info) != STB_WEAK)
                continue;
            if (sym[s].st_shndx == SHN_UNDEF)
                continue;
            if (sym[s].st_name >= str->sh_size)
                continue;
            const char *name = strtab + sym[s].st_name;
            if (strncmp(name, LPU_FW_API_PREFIX, pfxlen) != 0)
                continue; /* prefix filter: fw-internal/newlib symbols are not exported */
            if (n >= RT_FW_MAX_EXPORTS) {
                free(tab);
                rt_set_last_error(LPU_RT_ERROR_INVALID_VALUE);
                return LPU_RT_ERROR_INVALID_VALUE;
            }
            snprintf(tab[n].name, sizeof(tab[n].name), "%s", name);
            tab[n].addr = sym[s].st_value; /* fw link address fixed → absolute address */
            n++;
        }
    }

    free(c->fw_exports); /* repeatable call: replace the registry */
    c->fw_exports = tab;
    c->nfw_exports = n;
    rt_log_("SetFirmwareElf: size=%zu exported %d lpu_fw_ symbols", size, n);
    return LPU_RT_SUCCESS;

invalid:
    rt_set_last_error(LPU_RT_ERROR_MODULE_INVALID);
    return LPU_RT_ERROR_MODULE_INVALID;
}

/* ---------------- operator DSO load ---------------- */

LPU_RT_API lpuRtError lpuRtModuleLoad(const void *image, size_t imageSize, lpuRtModule *module) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!image || !module)
        return LPU_RT_ERROR_INVALID_VALUE;

    const uint8_t *img = image;
    const Elf64_Ehdr *eh = image;
    rt_log_("ModuleLoad: size=%zu", imageSize);
    if (!hdr_ok(eh, imageSize, true))
        goto invalid;

    /* PT_LOAD span and load alignment */
    if (!in_image(imageSize, eh->e_phoff, (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr)))
        goto invalid;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(img + eh->e_phoff);

    uint64_t min_v = ~0ull, max_v = 0, align = LPU_POOL_ALIGN;
    const Elf64_Phdr *dyn = NULL;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD) {
            if (ph[i].p_memsz == 0)
                continue;
            if (!in_image(imageSize, ph[i].p_offset, ph[i].p_filesz))
                goto invalid;
            if (ph[i].p_vaddr < min_v)
                min_v = ph[i].p_vaddr;
            if (ph[i].p_vaddr + ph[i].p_memsz > max_v)
                max_v = ph[i].p_vaddr + ph[i].p_memsz;
            if (ph[i].p_align > align && (ph[i].p_align & (ph[i].p_align - 1)) == 0)
                align = ph[i].p_align;
        } else if (ph[i].p_type == PT_DYNAMIC) {
            dyn = &ph[i];
        }
    }
    if (min_v == ~0ull || max_v <= min_v)
        goto invalid;

    /* dynamic segment: the two relocation regions (.rela.dyn / .rela.plt) + DT_NEEDED rejected */
    const Elf64_Rela *rdyn = NULL, *rplt = NULL;
    size_t rdyn_sz = 0, rplt_sz = 0;
    if (dyn && dyn->p_filesz) {
        if (!in_image(imageSize, dyn->p_offset, dyn->p_filesz))
            goto invalid;
        const Elf64_Dyn *d = (const Elf64_Dyn *)(img + dyn->p_offset);
        size_t ndyn = dyn->p_filesz / sizeof(Elf64_Dyn);

        for (size_t i = 0; i < ndyn && d[i].d_tag != DT_NULL; i++) {
            switch (d[i].d_tag) {
            case DT_NEEDED:
                goto invalid; /* libc dependencies forbidden (freestanding ABI) */
            case DT_RELA:
            case DT_JMPREL: {
                size_t off;
                if (!vaddr_to_off(ph, eh->e_phnum, d[i].d_un.d_ptr, &off))
                    goto invalid;
                if (d[i].d_tag == DT_RELA)
                    rdyn = (const Elf64_Rela *)(img + off);
                else
                    rplt = (const Elf64_Rela *)(img + off);
                break;
            }
            case DT_RELASZ:
                rdyn_sz = d[i].d_un.d_val;
                break;
            case DT_PLTRELSZ:
                rplt_sz = d[i].d_un.d_val;
                break;
            default:
                break;
            }
        }
        if ((rdyn && !in_image(imageSize, (uint64_t)((const uint8_t *)rdyn - img), rdyn_sz)) ||
            (rplt && !in_image(imageSize, (uint64_t)((const uint8_t *)rplt - img), rplt_sz)))
            goto invalid;
    }

    /* .dynsym parse + resolve UND symbols against the fw export table (before allocation; failure
     * has zero side effects) */
    struct rt_dynsym *syms = NULL;
    int nsyms = 0;
    lpuRtError e = parse_dynsyms(c, img, imageSize, eh, &syms, &nsyms);
    if (e != LPU_RT_SUCCESS) {
        rt_set_last_error(e);
        return e;
    }

    /* symbol table (extracted from the image at load time; no section headers in the window) */
    struct lpu_function *funcs = NULL;
    int nfuncs = 0;
    e = parse_symbols(img, imageSize, eh, &funcs, &nfuncs);
    if (e == LPU_RT_ERROR_OUT_OF_MEMORY) {
        free(syms);
        rt_set_last_error(e);
        return e;
    }
    if (e != LPU_RT_SUCCESS) {
        free(syms);
        goto invalid;
    }

    /* device DRAM allocation (BSS included; alignment = max(PT_LOAD p_align, 256)) */
    lpu_memblk_t *blk = NULL;
    uint64_t addr = 0;
    e = rt_pool_alloc(&c->pool, max_v - min_v, align, &blk, &addr);
    if (e != LPU_RT_SUCCESS) {
        free(funcs);
        free(syms);
        rt_set_last_error(e);
        return e;
    }
    rt_log_("ModuleLoad: span=%#llx align=%#llx -> dev_addr=%#llx rela(dyn=%zu plt=%zu)",
            (unsigned long long)(max_v - min_v), (unsigned long long)align,
            (unsigned long long)addr, rdyn_sz, rplt_sz);

    struct lpu_module *m = calloc(1, sizeof(*m));
    if (!m) {
        free(funcs);
        free(syms);
        rt_pool_free(&c->pool, blk);
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    }
    m->blk = blk;
    m->min_vaddr = min_v;
    m->span_vaddr = max_v - min_v;
    m->base = addr - min_v; /* device-side address where vaddr 0 lands */
    m->entry_vaddr = eh->e_entry;
    m->funcs = funcs;
    m->nfuncs = nfuncs;

    uint8_t *win = rt_dram_ptr(&c->be, addr);
    rt_dmemset(win, 0, (size_t)m->span_vaddr); /* zero BSS + segment gaps */
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        rt_dmemcpy(win + (ph[i].p_vaddr - min_v), img + ph[i].p_offset, ph[i].p_filesz);
    }

    /* relocations: two regions, written into the window in place on the host side (fw never acts as
     * an ELF loader) */
    if (rdyn) {
        e = apply_rela(win, m, syms, nsyms, rdyn, rdyn_sz);
        if (e != LPU_RT_SUCCESS)
            goto invalid_free;
    }
    if (rplt) {
        e = apply_rela(win, m, syms, nsyms, rplt, rplt_sz);
        if (e != LPU_RT_SUCCESS)
            goto invalid_free;
    }

    free(syms);
    *module = m;
    rt_log_("ModuleLoad: done base=%#llx entry=%#llx nfuncs=%d", (unsigned long long)m->base,
            (unsigned long long)m->entry_vaddr, m->nfuncs);
    return LPU_RT_SUCCESS;

invalid_free:
    rt_pool_free(&c->pool, blk);
    free(funcs);
    free(syms);
    free(m);
invalid:
    rt_set_last_error(LPU_RT_ERROR_MODULE_INVALID);
    return LPU_RT_ERROR_MODULE_INVALID;
}

LPU_RT_API lpuRtError lpuRtModuleGetFunction(lpuRtModule module, const char *name,
                                             lpuRtFunction *function) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!module || !module->blk || !function)
        return LPU_RT_ERROR_INVALID_HANDLE;

    struct lpu_function *f = calloc(1, sizeof(*f));
    if (!f)
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    f->mod = module;

    if (!name) {
        f->entry_off = module->entry_vaddr;
        snprintf(f->name, sizeof(f->name), "<entry>");
    } else {
        bool found = false;
        for (int i = 0; i < module->nfuncs; i++) {
            if (strcmp(module->funcs[i].name, name) == 0) {
                f->entry_off = module->funcs[i].entry_off;
                snprintf(f->name, sizeof(f->name), "%s", name);
                found = true;
                break;
            }
        }
        if (!found) {
            free(f);
            rt_set_last_error(LPU_RT_ERROR_INVALID_VALUE);
            return LPU_RT_ERROR_INVALID_VALUE;
        }
    }

    if (f->entry_off < module->min_vaddr ||
        f->entry_off >= module->min_vaddr + module->span_vaddr) {
        free(f);
        rt_set_last_error(LPU_RT_ERROR_MODULE_INVALID);
        return LPU_RT_ERROR_MODULE_INVALID;
    }

    *function = f;
    rt_log_("GetFunction: '%s' entry_off=%#llx (pc=%#llx)", f->name,
            (unsigned long long)f->entry_off, (unsigned long long)(f->mod->base + f->entry_off));
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtModuleUnload(lpuRtModule module) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!module || !module->blk)
        return LPU_RT_ERROR_INVALID_HANDLE;

    /* the caller must guarantee no unfinished launch still references the module (StreamSync first)
     */
    rt_pool_free(&c->pool, module->blk);
    free(module->funcs);
    free(module);
    return LPU_RT_SUCCESS;
}

LPU_RT_API lpuRtError lpuRtModuleLoadFile(const char *path, lpuRtModule *module) {
    lpu_ctx_t *c = rt_ctx_checked();
    if (!c) {
        rt_set_last_error(LPU_RT_ERROR_NOT_INITIALIZED);
        return LPU_RT_ERROR_NOT_INITIALIZED;
    }
    if (!path || !module)
        return LPU_RT_ERROR_INVALID_VALUE;

    FILE *f = fopen(path, "rb");
    if (!f) {
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    rewind(f);

    void *img = malloc((size_t)sz);
    if (!img) {
        fclose(f);
        rt_set_last_error(LPU_RT_ERROR_OUT_OF_MEMORY);
        return LPU_RT_ERROR_OUT_OF_MEMORY;
    }
    if (fread(img, 1, (size_t)sz, f) != (size_t)sz) {
        free(img);
        fclose(f);
        rt_set_last_error(LPU_RT_ERROR_IO);
        return LPU_RT_ERROR_IO;
    }
    fclose(f);

    lpuRtError e = lpuRtModuleLoad(img, (size_t)sz, module);
    free(img);
    return e;
}
