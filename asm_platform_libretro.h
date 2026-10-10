/* Replacement for beebjit's asm/asm_platform.h when built as a libretro core.
 *
 * It is force-included (-include) with BEEBJIT_ASM_PLATFORM_H predefined so the
 * original header becomes a no-op, leaving beebjit's tree untouched.
 *
 * Why: beebjit maps the emulated machine, the JIT and the inturbo code at fixed
 * virtual addresses. The stock Linux values sit in the low 32-bit range
 * (0x06000000-0x80000000), which is fine for a standalone binary but inside a
 * frontend such as RetroArch the process heap has often already grown over
 * them, and beebjit then bails with "mmap in wrong location". On ARM64 the
 * generated code supports 64-bit addresses (macOS already uses them), so we
 * move everything into the middle of the 39-bit user address space, where no
 * heap or library mapping will reach.
 *
 * K_JIT_ADDR and K_INTURBO_ADDR are used as `orr` immediates and so must be
 * valid ARM64 logical immediates (a contiguous run of set bits). 0x3ffc000000
 * (bits 26-37) and 0x3fff000000 (bits 24-37) both are.
 */
#ifndef BEEBJIT_ASM_PLATFORM_LIBRETRO_H
#define BEEBJIT_ASM_PLATFORM_LIBRETRO_H

#if __APPLE__
#define ASM_SYM(x) _ ## x
#else
#define ASM_SYM(x) x
#endif

#define K_JIT_BYTES_SHIFT                  7

#if defined(__aarch64__) && !__APPLE__
#define K_BBC_MEM_RAW_ADDR                 0x300f008000
#define K_JIT_ADDR                         0x3ffc000000
#define K_INTURBO_ADDR                     0x3fff000000
#define K_ASM_TABLE_ADDR                   0x0          /* Unused on ARM64. */
#elif __APPLE__ && defined(__aarch64__)
#define K_BBC_MEM_RAW_ADDR                 0x700f008000
#define K_JIT_ADDR                         0x7ffc000000
#define K_INTURBO_ADDR                     0x7fff000000
#define K_ASM_TABLE_ADDR                   0x0
#elif __APPLE__
#define K_BBC_MEM_RAW_ADDR                 0x70008000
#define K_JIT_ADDR                         0x75000000
#define K_INTURBO_ADDR                     0x76000000
#define K_ASM_TABLE_ADDR                   0x77000000
#define K_JIT_TRAMPOLINES_ADDR             0x77800000
#else
/* x64 Linux: the JIT needs 32-bit absolute addresses, keep beebjit's values. */
#define K_BBC_MEM_RAW_ADDR                 0x0f008000
#define K_JIT_ADDR                         0x06000000
#define K_INTURBO_ADDR                     0x07000000
#define K_ASM_TABLE_ADDR                   0x50000000
#define K_JIT_TRAMPOLINES_ADDR             0x80000000
#endif

#endif /* BEEBJIT_ASM_PLATFORM_LIBRETRO_H */
