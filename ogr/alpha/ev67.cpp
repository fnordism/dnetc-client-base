/*
 * Copyright distributed.net 2002-2003 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 */

const char *ogr_ev67_cpp(void) {
return "@(#)$Id: ev67.cpp,v 1.2.4.3.2.1 2004/08/08 19:29:18 kakace Exp $"; }

#if defined(__GNUC__)
  #define OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM   2 /* 0-2 - "100% asm"      */
  #define OGROPT_STRENGTH_REDUCE_CHOOSE         0 /* 0/1 - 'no'            */
  #define OGROPT_NO_FUNCTION_INLINE             0 /* 0/1 - 'no'  (default) */
  #define OGROPT_HAVE_OGR_CYCLE_ASM             0 /* 0-2 - 'no'  (default) */
  #define OGROPT_CYCLE_CACHE_ALIGN              0 /* 0/1 - 'no'  (default) */
  #define OGROPT_ALTERNATE_CYCLE                0 /* 0-2 - 'no'  (default) */
  #define OGROPT_ALTERNATE_COMP_LEFT_LIST_RIGHT 0 /* 0/1 - 'std' (default) */

#else /* Compaq CC */
  #include <c_asm.h>
  #define OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM   2 /* 0-2 - "100% asm"      */
  #define OGROPT_STRENGTH_REDUCE_CHOOSE         0 /* 0/1 - 'no'            */
  #define OGROPT_NO_FUNCTION_INLINE             0 /* 0/1 - 'no'  (default) */
  #define OGROPT_HAVE_OGR_CYCLE_ASM             0 /* 0-2 - 'no'  (default) */
  #define OGROPT_CYCLE_CACHE_ALIGN              0 /* 0/1 - 'no'  (default) */
  #define OGROPT_ALTERNATE_CYCLE                0 /* 0-2 - 'no'  (default) */
  #define OGROPT_ALTERNATE_COMP_LEFT_LIST_RIGHT 0 /* 0/1 - 'std' (default) */
#endif

#define ALPHA_CIX
#define OGR_GET_DISPATCH_TABLE_FXN    ogr_get_dispatch_table_cix


#if (OGROPT_HAVE_FIND_FIRST_ZERO_BIT_ASM == 2)
  #if defined(__GNUC__)
    static __inline__ int __CNTLZ__(register unsigned int i)
    { 
      register unsigned long j = (unsigned long)i << 32;
      __asm__ ("ctlz %0,%0" : "=r"(j) : "0" (j));
      return (int)j;
    }
  #else
    static inline int __CNTLZ__(register unsigned int i)
    {
      __int64 r = asm("ctlz %a0, %v0;", (unsigned long)i << 32);
      return (int)r;
    } 
  #endif
  #define __CNTLZ(x) (__CNTLZ__(~(x))+1)
#endif

#include "ansi/ogr.cpp"
