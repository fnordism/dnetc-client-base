/*
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * $Id: ogr.cpp,v 1.1.2.1 2000/08/09 13:16:00 cyp Exp $
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_STATIC_CHOOSEDAT /* choosedat table is static, pre-generated */
/* #define CRC_CHOOSEDAT_ANYWAY */ /* you'll need to link crc32 if this is defd */

/* optimization for machines where shift is faster than mem access */
#ifdef BITOFLIST_OVER_MEMTABLE      /* if memlookup is faster than shift */
#undef NO_BITOFLIST_OVER_MEMTABLE   /* the default is "no" */
#endif

/* optimization for machines with small or no data cache (< 128K) */
/* no gain or loss for machines with big cache */
#if (defined(__PPC__) || defined(ASM_PPC)) || \
  (defined(__GNUC__) && (defined(ASM_SPARC) || defined(ASM_ALPHA) \
                         || defined(ASM_X86) ))
  #define HAVE_REVERSE_FFS_INSN /* have 'ffs from highbit' instruction */
  /* #define FIRSTBLANK_ASM_TEST */ /* define this to test */
#endif  
#ifndef HAVE_REVERSE_FFS_INSN    /* have hardware insn to find first blank */
#define FIRSTBLANK_OVER_MEMTABLE /* the default is "no" */
#endif

/* ------------------------------------------------------------------ */

#if !defined(HAVE_STATIC_CHOOSEDAT) || defined(CRC_CHOOSEDAT_ANYWAY)
#include "crc32.h" /* only need to crc choose_dat if its not static */
#endif
#include "ogr.h"

#define CHOOSEBITS 12
#define MAXBITS    12
#define ttmMAXBITS (32-MAXBITS)

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef HAVE_STATIC_CHOOSEDAT  /* choosedat table is static, pre-generated */
extern const unsigned char ogr_choose_dat[]; /* this is in choosedat.h|c */
#define choose(x,y) (ogr_choose_dat[CHOOSEBITS*(x)+(y+3)]) /*+3 skips header */
#else
static const unsigned char *choosedat;/* set in init_load_choose() */
#define choose(x,y) (choosedat[CHOOSEBITS*(x)+(y)])
#endif

static const int OGR[] = {
  /*  1 */    0,   1,   3,   6,  11,  17,  25,  34,  44,  55,
  /* 11 */   72,  85, 106, 127, 151, 177, 199, 216, 246, 283,
  /* 21 */  333, 356, 372, 425, 480, 492, 553, 585, 623
};
#if defined(FIRSTBLANK_OVER_MEMTABLE) || defined(FIRSTBLANK_ASM_TEST)
static char ogr_first_blank[65537]; /* first blank in 16 bit COMP bitmap, range: 1..16 */
#endif
#if defined(BITOFLIST_OVER_MEMTABLE)
static U ogr_bit_of_LIST[200]; /* which bit of LIST to update */
#endif

static int init_load_choose(void);
static int found_one(struct State *oState);
static int ogr_init(void);
static int ogr_create(void *input, int inputlen, void *state, int statelen);
static int ogr_cycle(void *state, int *pnodes);
static int ogr_getresult(void *state, void *result, int resultlen);
static int ogr_destroy(void *state);
static int ogr_count(void *state);
static int ogr_save(void *state, void *buffer, int buflen);
static int ogr_load(void *buffer, int buflen, void **state);
static int ogr_cleanup(void);

#ifndef OGR_GET_DISPATCH_TABLE_FXN
  #define OGR_GET_DISPATCH_TABLE_FXN ogr_get_dispatch_table
#endif  
extern CoreDispatchTable * OGR_GET_DISPATCH_TABLE_FXN (void);

#if defined(__cplusplus)
}
#endif

/* ------------------------------------------------------------------ */

#define COMP_LEFT_LIST_RIGHT(lev,s)                             \
  {                                                             \
    register int ss = 32 - s;                                   \
    lev->comp[0] = (lev->comp[0] << s) | (lev->comp[1] >> ss);  \
    lev->comp[1] = (lev->comp[1] << s) | (lev->comp[2] >> ss);  \
    lev->comp[2] = (lev->comp[2] << s) | (lev->comp[3] >> ss);  \
    lev->comp[3] = (lev->comp[3] << s) | (lev->comp[4] >> ss);  \
    lev->comp[4] <<= s;                                         \
    lev->list[4] = (lev->list[4] >> s) | (lev->list[3] << ss);  \
    lev->list[3] = (lev->list[3] >> s) | (lev->list[2] << ss);  \
    lev->list[2] = (lev->list[2] >> s) | (lev->list[1] << ss);  \
    lev->list[1] = (lev->list[1] >> s) | (lev->list[0] << ss);  \
    lev->list[0] >>= s;                                         \
  }

#define COMP_LEFT_LIST_RIGHT_32(lev)              \
  lev->comp[0] = lev->comp[1];                    \
  lev->comp[1] = lev->comp[2];                    \
  lev->comp[2] = lev->comp[3];                    \
  lev->comp[3] = lev->comp[4];                    \
  lev->comp[4] = 0;                               \
  lev->list[4] = lev->list[3];                    \
  lev->list[3] = lev->list[2];                    \
  lev->list[2] = lev->list[1];                    \
  lev->list[1] = lev->list[0];                    \
  lev->list[0] = 0;

#if defined(BITOFLIST_OVER_MEMTABLE)
  #define BITOFLIST(x) ogr_bit_of_LIST[x]
#else
  #define BITOFLIST(x) 0x80000000>>((x-1)&0x1f) /*0x80000000 >> ((x-1) % 32)*/
#endif

#define OPTSTEP9_1
#if defined(OPTSTEP9_1)
#define COPY_LIST_SET_BIT(lev2,lev,bitindex)      \
  {                                               \
    register int d = bitindex;                    \
    lev2->list[0] = lev->list[0];                 \
    lev2->list[1] = lev->list[1];                 \
    lev2->list[2] = lev->list[2];                 \
    lev2->list[3] = lev->list[3];                 \
    lev2->list[4] = lev->list[4];                 \
    if (d <= (32*5))                              \
      lev2->list[(d-1)>>5] |= BITOFLIST( d );     \
  }    
#elif defined(OPTSTEP9_2)  
#define COPY_LIST_SET_BIT(lev2,lev,bitindex)      \
  {                                               \
    register int d = bitindex;                    \
    memcpy( &(lev2->list[0]), &(lev->list[0]), sizeof(lev2->list[0])*5 ); \
    if (d <= (32*5))                              \
      lev2->list[(d-1)>>5] |= BITOFLIST( d );     \
  }    
#else
#define COPY_LIST_SET_BIT(lev2,lev,bitindex)      \
  {                                               \
    register int d = bitindex;                    \
    register int bit = BITOFLIST( d );            \
    if (d <= 32) {                                \
       lev2->list[0] = lev->list[0] | bit;        \
       lev2->list[1] = lev->list[1];              \
       lev2->list[2] = lev->list[2];              \
       lev2->list[3] = lev->list[3];              \
       lev2->list[4] = lev->list[4];              \
    } else if (d <= 64) {                         \
       lev2->list[0] = lev->list[0];              \
       lev2->list[1] = lev->list[1] | bit;        \
       lev2->list[2] = lev->list[2];              \
       lev2->list[3] = lev->list[3];              \
       lev2->list[4] = lev->list[4];              \
    } else if (d <= 96) {                         \
       lev2->list[0] = lev->list[0];              \
       lev2->list[1] = lev->list[1];              \
       lev2->list[2] = lev->list[2] | bit;        \
       lev2->list[3] = lev->list[3];              \
       lev2->list[4] = lev->list[4];              \
    } else if (d <= 128) {                        \
       lev2->list[0] = lev->list[0];              \
       lev2->list[1] = lev->list[1];              \
       lev2->list[2] = lev->list[2];              \
       lev2->list[3] = lev->list[3] | bit;        \
       lev2->list[4] = lev->list[4];              \
    } else if (d <= 160) {                        \
       lev2->list[0] = lev->list[0];              \
       lev2->list[1] = lev->list[1];              \
       lev2->list[2] = lev->list[2];              \
       lev2->list[3] = lev->list[3];              \
       lev2->list[4] = lev->list[4] | bit;        \
    } else {                                      \
       lev2->list[0] = lev->list[0];              \
       lev2->list[1] = lev->list[1];              \
       lev2->list[2] = lev->list[2];              \
       lev2->list[3] = lev->list[3];              \
       lev2->list[4] = lev->list[4];              \
    }                                             \
  }
#endif  

#define COPY_DIST_COMP(lev2,lev)                  \
  lev2->dist[0] = lev->dist[0] | lev2->list[0];   \
  lev2->dist[1] = lev->dist[1] | lev2->list[1];   \
  lev2->dist[2] = lev->dist[2] | lev2->list[2];   \
  lev2->dist[3] = lev->dist[3] | lev2->list[3];   \
  lev2->dist[4] = lev->dist[4] | lev2->list[4];   \
  lev2->comp[0] = lev->comp[0] | lev2->dist[0];   \
  lev2->comp[1] = lev->comp[1] | lev2->dist[1];   \
  lev2->comp[2] = lev->comp[2] | lev2->dist[2];   \
  lev2->comp[3] = lev->comp[3] | lev2->dist[3];   \
  lev2->comp[4] = lev->comp[4] | lev2->dist[4];

#if !defined(HAVE_STATIC_CHOOSEDAT) || defined(CRC_CHOOSEDAT_ANYWAY)
static const unsigned chooseCRC32[24] = {
  0x00000000,   /* 0 */
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,   /* 5 */
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,   /* 10 */
  0x00000000,
  0x01138a7d,
  0x00000000,
  0x00000000,
  0x00000000,   /* 15 */
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,   /* 20 */
  0x00000000,
  0x00000000,
  0x00000000
};
#endif

static int init_load_choose(void)
{
#ifndef HAVE_STATIC_CHOOSEDAT
  #error choose_dat needs to be created/loaded here
#endif  
  if (MAXBITS != ogr_choose_dat[2]) {
    return CORE_E_FORMAT;
  }
#ifndef HAVE_STATIC_CHOOSEDAT
  /* skip over the choose.dat header */
  choosedat = &ogr_choose_dat[3];
#endif  

#if !defined(HAVE_STATIC_CHOOSEDAT) || defined(CRC_CHOOSEDAT_ANYWAY)
  /* CRC32 check */
  {
    int i, j;
    unsigned crc32 = 0xffffffff;
    crc32 = CRC32(crc32, ogr_choose_dat[0]);
    crc32 = CRC32(crc32, ogr_choose_dat[1]);
    crc32 = CRC32(crc32, ogr_choose_dat[2]); /* This varies a lot */
    for (j = 0; j < (1 << MAXBITS); j++) {
      for (i = 0; i < CHOOSEBITS; ++i) crc32 = CRC32(crc32, choose(j, i));
    }
    crc32 = ~crc32;
    if (chooseCRC32[MAXBITS] != crc32) {
      /* printf("Your choose.dat (CRC=%08x) is corrupted! Oh well, continuing anyway.\n", crc32); */
      return CORE_E_FORMAT;
    }
  }

#endif
  return CORE_S_OK;
}

/*-----------------------------------------*/
/*  found_one() - print out golomb rulers  */
/*-----------------------------------------*/

#define HAVE_SMALL_DATA_CACHE
static int found_one(struct State *oState)
{
  /* confirm ruler is golomb */
  {
    register int i, j;
    #ifdef HAVE_SMALL_DATA_CACHE
    char diffs[((1024-64)+7)/8];
    #else
    char diffs[1024];
    #endif
    register int max = oState->max;
    register int maxdepth = oState->maxdepth;
    memset( diffs, 0, sizeof(diffs) );
    /* for (i = 1; i <= oState->max/2; i++) diffs[i] = 0; */
    for (i = 1; i < maxdepth; i++) {
      register int marks_i = oState->marks[i];
      for (j = 0; j < i; j++) {
        register int diff = marks_i - oState->marks[j];
        if (diff+diff <= max) {        /* Principle 1 */
          if (diff <= 64) break;      /* 2 bitmaps always tracked */
	  #ifdef HAVE_SMALL_DATA_CACHE
	  {
	    register int n1, n2;
            diff -= 64;
            n1 = diff>>3; n2 = 1<<(diff&7);
	    if ((diffs[n1] & n2)!=0) return 0;
	    diffs[n1] |= n2;
	  }
	  #else    
          if (diffs[diff]) return 0;
          diffs[diff] = 1;
	  #endif
        }
      }
    }
  }
  return 1;
}

#if defined(FIRSTBLANK_OVER_MEMTABLE) /* 0 <= x < 0xfffffffe */
  #define LOOKUP_FIRSTBLANK(x) ((x < 0xffff0000) ? \
      (ogr_first_blank[x>>16]) : (16 + ogr_first_blank[x - 0xffff0000]))
#elif defined(__PPC__) || defined(ASM_PPC) /* CouNT Leading Zeros Word */
  #if defined(__GNUC__)
    #error "Please check this (define FIRSTBLANK_ASM_TEST to test)"
    static __inline__ int LOOKUP_FIRSTBLANK(register unsigned int i)
    { __asm__ ("not %0,%0;cntlzw %0,%0;addi %0,%0,1" : : "r" (i)); return i; }
  #else /* macos */
    #error "Please check this (define FIRSTBLANK_ASM_TEST to test)"
    #define LOOKUP_FIRSTBLANK(x) (__cntlzw(~((unsigned int)(x)))+1)
  #endif    
#elif defined(ASM_ALPHA) && defined(__GNUC__)
  #error "Please check this (define FIRSTBLANK_ASM_TEST to test)"
  static __inline__ int LOOKUP_FIRSTBLANK(register unsigned int i)
  { i = ~i; __asm__ ("cntlzw %0,%0" : : "r" (i)); return i+1; }
#elif defined(ASM_SPARC) && defined(__GNUC__)    
  #error "Please check this (define FIRSTBLANK_ASM_TEST to test)"
  static __inline__ int LOOKUP_FIRSTBLANK(register unsigned int i)
  { register int count; __asm__ ("scan %1,0,%0" : "r=" ((unsigned int)(~i))
    : "r" ((unsigned int)(count)) );  return count+1; }
#elif defined(ASM_X86) && defined(__GNUC__)
  static __inline__ int LOOKUP_FIRSTBLANK(int t)
  {
    int s; t=~t; 
     __asm__( "movl $33, %%ebx 
              orl  %%ebx, %%ebx
              bsrl %%eax, %%eax
              jz   0f
              subl %%eax, %%ebx
              decl %%ebx
              0:" : "=b"(s) : "a"(t) : "%eax", "%ebx", "cc");
    return s;
  }  
#else
  #error FIRSTBLANK_OVER_MEMTABLE is not defined, and no code to match
#endif
	 
/*
     0- (0x0000+0x8000-1) = 1    0x0000-0x7fff = 1 (0x7fff) 1000 0000 0000 0000
0x8000- (0x8000+0x4000-1) = 2    0x8000-0xBfff = 2 (0x3fff) 1100 0000 0000 0000
0xC000- (0xC000+0x2000-1) = 3    0xC000-0xDfff = 3 (0x1fff) 1110 0000 0000 0000
0xE000- (0xE000+0x1000-1) = 4    0xE000-0xEfff = 4 (0x0fff) 1111 0000 0000 0000
0xF000- (0xF000+0x0800-1) = 5    0xF000-0xF7ff = 5 (0x07ff) 1111 1000 0000 0000
0xF800- (0xF800+0x0400-1) = 6    0xF800-0xFBff = 6 (0x03ff) 1111 1100 0000 0000
*/


static int ogr_init(void)
{
  int r = init_load_choose();
  if (r != CORE_S_OK) {
    return r;
  }

  #if defined(BITOFLIST_OVER_MEMTABLE)
  {
    int n;
    ogr_bit_of_LIST[0] = 0;
    for( n=1; n < 200; n++) {
       ogr_bit_of_LIST[n] = 0x80000000 >> ((n-1) % 32);
    }
  }    
  #endif

  #if defined(FIRSTBLANK_OVER_MEMTABLE) || defined(FIRSTBLANK_ASM_TEST)
  {
    /* first zero bit in 16 bits */
    int i, j, k = 0, m = 0x8000;
    for (i = 1; i <= 16; i++) {
      for (j = k; j < k+m; j++) ogr_first_blank[j] = (char)i;
      k += m;
      m >>= 1;
    }
    ogr_first_blank[0xffff] = 17;     /* just in case we use it */
  }    
  #endif

  #if defined(FIRSTBLANK_ASM_TEST)
  {
    static int done_test = -1;
    if ((++done_test) == 0)
    {
      unsigned int q, err_count = 0;
      printf("begin firstblank test\n"
             "(this may take a looooong time and requires a -KILL to stop)\n");   
      for (q = 0; q <= 0xfffffffe; q++)
      {
        int s1 = ((q < 0xffff0000) ? \
          (ogr_first_blank[q>>16]) : (16 + ogr_first_blank[q - 0xffff0000]));
        int s2 = LOOKUP_FIRSTBLANK(q);
        if (s1 != s2)
        {
          printf("\nfirstblank error %d != %d (q=%u)\n", s1, s2, q);
          err_count++;
        }  
        else if ((q & 0xffff) == 0xffff)      
          printf("\rfirstblank done 0x%08x-0x%08x ", q-0xffff, q);
      }
      printf("\nend firstblank test (%u errors)\n", err_count);    
    }
    done_test = 0;
  }
  #endif

  return CORE_S_OK;
}

#ifdef OGR_DEBUG
static void dump(int depth, struct Level *lev, int limit)
{
  printf("--- depth %d\n", depth);
  printf("list=%08x%08x%08x%08x%08x\n", lev->list[0], lev->list[1], lev->list[2], lev->list[3], lev->list[4]);
  printf("dist=%08x%08x%08x%08x%08x\n", lev->dist[0], lev->dist[1], lev->dist[2], lev->dist[3], lev->dist[4]);
  printf("comp=%08x%08x%08x%08x%08x\n", lev->comp[0], lev->comp[1], lev->comp[2], lev->comp[3], lev->comp[4]);
  printf("cnt1=%d cnt2=%d limit=%d\n", lev->cnt1, lev->cnt2, limit);
  //sleep(1);
}
#endif

static int ogr_create(void *input, int inputlen, void *state, int statelen)
{
  struct State *oState;
  struct WorkStub *workstub = (struct WorkStub *)input;

  if (input == NULL || inputlen != sizeof(struct WorkStub)) {
    return CORE_E_FORMAT;
  }

  if (((unsigned int)statelen) < sizeof(struct State)) {
    return CORE_E_FORMAT;
  }
  oState = (struct State *)state;
  if (oState == NULL) {
    return CORE_E_MEMORY;
  }

  memset(oState, 0, sizeof(struct State));

  oState->maxdepth = workstub->stub.marks;
  oState->maxdepthm1 = oState->maxdepth-1;

  if (((unsigned int)oState->maxdepth) > (sizeof(OGR)/sizeof(OGR[0]))) {
    return CORE_E_FORMAT;
  }

  oState->max = OGR[oState->maxdepth-1];

  /* Note, marks are labled 0, 1...  so mark @ depth=1 is 2nd mark */
  oState->half_depth2 = oState->half_depth = ((oState->maxdepth+1) >> 1) - 1;
  if (!(oState->maxdepth % 2)) oState->half_depth2++;  /* if even, use 2 marks */

  /* Simulate GVANT's "KTEST=1" */
  oState->half_depth--;
  oState->half_depth2++;
  /*------------------
  Since:  half_depth2 = half_depth+2 (or 3 if maxdepth even) ...
  We get: half_length2 >= half_length + 3 (or 6 if maxdepth even)
  But:    half_length2 + half_length <= max-1    (our midpoint reduction)
  So:     half_length + 3 (6 if maxdepth even) + half_length <= max-1
  ------------------*/
                               oState->half_length = (oState->max-4) >> 1;
  if ( !(oState->maxdepth%2) ) oState->half_length = (oState->max-7) >> 1;

  oState->depth = 1;

  {
    int i, n;
    struct Level *lev, *lev2;

    n = workstub->worklength;
    if (n < workstub->stub.length) {
      n = workstub->stub.length;
    }
    if (n > STUB_MAX) {
      return CORE_E_FORMAT;
    }
    lev = &oState->Levels[1];
    for (i = 0; i < n; i++) {
      int limit;
      if (oState->depth <= oState->half_depth2) {
        if (oState->depth <= oState->half_depth) {
          limit = oState->max - OGR[oState->maxdepthm1 - oState->depth];
          limit = limit < oState->half_length ? limit : oState->half_length;
        } else {
          limit = oState->max - choose(lev->dist[0] >> ttmMAXBITS, oState->maxdepthm1 - oState->depth);
          limit = limit < oState->max - oState->marks[oState->half_depth]-1 ? limit : oState->max - oState->marks[oState->half_depth]-1;
        }
      } else {
        limit = oState->max - choose(lev->dist[0] >> ttmMAXBITS, oState->maxdepthm1 - oState->depth);
      }
      lev->limit = limit;
      register int s = workstub->stub.diffs[i];
      //dump(oState->depth, lev, 0);
      oState->marks[i+1] = oState->marks[i] + s;
      lev->cnt2 += s;
      register int t = s;
      while (t >= 32) {
        COMP_LEFT_LIST_RIGHT_32(lev);
        t -= 32;
      }
      if (t > 0) {
        COMP_LEFT_LIST_RIGHT(lev, t);
      }
      lev2 = lev + 1;
      COPY_LIST_SET_BIT(lev2, lev, s);
      COPY_DIST_COMP(lev2, lev);
      lev2->cnt1 = lev->cnt2;
      lev2->cnt2 = lev->cnt2;
      lev++;
      oState->depth++;
    }
    oState->depth--; // externally visible depth is one less than internal
  }

  oState->startdepth = workstub->stub.length;

/*
  printf("sizeof      = %d\n", sizeof(struct State));
  printf("max         = %d\n", oState->max);
  printf("maxdepth    = %d\n", oState->maxdepth);
  printf("maxdepthm1  = %d\n", oState->maxdepthm1);
  printf("half_length = %d\n", oState->half_length);
  printf("half_depth  = %d\n", oState->half_depth);
  printf("half_depth2 = %d\n", oState->half_depth2);
  {
    int i;
    printf("marks       = ");
    for (i = 1; i < oState->depth; i++) {
      printf("%d ", oState->marks[i]-oState->marks[i-1]);
    }
    printf("\n");
  }
*/

  return CORE_S_OK;
}

#ifdef OGR_DEBUG
static void dump_ruler(struct State *oState, int depth)
{
  int i;
  printf("max %d ruler ", oState->max);
  for (i = 1; i < depth; i++) {
    printf("%d ", oState->marks[i] - oState->marks[i-1]);
  }
  printf("\n");
}
#endif


static int ogr_cycle(void *state, int *pnodes)
{
  struct State *oState = (struct State *)state;
  int depth = oState->depth+1;      /* the depth of recursion */
  struct Level *lev = &oState->Levels[depth];
  struct Level *lev2;
  int nodes = 0;
  int nodeslimit = *pnodes;
  int retval = CORE_S_CONTINUE;
  int limit;
  int s;
  U comp0;

#ifdef OGR_DEBUG
  oState->LOGGING = 1;
#endif
  for (;;) {

    oState->marks[depth-1] = lev->cnt2;
#ifdef OGR_DEBUG
    if (oState->LOGGING) dump_ruler(oState, depth);
#endif
    if (depth <= oState->half_depth2) {
      if (depth <= oState->half_depth) {
        //dump_ruler(oState, depth);
        if (nodes >= nodeslimit) {
          break;
        }
        limit = oState->max - OGR[oState->maxdepthm1 - depth];
        limit = limit < oState->half_length ? limit : oState->half_length;
      } else {
        limit = oState->max - choose(lev->dist[0] >> ttmMAXBITS, oState->maxdepthm1 - depth);
        limit = limit < oState->max - oState->marks[oState->half_depth]-1 ? limit : oState->max - oState->marks[oState->half_depth]-1;
      }
    } else {
      limit = oState->max - choose(lev->dist[0] >> ttmMAXBITS, oState->maxdepthm1 - depth);
    }

#ifdef OGR_DEBUG
    if (oState->LOGGING) dump(depth, lev, limit);
#endif

    nodes++;

    /* Find the next available mark location for this level */
stay:
    comp0 = lev->comp[0];
#ifdef OGR_DEBUG
    if (oState->LOGGING) printf("comp0=%08x\n", comp0);
#endif

    if (comp0 < 0xfffffffe) {
      #ifndef FIRSTBLANK_OVER_MEMTABLE /* 0 <= x < 0xfffffffe */
      s = LOOKUP_FIRSTBLANK( comp0 );
      #else
      if (comp0 < 0xffff0000) 
        s = ogr_first_blank[comp0 >> 16];
      else {    
        /* s = 16 + ogr_first_blank[comp0 & 0x0000ffff]; slow code */
        s = 16 + ogr_first_blank[comp0 - 0xffff0000];
      }        
      #endif
#ifdef OGR_DEBUG
  if (oState->LOGGING) printf("depth=%d s=%d len=%d limit=%d\n", depth, s+(lev->cnt2-lev->cnt1), lev->cnt2+s, limit);
#endif
      if ((lev->cnt2 += s) > limit) goto up; /* no spaces left */
      COMP_LEFT_LIST_RIGHT(lev, s);
    } else {
      /* s>32 */
      if ((lev->cnt2 += 32) > limit) goto up; /* no spaces left */
      COMP_LEFT_LIST_RIGHT_32(lev);
      if (comp0 == 0xffffffff) goto stay;
    }


    /* New ruler? */
    if (depth == oState->maxdepthm1) {
      oState->marks[oState->maxdepthm1] = lev->cnt2;       /* not placed yet into list arrays! */
      if (found_one(oState)) {
        retval = CORE_S_SUCCESS;
        break;
      }
      goto stay;
    }

    /* Go Deeper */
    lev2 = lev + 1;
    COPY_LIST_SET_BIT(lev2, lev, lev->cnt2-lev->cnt1);
    COPY_DIST_COMP(lev2, lev);
    lev2->cnt1 = lev->cnt2;
    lev2->cnt2 = lev->cnt2;
    lev->limit = limit;
    lev++;
    depth++;
    continue;

up:
    lev--;
    depth--;
    if (depth <= oState->startdepth) {
      retval = CORE_S_OK;
      break;
    }
    limit = lev->limit;

    goto stay; /* repeat this level till done */
  }

  oState->Nodes += nodes;
  oState->depth = depth-1;

  *pnodes = nodes;

  return retval;
}

static int ogr_getresult(void *state, void *result, int resultlen)
{
  struct State *oState = (struct State *)state;
  struct WorkStub *workstub = (struct WorkStub *)result;
  int i;

  if (resultlen != sizeof(struct WorkStub)) {
    return CORE_E_FORMAT;
  }
  workstub->stub.marks = (u16)oState->maxdepth;
  workstub->stub.length = (u16)oState->startdepth;
  for (i = 0; i < STUB_MAX; i++) {
    workstub->stub.diffs[i] = (u16)(oState->marks[i+1] - oState->marks[i]);
  }
  workstub->worklength = oState->depth;
  if (workstub->worklength > STUB_MAX) {
    workstub->worklength = STUB_MAX;
  }
  return CORE_S_OK;
}

static int ogr_destroy(void *state)
{
  state = state;
  return CORE_S_OK;
}

#if 0
static int ogr_count(void *state)
{
  return sizeof(struct State);
}

static int ogr_save(void *state, void *buffer, int buflen)
{
  if (buflen < sizeof(struct State)) {
    return CORE_E_MEMORY;
  }
  memcpy(buffer, state, sizeof(struct State));
  return CORE_S_OK;
}

static int ogr_load(void *buffer, int buflen, void **state)
{
  if (buflen < sizeof(struct State)) {
    return CORE_E_FORMAT;
  }
  *state = malloc(sizeof(struct State));
  if (*state == NULL) {
    return CORE_E_MEMORY;
  }
  memcpy(*state, buffer, sizeof(struct State));
  return CORE_S_OK;
}
#endif

static int ogr_cleanup(void)
{
  return CORE_S_OK;
}

CoreDispatchTable * OGR_GET_DISPATCH_TABLE_FXN (void)
{
  static CoreDispatchTable dispatch_table;
  dispatch_table.init      = ogr_init;
  dispatch_table.create    = ogr_create;
  dispatch_table.cycle     = ogr_cycle;
  dispatch_table.getresult = ogr_getresult;
  dispatch_table.destroy   = ogr_destroy;
#if 0
  dispatch_table.count     = ogr_count;
  dispatch_table.save      = ogr_save;
  dispatch_table.load      = ogr_load;
#endif
  dispatch_table.cleanup   = ogr_cleanup;
  return &dispatch_table;
}

