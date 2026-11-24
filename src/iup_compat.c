/**
 * @             Project: BUAA GPA Studio
 * @                File: iup_compat.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.19
 * @         Description: Bridge legacy IUP symbols to the C-Dev UCRT toolchain.
 * @ Version Information: Created by WangNan, 2026.10.19
**/

#include <stdlib.h>

static int iup_compat_argc;
static char **iup_compat_argv;
int *__imp___argc = &iup_compat_argc;
char ***__imp___argv = &iup_compat_argv;

__asm__(".globl _setjmp\n_setjmp:\n jmp __intrinsic_setjmp");
