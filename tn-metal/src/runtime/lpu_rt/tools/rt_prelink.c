/*
 * rt_prelink.c — loader self-check (host ctest) + QEMU smoke prelinked image producer (both in one
 * binary)
 *
 * one binary, two uses (lpu-sw-impl.md A8 in docs/rt/):
 *  1. host ctest layer: verify loader logic (ELF parsing / symbol resolution / slot value
 * computation). Differential check = this tool side independently parses both ELFs and computes the
 * expected value of every relocation slot, then reads the DRAM window back via lpuRtSimPtrToHost
 * and compares against what was actually written — not a replay of loader conclusions, a real
 * check.
 *  2. Real execution lives in the th-c906 QEMU smoke layer (fw selftest ③ really calls fw exports
 * through the PLT); this tool only produces sample_op.prelink.bin.
 *
 * Pipeline: lpuRtInit(SIM) → 2 negative cases (bad ELF header / unregistered fw ELF) → dummy
 * allocations push the DRAM pool to LPU_OP_BASE → lpuRtSetFirmwareElf → lpuRtModuleLoad → slot
 * value differential check → export [16B header + module load window].
 *
 * Dummy allocation arithmetic: the pool starts at 0x80000000 with 256B granularity — canary =
 * Malloc(1) takes the pool head [base, base+256), bridge = Malloc(OP_BASE - canary - 256) fills the
 * rest in one block, so the module allocation (aligned to max(p_align, 256) | OP_BASE) lands
 * exactly at LPU_OP_BASE.
 */
#include <elf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lpu/lpu_prelink.h"
#include "lpu_rt.h"
#include "lpu_rt_sim.h"

/* old-host elf.h (Ubuntu 20.04 glibc 2.31) may lack RISC-V constants */
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

/* matches the hardcoded alignment in rt_mem.c lpuRtMalloc (the public API does not expose that
 * granularity): canary Malloc(1) really takes 256B, the bridge size is sized to close exactly at
 * LPU_OP_BASE */
#define RT_PRELINK_POOL_ALIGN 256u

static int g_fail;

#define CHECK(cond, fmt, ...)                                                                      \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__);                    \
        }                                                                                          \
    } while (0)

static void *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    void *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

/* ---------------- tool-side ELF parsing (independent of the loader implementation)
 * ---------------- */

static bool elf_ok(const uint8_t *img, size_t size) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    if (size < sizeof(*eh))
        return false;
    return memcmp(eh->e_ident, ELFMAG, SELFMAG) == 0 && eh->e_ident[EI_CLASS] == ELFCLASS64 &&
           eh->e_ident[EI_DATA] == ELFDATA2LSB && eh->e_machine == EM_RISCV;
}

static const Elf64_Phdr *elf_phdrs(const uint8_t *img, size_t size, int *phnum) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > size)
        return NULL;
    *phnum = eh->e_phnum;
    return (const Elf64_Phdr *)(img + eh->e_phoff);
}

/* min/max PT_LOAD vaddr (mirror of the loader span computation) */
static bool elf_load_span(const uint8_t *img, size_t size, uint64_t *min_v, uint64_t *max_v) {
    int phnum;
    const Elf64_Phdr *ph = elf_phdrs(img, size, &phnum);
    if (!ph)
        return false;
    uint64_t lo = ~0ull, hi = 0;
    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        if (ph[i].p_vaddr < lo)
            lo = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > hi)
            hi = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (lo == ~0ull || hi <= lo)
        return false;
    *min_v = lo;
    *max_v = hi;
    return true;
}

/* PT_DYNAMIC d_ptr (vaddr) → file offset (mapped through PT_LOAD filesz) */
static bool elf_vaddr_to_off(const uint8_t *img, size_t size, uint64_t vaddr, size_t *off) {
    int phnum;
    const Elf64_Phdr *ph = elf_phdrs(img, size, &phnum);
    if (!ph)
        return false;
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

/* fetch the two relocation regions via PT_DYNAMIC (the same source the loader uses) */
static bool elf_rela_regions(const uint8_t *img, size_t size, const Elf64_Rela **rdyn,
                             size_t *rdyn_sz, const Elf64_Rela **rplt, size_t *rplt_sz) {
    int phnum;
    const Elf64_Phdr *ph = elf_phdrs(img, size, &phnum);
    if (!ph)
        return false;
    *rdyn = NULL;
    *rdyn_sz = 0;
    *rplt = NULL;
    *rplt_sz = 0;

    for (int i = 0; i < phnum; i++) {
        if (ph[i].p_type != PT_DYNAMIC || ph[i].p_filesz == 0)
            continue;
        if (ph[i].p_offset + ph[i].p_filesz > size)
            return false;
        const Elf64_Dyn *d = (const Elf64_Dyn *)(img + ph[i].p_offset);
        size_t ndyn = ph[i].p_filesz / sizeof(Elf64_Dyn);
        for (size_t k = 0; k < ndyn && d[k].d_tag != DT_NULL; k++) {
            size_t off;
            switch (d[k].d_tag) {
            case DT_RELA:
                if (!elf_vaddr_to_off(img, size, d[k].d_un.d_ptr, &off))
                    return false;
                *rdyn = (const Elf64_Rela *)(img + off);
                break;
            case DT_JMPREL:
                if (!elf_vaddr_to_off(img, size, d[k].d_un.d_ptr, &off))
                    return false;
                *rplt = (const Elf64_Rela *)(img + off);
                break;
            case DT_RELASZ:
                *rdyn_sz = d[k].d_un.d_val;
                break;
            case DT_PLTRELSZ:
                *rplt_sz = d[k].d_un.d_val;
                break;
            default:
                break;
            }
        }
        /* the range lies fully inside the image */
        if ((*rdyn && (uint64_t)((const uint8_t *)*rdyn - img) + *rdyn_sz > size) ||
            (*rplt && (uint64_t)((const uint8_t *)*rplt - img) + *rplt_sz > size))
            return false;
        return true;
    }
    return true; /* no PT_DYNAMIC: no relocations beyond RELATIVE-only */
}

/* .dynsym + .dynstr (located via the section table) */
static bool elf_dynsyms(const uint8_t *img, size_t size, const Elf64_Sym **syms, size_t *nsyms,
                        const char **strtab, size_t *strsz) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    if (eh->e_shoff == 0 || eh->e_shnum == 0 ||
        eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) > size)
        return false;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(img + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_DYNSYM || sh[i].sh_link >= eh->e_shnum)
            continue;
        const Elf64_Shdr *str = &sh[sh[i].sh_link];
        if (sh[i].sh_offset + sh[i].sh_size > size || str->sh_offset + str->sh_size > size)
            return false;
        *syms = (const Elf64_Sym *)(img + sh[i].sh_offset);
        *nsyms = sh[i].sh_size / sizeof(Elf64_Sym);
        *strtab = (const char *)(img + str->sh_offset);
        *strsz = str->sh_size;
        return true;
    }
    return false;
}

/* look up a function symbol address by name in the fw ELF .symtab (an independent view,
 * complementary to the loader prefix filter) */
static uint64_t elf_fw_sym_addr(const uint8_t *img, size_t size, const char *name) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    if (eh->e_shoff == 0 || eh->e_shnum == 0 ||
        eh->e_shoff + (uint64_t)eh->e_shnum * sizeof(Elf64_Shdr) > size)
        return 0;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(img + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_SYMTAB || sh[i].sh_link >= eh->e_shnum)
            continue;
        const Elf64_Shdr *str = &sh[sh[i].sh_link];
        if (sh[i].sh_offset + sh[i].sh_size > size || str->sh_offset + str->sh_size > size)
            continue;
        const Elf64_Sym *sym = (const Elf64_Sym *)(img + sh[i].sh_offset);
        size_t nsym = sh[i].sh_size / sizeof(Elf64_Sym);
        const char *st = (const char *)(img + str->sh_offset);
        for (size_t s = 1; s < nsym; s++) {
            if (sym[s].st_name >= str->sh_size)
                continue;
            if (strcmp(st + sym[s].st_name, name) == 0 && sym[s].st_shndx != SHN_UNDEF)
                return sym[s].st_value;
        }
    }
    return 0;
}

/* ---------------- slot value differential check ---------------- */

static void verify_region(uint64_t m_base, const Elf64_Rela *rela, size_t sz, const Elf64_Sym *syms,
                          size_t nsyms, const char *strtab, size_t strsz, const uint8_t *fw,
                          size_t fw_size, const char *tag) {
    size_t n = 0;
    for (size_t off = 0; off + sizeof(Elf64_Rela) <= sz; off += sizeof(Elf64_Rela), n++) {
        const Elf64_Rela *r = (const Elf64_Rela *)((const uint8_t *)rela + off);
        uint32_t type = (uint32_t)ELF64_R_TYPE(r->r_info);
        uint32_t si = (uint32_t)ELF64_R_SYM(r->r_info);
        uint64_t expect;

        if (type == R_RISCV_RELATIVE) {
            expect = m_base + (uint64_t)r->r_addend;
        } else if (type == R_RISCV_64 || type == R_RISCV_JUMP_SLOT) {
            CHECK(si > 0 && si < nsyms, "%s symidx %u out of range", tag, si);
            if (si == 0 || si >= nsyms)
                continue;
            if (syms[si].st_shndx == SHN_UNDEF) {
                CHECK(syms[si].st_name < strsz, "%s UND name off", tag);
                if (syms[si].st_name >= strsz)
                    continue;
                const char *nm = strtab + syms[si].st_name;
                uint64_t fw_addr = elf_fw_sym_addr(fw, fw_size, nm);
                CHECK(fw_addr != 0, "%s '%s' not in fw .symtab", tag, nm);
                if (fw_addr == 0)
                    continue;
                expect = fw_addr + (uint64_t)r->r_addend;
            } else {
                expect = m_base + syms[si].st_value + (uint64_t)r->r_addend;
            }
        } else {
            CHECK(false, "%s unexpected reloc type %u", tag, type);
            continue;
        }

        void *host;
        CHECK(lpuRtSimPtrToHost(m_base + r->r_offset, &host) == LPU_RT_SUCCESS,
              "%s slot vaddr 0x%llx out of window", tag, (unsigned long long)r->r_offset);
        if (!host)
            continue;
        uint64_t actual = *(uint64_t *)host;
        CHECK(actual == expect, "%s slot @+0x%llx: actual 0x%llx != expect 0x%llx", tag,
              (unsigned long long)r->r_offset, (unsigned long long)actual,
              (unsigned long long)expect);
    }
    printf("  %-8s %2zu slots verified\n", tag, n);
}

int main(void) {
    printf("== rt_prelink: loader self-check + prelink image ==\n");

    /* real artifacts (missing = FAIL with guidance, never a silent skip) */
    size_t fw_size = 0, dso_size = 0;
    uint8_t *fw = read_file(RT_PRELINK_FW_ELF, &fw_size);
    uint8_t *dso = read_file(RT_PRELINK_DSO, &dso_size);
    if (!fw || !dso) {
        printf("FAIL missing real artifacts:\n"
               "  fw ELF : %s (%s)\n  DSO   : %s (%s)\n"
               "  -> run make -C src selftest first\n",
               RT_PRELINK_FW_ELF, fw ? "ok" : "MISSING", RT_PRELINK_DSO, dso ? "ok" : "MISSING");
        free(fw);
        free(dso);
        return 1;
    }
    CHECK(elf_ok(fw, fw_size), "fw ELF header");
    CHECK(elf_ok(dso, dso_size), "DSO ELF header");
    if (g_fail)
        return 1;

    /* tool-side pre-parse (an independent source of expected values) */
    uint64_t min_v, max_v;
    CHECK(elf_load_span(dso, dso_size, &min_v, &max_v), "DSO PT_LOAD span");
    const Elf64_Rela *rdyn, *rplt;
    size_t rdyn_sz, rplt_sz;
    CHECK(elf_rela_regions(dso, dso_size, &rdyn, &rdyn_sz, &rplt, &rplt_sz), "DSO rela regions");
    const Elf64_Sym *syms;
    size_t nsyms;
    const char *strtab;
    size_t strsz;
    CHECK(elf_dynsyms(dso, dso_size, &syms, &nsyms, &strtab, &strsz), "DSO .dynsym");
    uint64_t fw_core_id_addr = elf_fw_sym_addr(fw, fw_size, "lpu_fw_core_id");
    CHECK(fw_core_id_addr != 0, "fw .symtab lacks lpu_fw_core_id (keep-alive?)");
    printf("fw export lpu_fw_core_id @ 0x%llx\n", (unsigned long long)fw_core_id_addr);
    printf("DSO span [0x%llx, 0x%llx)\n", (unsigned long long)min_v, (unsigned long long)max_v);
    if (g_fail)
        return 1;

    /* --- pipeline (production runtime APIs, sim backend) --- */

    lpuRtError e = lpuRtInit(LPU_RT_INIT_SIM);
    CHECK(e == LPU_RT_SUCCESS, "lpuRtInit(SIM) = %d", e);
    if (e != LPU_RT_SUCCESS)
        return 1;

    /* negative case: bad ELF header (all-zero bytes) */
    static const uint8_t zeros[64];
    lpuRtModule m = NULL;
    e = lpuRtModuleLoad(zeros, sizeof(zeros), &m);
    CHECK(e == LPU_RT_ERROR_MODULE_INVALID, "zeros load = %d (want MODULE_INVALID)", e);
    CHECK(m == NULL, "module handle leaked on failure");

    /* negative case: no registered fw ELF → UND symbols have no source (the strict ABI rejects the
     * whole module) */
    e = lpuRtModuleLoad(dso, dso_size, &m);
    CHECK(e == LPU_RT_ERROR_SYMBOL_NOT_FOUND,
          "DSO load before SetFirmwareElf = %d (want SYMBOL_NOT_FOUND)", e);
    CHECK(m == NULL, "module handle leaked on failure");

    /* dummy allocations: push the pool to LPU_OP_BASE (canary takes the pool head 256B, bridge
     * takes the rest) */
    lpuRtDevicePtr canary = 0, bridge = 0;
    e = lpuRtMalloc(&canary, 1);
    CHECK(e == LPU_RT_SUCCESS, "canary malloc = %d", e);
    e = lpuRtMalloc(&bridge, LPU_OP_BASE - canary - RT_PRELINK_POOL_ALIGN);
    CHECK(e == LPU_RT_SUCCESS, "bridge malloc = %d", e);
    printf("dummy: canary=0x%llx bridge=0x%llx (+0x%x bytes)\n", (unsigned long long)canary,
           (unsigned long long)bridge, LPU_OP_BASE - (uint32_t)canary - RT_PRELINK_POOL_ALIGN);

    /* fw export table + the real load */
    e = lpuRtSetFirmwareElf(fw, fw_size);
    CHECK(e == LPU_RT_SUCCESS, "SetFirmwareElf = %d", e);
    e = lpuRtModuleLoad(dso, dso_size, &m);
    CHECK(e == LPU_RT_SUCCESS, "ModuleLoad = %d", e);
    if (e != LPU_RT_SUCCESS) {
        printf("FAIL ModuleLoad: %s\n", lpuRtGetErrorString(e));
        return 1;
    }

    uint64_t m_base = 0;
    CHECK(lpuRtSimModuleBase(m, &m_base) == LPU_RT_SUCCESS, "SimModuleBase");
    CHECK(m_base + min_v == LPU_OP_BASE,
          "module landed at 0x%llx (want 0x%x): dummy-alloc math broken",
          (unsigned long long)(m_base + min_v), LPU_OP_BASE);
    printf("module: base=0x%llx window=[0x%llx, +0x%llx)\n", (unsigned long long)m_base,
           (unsigned long long)(m_base + min_v), (unsigned long long)(max_v - min_v));
    if (g_fail)
        return 1;

    /* slot value differential check (tool-side expected values vs DRAM window actuals) */
    printf("reloc slot differential check:\n");
    if (rdyn)
        verify_region(m_base, rdyn, rdyn_sz, syms, nsyms, strtab, strsz, fw, fw_size, "rela.dyn");
    if (rplt)
        verify_region(m_base, rplt, rplt_sz, syms, nsyms, strtab, strsz, fw, fw_size, "rela.plt");

    /* entry resolution */
    lpuRtFunction fn_e = NULL, fn_n = NULL;
    CHECK(lpuRtModuleGetFunction(m, NULL, &fn_e) == LPU_RT_SUCCESS, "GetFunction(entry)");
    CHECK(lpuRtModuleGetFunction(m, "op_entry", &fn_n) == LPU_RT_SUCCESS, "GetFunction(op_entry)");

    /* --- export the prelinked image: [16B header + load window] --- */
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)dso;
    CHECK(eh->e_entry >= min_v && eh->e_entry < max_v, "e_entry 0x%llx outside PT_LOAD span",
          (unsigned long long)eh->e_entry);
    void *win_host;
    CHECK(lpuRtSimPtrToHost(m_base + min_v, &win_host) == LPU_RT_SUCCESS, "window ptr");
    lpu_prelink_hdr_t hdr = {
        .magic = LPU_PRELINK_MAGIC,
        .base = (uint32_t)(m_base + min_v),
        .entry_off = (uint32_t)(eh->e_entry - min_v),
        .size = (uint32_t)(max_v - min_v),
    };
    FILE *f = fopen(RT_PRELINK_OUT, "wb");
    CHECK(f != NULL, "open %s", RT_PRELINK_OUT);
    if (f) {
        CHECK(fwrite(&hdr, sizeof(hdr), 1, f) == 1, "write header");
        CHECK(fwrite(win_host, hdr.size, 1, f) == 1, "write image");
        fclose(f);
        printf("prelink image: %s (base=0x%x entry_off=0x%x size=0x%x)\n", RT_PRELINK_OUT, hdr.base,
               hdr.entry_off, hdr.size);
    }

    /* teardown (exercising the Unload/Free pool paths along the way) */
    lpuRtModuleUnload(m);
    lpuRtFree(bridge);
    lpuRtFree(canary);
    lpuRtShutdown();

    free(fw);
    free(dso);
    if (g_fail) {
        printf("== rt_prelink: %d FAIL ==\n", g_fail);
        return 1;
    }
    printf("== rt_prelink: PASS ==\n");
    return 0;
}
