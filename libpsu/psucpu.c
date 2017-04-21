/*
 * Copyright (c) 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * CPU definitions for libpsu (x86 cpuinfo data)
 */

#include <stdint.h>
#include <stdio.h>

#include <libpsu/psucommon.h>
#include <libpsu/psulog.h>
#include <libpsu/psucpu.h>

typedef struct psu_cpu_flags_s {
    uint32_t cf_bit;
    const char *cf_name;
    const char *cf_desc;
} psu_cpu_flags_t;

static psu_cpu_flags_t psu_cpu_cx_flags[] = {
    { CPU_CX_SSE3, "sse3", "Prescott New Instructions-SSE3 (PNI)" },
    { CPU_CX_PCLMULQDQ, "pclmulqdq", "PCLMULQDQ support" },
    { CPU_CX_DTES64, "dtes64", "64-bit debug store (edx bit 21)" },
    { CPU_CX_MONITOR, "monitor", "MONITOR and MWAIT instructions (SSE3)" },
    { CPU_CX_DSCPL, "dscpl", "CPL qualified debug store" },
    { CPU_CX_VMX, "vmx", "Virtual Machine eXtensions" },
    { CPU_CX_SMX, "smx", "Safer Mode Extensions (LaGrande)" },
    { CPU_CX_EST, "est", "Enhanced SpeedStep" },
    { CPU_CX_TM2, "tm2", "Thermal Monitor 2" },
    { CPU_CX_SSSE3S, "ssse3s", "Supplemental SSE3 instructions" },
    { CPU_CX_CNXTID, "cnxtid", "L1 Context ID" },
    { CPU_CX_SDBG, "sdbg", "Silicon Debug interface" },
    { CPU_CX_FMA, "fma", "Fused multiply-add (FMA3)" },
    { CPU_CX_CX16, "cx16", "CMPXCHG16B instruction" },
    { CPU_CX_XTPR, "xtpr", "Can disable sending task priority messages" },
    { CPU_CX_PDCM, "pdcm", "Perfmon & debug capability" },
    { CPU_CX_PAGE, "Page", "Attribute Table (reserved)" },
    { CPU_CX_PCID, "pcid", "Process context identifiers (CR4 bit 17)" },
    { CPU_CX_DCA, "dca", "Direct cache access for DMA writes[12][13]" },
    { CPU_CX_SSE41, "sse41", "SSE4.1 instructions" },
    { CPU_CX_SSE42, "sse42", "SSE4.2 instructions" },
    { CPU_CX_X2APIC, "x2apic", "x2APIC support" },
    { CPU_CX_MOVBE, "movbe", "MOVBE instruction (big-endian)" },
    { CPU_CX_POPCNT, "popcnt", "POPCNT instruction" },
    { CPU_CX_TSCDL, "tscdl",
      "APIC supports one-shot operation using a TSC deadline value" },
    { CPU_CX_AES, "aes", "AES instruction set" },
    { CPU_CX_XSAVE, "xsave", "XSAVE, XRESTOR, XSETBV, XGETBV" },
    { CPU_CX_OSXSAVE, "osxsave", "XSAVE enabled by OS" },
    { CPU_CX_AVX, "avx", "Advanced Vector Extensions" },
    { CPU_CX_F16C, "f16c", "F16C (half-precision) FP support" },
    { CPU_CX_RDRND, "rdrnd",
      "RDRAND (on-chip random number generator) support" },
    { CPU_CX_HYPERVISOR, "hypervisor",
      "Running on a hypervisor (always 0 on a real CPU, but also with some hypervisors)" },
    { 0, NULL, NULL }
};

static psu_cpu_flags_t psu_cpu_dx_flags[] = {
    { CPU_DX_FPU, "fpu", "Onboard x87 FPU" },
    { CPU_DX_VME, "vme",
      "Virtual 8086 mode extensions (such as VIF, VIP, PIV)" },
    { CPU_DX_DE, "de", "Debugging extensions (CR4 bit 3)" },
    { CPU_DX_PSE, "pse", "Page Size Extension" },
    { CPU_DX_TSC, "tsc", "Time Stamp Counter" },
    { CPU_DX_MSR, "msr", "Model-specific registers" },
    { CPU_DX_PAE, "pae", "Physical Address Extension" },
    { CPU_DX_MCE, "mce", "Machine Check Exception" },
    { CPU_DX_CX8, "cx8", "CMPXCHG8 (compare-and-swap) instruction" },
    { CPU_DX_APIC, "apic",
      "Onboard Advanced Programmable Interrupt Controller" },
    { CPU_DX_RESERVED10, "reserved10", "reserved10" },
    { CPU_DX_SEP, "sep", "SYSENTER and SYSEXIT instructions" },
    { CPU_DX_MTRR, "mtrr", "Memory Type Range Registers" },
    { CPU_DX_PGE, "pge", "Page Global Enable bit in CR4" },
    { CPU_DX_MCA, "mca", "Machine check architecture" },
    { CPU_DX_CMOV, "cmov", "Conditional move and FCMOV instructions" },
    { CPU_DX_PAT, "pat", "Page Attribute Table" },
    { CPU_DX_PSE36, "pse36", "36-bit page size extension" },
    { CPU_DX_PSN, "psn", "Processor Serial Number" },
    { CPU_DX_CLFSH, "clfsh", "CLFLUSH instruction (SSE2)" },
    { CPU_DX_RESERVED20, "reserved20", "reserved20" },
    { CPU_DX_DS, "ds", "Debug store: save trace of executed jumps" },
    { CPU_DX_ACPI, "acpi", "Onboard thermal control MSRs for ACPI" },
    { CPU_DX_MMX, "mmx", "MMX instructions" },
    { CPU_DX_FXSR, "fxsr", "FXSAVE, FXRESTOR instructions, CR4 bit 9" },
    { CPU_DX_SSE, "sse", "SSE instructions (a.k.a. Katmai New Instructions)" },
    { CPU_DX_SSE2, "sse2", "SSE2 instructions" },
    { CPU_DX_SS, "ss", "CPU cache supports self-snoop" },
    { CPU_DX_HTT, "htt", "Hyper-threading" },
    { CPU_DX_TM, "tm", "Thermal monitor automatically limits temperature" },
    { CPU_DX_IA64, "ia64", "IA64 processor emulating x86" },
    { CPU_DX_PBE, "pbe", "Pending Break Enable (PBE# pin) wakeup support" },
    { 0, NULL, NULL }
};

void
psu_cpu_get_info (uint32_t which, psu_cpuid_t *pcp)
{
    bzero(pcp, sizeof(*pcp));

#if defined(__x86_64__) || defined(__i386__)
    asm volatile
	("cpuid" : "=a" (pcp->pc_ax), "=b" (pcp->pc_bx),
	 "=c" (pcp->pc_cx), "=d" (pcp->pc_dx)
       : "a" (which), "c" (0));
#endif /* _X86_ */

    psu_log("cpu info(%u): %#x, %#x, %#x, %#x\n",
	    which, pcp->pc_ax, pcp->pc_bx, pcp->pc_cx, pcp->pc_dx);
}

static void
psu_cpu_print_bits (const char *title, int verbose, uint32_t flags,
		    psu_cpu_flags_t *cfp)
{
    uint32_t bit;
    int hit = 0;

    printf("%s:%s", title, verbose ? " {\n" : " ");
    for (bit = 0; bit < 32; bit++)
	if (flags & (1 << bit))
	    printf("%s%s%s%s%s", verbose ? "    " : hit++ ? ", " : "",
		   cfp[bit].cf_name, verbose ? " -- " : "",
		   verbose ? cfp[bit].cf_desc : "", verbose ? "\n" : "");
    printf("%s\n", verbose ? "}" : "");
}

void
psu_dump_cpu_info (int verbose)
{
    char name[18];
    char desc[50];
    psu_cpuid_t pc;

    psu_cpu_get_info(0, &pc);
    /* Funky string-as-integer-values storage; yes it's bx.dx.cx */
    snprintf(name, sizeof(name), "%.4s%.4s%.4s", (const char *) &pc.pc_bx,
	     (const char *) &pc.pc_dx, (const char *) &pc.pc_cx);

    psu_cpu_get_info(0x80000002, &pc);
    memcpy(desc, &pc, sizeof(pc));
    psu_cpu_get_info(0x80000003, &pc);
    memcpy(desc + sizeof(pc), &pc, sizeof(pc));
    psu_cpu_get_info(0x80000004, &pc);
    memcpy(desc + 2 * sizeof(pc), &pc, sizeof(pc));
    desc[48] = '\0';

    printf("cpu: %s (%s)\n", name, desc);

    psu_cpu_get_info(1, &pc);
    psu_cpu_print_bits("cpu flags (cx)", verbose, pc.pc_cx, psu_cpu_cx_flags);
    psu_cpu_print_bits("cpu flags (dx)", verbose, pc.pc_dx, psu_cpu_dx_flags);
}

#if defined(UNIT_TEST)
/*
 * Compile stand alone:
  cc -O2 -g -DUNIT_TEST -I . -I ../ -o ~/bin/cpuid ../libpsu/psucpu.c libpsu/.libs/libpsu.a
*/
int
main (int argc, char **argv)
{
    int verbose = 0;

    if (argc > 1 && strcmp(argv[1], "-v") == 0)
	verbose = 1;

    psu_dump_cpu_info(verbose);
    return 0;
}
#endif /* UNIT_TEST */
