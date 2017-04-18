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

#ifndef LIBPSU_PSUCPU_H
#define LIBPSU_PSUCPU_H

#include <stdint.h>

/**
 * Saved register values after the "cpuid" instruction.  For details, see:
 * http://en.wikipedia.org/wiki/CPUID
 */
typedef struct psu_cpuid_s {
    uint32_t pc_ax;
    uint32_t pc_bx;
    uint32_t pc_cx;
    uint32_t pc_dx;
} psu_cpuid_t;

/* Flags for "pc_cx" after cpuid: */
#define CPU_CX_SSE3 (1<<0) /* Prescott New Instructions-SSE3 (PNI) */
#define CPU_CX_PCLMULQDQ (1<<1) /* PCLMULQDQ support */
#define CPU_CX_DTES64 (1<<2) /* 64-bit debug store (edx bit 21) */
#define CPU_CX_MONITOR (1<<3) /* MONITOR and MWAIT instructions (SSE3) */

#define CPU_CX_DSCPL (1<<4) /* CPL qualified debug store */
#define CPU_CX_VMX (1<<5) /* Virtual Machine eXtensions */
#define CPU_CX_SMX (1<<6) /* Safer Mode Extensions (LaGrande) */
#define CPU_CX_EST (1<<7) /* Enhanced SpeedStep */

#define CPU_CX_TM2 (1<<8) /* Thermal Monitor 2 */
#define CPU_CX_SSSE3S (1<<9) /* Supplemental SSE3 instructions */
#define CPU_CX_CNXTID (1<<10) /* L1 Context ID */
#define CPU_CX_SDBG (1<<11) /* Silicon Debug interface */

#define CPU_CX_FMA (1<<12) /* Fused multiply-add (FMA3) */
#define CPU_CX_CX16 (1<<13) /* CMPXCHG16B instruction */
#define CPU_CX_XTPR (1<<14) /* Can disable sending task priority messages */
#define CPU_CX_PDCM (1<<15) /* Perfmon & debug capability */

#define CPU_CX_PAGE (1<<16) /* Attribute Table (reserved) */
#define CPU_CX_PCID (1<<17) /* Process context identifiers (CR4 bit 17) */
#define CPU_CX_DCA (1<<18) /* Direct cache access for DMA writes[12][13] */
#define CPU_CX_SSE41 (1<<19) /* SSE4.1 instructions */

#define CPU_CX_SSE42 (1<<20) /* SSE4.2 instructions */
#define CPU_CX_X2APIC (1<<21) /* x2APIC support */
#define CPU_CX_MOVBE (1<<22) /* MOVBE instruction (big-endian) */
#define CPU_CX_POPCNT (1<<23) /* POPCNT instruction */

#define CPU_CX_TSCDL (1<<24) /* APIC supports one-shot operation using a TSC deadline value */
#define CPU_CX_AES (1<<25) /* AES instruction set */
#define CPU_CX_XSAVE (1<<26) /* XSAVE, XRESTOR, XSETBV, XGETBV */
#define CPU_CX_OSXSAVE (1<<27) /* XSAVE enabled by OS */

#define CPU_CX_AVX (1<<28) /* Advanced Vector Extensions */
#define CPU_CX_F16C (1<<29) /* F16C (half-precision) FP support */
#define CPU_CX_RDRND (1<<30) /* RDRAND (on-chip random number generator) support */
#define CPU_CX_HYPERVISOR (1<<31) /* Running on a hypervisor (always 0 on a real CPU, but also with some hypervisors) */

/* Flags for "pc_dx" after cpuid: */
#define CPU_DX_FPU (1<<0) /* Onboard x87 FPU */
#define CPU_DX_VME (1<<1) /* Virtual 8086 mode extensions (such as VIF, VIP, PIV) */
#define CPU_DX_DE (1<<2) /* Debugging extensions (CR4 bit 3) */
#define CPU_DX_PSE (1<<3) /* Page Size Extension */

#define CPU_DX_TSC (1<<4) /* Time Stamp Counter */
#define CPU_DX_MSR (1<<5) /* Model-specific registers */
#define CPU_DX_PAE (1<<6) /* Physical Address Extension */
#define CPU_DX_MCE (1<<7) /* Machine Check Exception */

#define CPU_DX_CX8 (1<<8) /* CMPXCHG8 (compare-and-swap) instruction */
#define CPU_DX_APIC (1<<9) /* Onboard Advanced Programmable Interrupt Controller */
#define CPU_DX_RESERVED10 (1<<10) /* (reserved ) */
#define CPU_DX_SEP (1<<11) /* SYSENTER and SYSEXIT instructions */

#define CPU_DX_MTRR (1<<12) /* Memory Type Range Registers */
#define CPU_DX_PGE (1<<13) /* Page Global Enable bit in CR4 */
#define CPU_DX_MCA (1<<14) /* Machine check architecture */
#define CPU_DX_CMOV (1<<15) /* Conditional move and FCMOV instructions */

#define CPU_DX_PAT (1<<16) /*  */
#define CPU_DX_PSE36 (1<<17) /*-36 36-bit page size extension */
#define CPU_DX_PSN (1<<18) /* Processor Serial Number */
#define CPU_DX_CLFSH (1<<19) /* CLFLUSH instruction (SSE2) */

#define CPU_DX_RESERVED20 (1<<20) /* (reserved ) */
#define CPU_DX_DS (1<<21) /* Debug store: save trace of executed jumps */
#define CPU_DX_ACPI (1<<22) /* Onboard thermal control MSRs for ACPI */
#define CPU_DX_MMX (1<<23) /* MMX instructions */

#define CPU_DX_FXSR (1<<24) /* FXSAVE, FXRESTOR instructions, CR4 bit 9 */
#define CPU_DX_SSE (1<<25) /* SSE instructions (a.k.a. Katmai New Instructions) */
#define CPU_DX_SSE2 (1<<26) /* SSE2 instructions */
#define CPU_DX_SS (1<<27) /* CPU cache supports self-snoop */

#define CPU_DX_HTT (1<<28) /* Hyper-threading */
#define CPU_DX_TM (1<<29) /* Thermal monitor automatically limits temperature */
#define CPU_DX_IA64 (1<<30) /* IA64 processor emulating x86 */
#define CPU_DX_PBE (1<<31) /* Pending Break Enable (PBE# pin) wakeup support */

void
psu_cpu_get_info (uint32_t which, psu_cpuid_t *pcp);

void
psu_dump_cpu_info (int);

#endif /* LIBPSU_PSUCPU_H */
