/*
 * Copyright distributed.net 2001-2004 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * Wrapper around ogr.cpp for all processor WITH a fast bsr instruction.
 * (ie, PPro, PII, PIII)
 *
 * $Id: ogr-a.cpp,v 1.2.4.2 2004/08/14 23:32:51 kakace Exp $
*/

#define OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM   2 /* 0-2 - '100% asm'      */
#define OGROPT_STRENGTH_REDUCE_CHOOSE         1 /* 0/1 - 'yes' (default) */
#define OGROPT_NO_FUNCTION_INLINE             0 /* 0/1 - 'no'  (default) */
#define OGROPT_HAVE_OGR_CYCLE_ASM             0 /* 0-2 - 'no'  (default) */
#define OGROPT_CYCLE_CACHE_ALIGN              0 /* 0/1 - 'no'  (default) */
#define OGROPT_ALTERNATE_CYCLE                0 /* 0-2 - 'no'  (default) */
#define OGROPT_ALTERNATE_COMP_LEFT_LIST_RIGHT 0 /* 0/1 - 'std' (default) */

#include "asm-x86.h"

#if (OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM == 2) && !defined(__CNTLZ)
  #warning Macro __CNTLZ not defined. OGROPT_FFZ reset to 0.
  #undef  OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM
  #define OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM   0
#endif

#include "ansi/ogr.cpp"
