/* 
 * Copyright distributed.net 1997-2003 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
*/
const char *ogrng_cell_spe_wrapper_cpp(void) {
return "@(#)$Id: ogrng-cell-spe-wrapper.c,v 1.2 2008/06/29 14:20:28 stream Exp $"; }

#include <spu_intrinsics.h>
#include "ccoreio.h"
#include "cputypes.h"
#include <spu_mfcio.h>

#define __CNTLZ__(n)  spu_extract(spu_cntlz(spu_promote(n, 0)), 0)

#include "ogrng-cell.h"
#include "ansi/first_blank.h"

#ifndef CORE_NAME
#error CORE_NAME not defined
#endif

#define SPE_CORE_FUNCTION(name) SPE_CORE_FUNCTION2(name)
#define SPE_CORE_FUNCTION2(name) ogrng_cycle_ ## name ## _spe_core

#ifdef __cplusplus
extern "C"
#endif
// s32 CDECL SPE_CORE_FUNCTION(CORE_NAME) ( struct State*, int*, const unsigned char* );

// #define FULL_CYCLE

CellOGRCoreArgs myCellOGRCoreArgs __attribute__((aligned (128)));

int ogr_cycle_256(struct OgrState *oState, int *pnodes, /* const u16* */ u32 upchoose);
#ifdef FULL_CYCLE
int ogr_cycle_entry(struct OgrState *oState, int *pnodes, u32 upchoose);
#endif

#define DMA_ID  31

int main(unsigned long long speid, addr64 argp, addr64 envp)
{
  // Check size of structures, these offsets must match assembly
  STATIC_ASSERT(sizeof(struct OgrLevel) == 7*16);
  STATIC_ASSERT(sizeof(struct OgrState) == 2*16 + 7*16*29);
  STATIC_ASSERT(sizeof(CellOGRCoreArgs) == 2*16 + 7*16*29 + 16);
  STATIC_ASSERT(offsetof(CellOGRCoreArgs, state.Levels) == 32);
  STATIC_ASSERT(sizeof(u16) == 2); /* DMA fetches of pchoose */
  
  (void) speid; (void) envp;

  // One DMA used in program
  mfc_write_tag_mask(1<<DMA_ID);

  // Fetch arguments from main memory
  mfc_get(&myCellOGRCoreArgs, argp.a32[1], sizeof(CellOGRCoreArgs), DMA_ID, 0, 0);
  mfc_read_tag_status_all();

  // Prepare arguments to be passed to the core
  struct OgrState* state = &myCellOGRCoreArgs.state;
  int* pnodes   = &myCellOGRCoreArgs.pnodes;
  u32  upchoose = myCellOGRCoreArgs.upchoose;

  // Call the core
//  s32 retval = SPE_CORE_FUNCTION(CORE_NAME) (state, pnodes, ogr_choose_dat);
  s32 retval;
#ifdef FULL_CYCLE
  retval = ogr_cycle_entry(state, pnodes, upchoose);
#else
  myCellOGRCoreArgs.ret_depth = ogr_cycle_256(state, pnodes, upchoose);
  retval = 0;
#endif

  // Update changes in main memory
  mfc_put(&myCellOGRCoreArgs, argp.a32[1], sizeof(CellOGRCoreArgs), DMA_ID, 0, 0);
  mfc_read_tag_status_all();

  return retval; /* no status codes in ogr-ng, core info returned in ret_depth */
}

typedef vector unsigned char v8_t;
typedef vector unsigned int v32_t;

#define vec_splat_u32(_a)   spu_splats((unsigned int)(_a))
#define vec_andc            spu_andc
#define vec_or              spu_or

/*
** Define the local variables used for the top recursion state
*/
#define SETUP_TOP_STATE(lev)                           \
  SCALAR comp0, dist0;                                 \
  v32_t compV0, compV1;                                \
  v32_t listV0, listV1;                                \
  v32_t distV0, distV1;                                \
  v32_t V_ZERO = vec_splat_u32(0);                     \
  v32_t V_ONES = vec_splat_u32(~0);                    \
  v32_t newbit = V_ZERO;                               \
  listV0 = lev->list[0].v;                             \
  listV1 = lev->list[1].v;                             \
  distV0 = lev->dist[0].v;                             \
  distV1 = lev->dist[1].v;                             \
  compV0 = lev->comp[0].v;                             \
  compV1 = lev->comp[1].v;                             \
  dist0 = spu_extract(distV0, 0);                      \
  comp0 = spu_extract(compV0, 0);                      \
  if (depth < oState->maxdepthm1) {                    \
    newbit = vec_splat_u32(1);                         \
  }

/*
** Rotate/shift full vector by specific number of bits
*/
#define full_rl(v, n)    spu_rlqw(spu_rlqwbytebc(v, n), n)
#define full_sl(v, n)    spu_slqw(spu_slqwbytebc(v, n), n)
#define full_sr(v, n)    spu_rlmaskqw(spu_rlmaskqwbytebc(v, 7-n), 0-n)

#define COMP_LEFT_LIST_RIGHT(lev, s)                   \
{                                                      \
  v32_t selMask;                                       \
  int   inv_s;                                         \
  selMask = full_sr(V_ONES, s);                        \
  compV0  = spu_sel(compV1, compV0, selMask);          \
  compV0  = full_rl(compV0, s);                        \
  compV1  = full_sl(compV1, s);                        \
  comp0   = spu_extract(compV0, 0);                    \
  selMask = full_sl(V_ONES, s);                        \
  listV1  = spu_sel(listV0, listV1, selMask);          \
  inv_s   = 128 - s;                                   \
  listV1  = full_rl(listV1, inv_s);                    \
  listV0  = spu_sel(newbit, listV0, selMask);          \
  listV0  = full_rl(listV0, inv_s);                    \
  newbit  = V_ZERO;                                    \
}
  
#define COMP_LEFT_LIST_RIGHT_WORD(lev)  COMP_LEFT_LIST_RIGHT(lev, 32)


/*
** Set the current mark and push a level to start a new mark
*/
#define PUSH_LEVEL_UPDATE_STATE(lev)    \
  lev->list[0].v = listV0;              \
  distV0 = vec_or(distV0, listV0);      \
  lev->comp[0].v = compV0;              \
  compV0 = vec_or(compV0, distV0);      \
  lev->list[1].v = listV1;              \
  distV1 = vec_or(distV1, listV1);      \
  lev->comp[1].v = compV1;              \
  compV1 = vec_or(compV1, distV1);      \
  dist0  = spu_extract(distV0, 0);      \
  comp0  = spu_extract(compV0, 0);      \
  newbit = vec_splat_u32(1);


/*
** Pop a level to continue work on previous mark
*/
#define POP_LEVEL(lev)                  \
  listV0 = lev->list[0].v;              \
  listV1 = lev->list[1].v;              \
  distV0 = vec_andc(distV0, listV0);    \
  distV1 = vec_andc(distV1, listV1);    \
  compV0 = lev->comp[0].v;              \
  compV1 = lev->comp[1].v;              \
  comp0  = spu_extract(compV0, 0);      \
  newbit = V_ZERO;


/*
** Save the local state variables
*/
#define SAVE_FINAL_STATE(lev)           \
  lev->list[0].v = listV0;              \
  lev->dist[0].v = distV0;              \
  lev->comp[0].v = compV0;              \
  lev->list[1].v = listV1;              \
  lev->dist[1].v = distV1;              \
  lev->comp[1].v = compV1;


static inline unsigned direct_dma_fetch(u32 upchoose, u32 index)
{
  u16 tempdma[8]; /* use array to fetch 16 bytes (transfer must be aligned) */
  STATIC_ASSERT(sizeof(tempdma) == 16);

  upchoose += index * 2;        /* get true full address in u16 *pchoose */
  index = (upchoose & 15) / 2;  /* index of u16 element within 16 bytes  */
  upchoose &= ~15;              /* align transfer to 16 bytes            */
  mfc_get(tempdma, upchoose, sizeof(tempdma), DMA_ID, 0, 0);
  mfc_read_tag_status_all();
  return tempdma[index];
}

#define getchoose(index)  direct_dma_fetch(upchoose, (index))

#define choose(dist,seg) getchoose( (dist >> (SCALAR_BITS-CHOOSE_DIST_BITS)) * 32 + (seg) )

#if 1

int ogr_cycle_256(struct OgrState * oState, int * pnodes, /* const u16* */ u32 upchoose)
{
  struct OgrLevel *lev = &oState->Levels[oState->depth];
  int depth       = oState->depth;
  int maxlen_m1   = oState->max - 1;
  int nodes       = *pnodes;

  SETUP_TOP_STATE(lev);

  do {
    int limit = lev->limit;
    int mark  = lev->mark;

    for (;;) {
      if (comp0 < (SCALAR)~1) {
        int s = LOOKUP_FIRSTBLANK(comp0);

        if ((mark += s) > limit) {
          break;
        }
        COMP_LEFT_LIST_RIGHT(lev, s);
      }
      else {         /* s >= SCALAR_BITS */
        if ((mark += SCALAR_BITS) > limit) {
          break;
        }
        if (comp0 == (SCALAR)~0) {
          COMP_LEFT_LIST_RIGHT_WORD(lev);
          continue;
        }
        COMP_LEFT_LIST_RIGHT_WORD(lev);
      }

      lev->mark = mark;
      if (depth == oState->maxdepthm1) {
        goto exit;         /* Ruler found */
      }

      /* Update the bitmaps for the next level */
      PUSH_LEVEL_UPDATE_STATE(lev);
      ++lev;
      ++depth;
      
      /* Compute the maximum position for the next level */
      limit = choose(dist0, depth);

      if (depth > oState->half_depth && depth <= oState->half_depth2) {
        int temp = maxlen_m1 - oState->Levels[oState->half_depth].mark;

        if (limit > temp) {
          limit = temp;
        }

        /* The following part is only relevant for rulers with an odd number of
        ** marks. If the number of marks is even (as for OGR-26), then the
        ** condition is always false.
        ** LOOKUP_FIRSTBLANK(0xFF..FF) shall return the total number of bits
        ** set plus one. If not, selftest #32 will fail.
        */
        if (depth < oState->half_depth2) {
          #if (SCALAR_BITS <= 32)
          limit -= LOOKUP_FIRSTBLANK(dist0);
          #else
          // Reduce the resolution for larger datatypes, otherwise the final
          // node count may not match that of 32-bit cores.
          limit -= LOOKUP_FIRSTBLANK(dist0 & -((SCALAR)1 << 32));
          #endif
        }
      }
      lev->limit = limit;

      if (--nodes <= 0) {
        lev->mark = mark;
        goto exit;
      }
    } /* for (;;) */
    --lev;
    --depth;
    POP_LEVEL(lev);
  } while (depth > oState->stopdepth);

exit:
  SAVE_FINAL_STATE(lev);
  *pnodes -= nodes;
  return depth;
}

#ifdef FULL_CYCLE
static int found_one(const struct OgrState *oState)
{
  register int i, j;
  u32 diffs[((1024 - OGRNG_BITMAPS_LENGTH) + 31) / 32];
  register int max = oState->max;
  register int maxdepth = oState->maxdepth;
  const struct OgrLevel *levels = &oState->Levels[0];

  for (i = max >> (5+1); i >= 0; --i)
    diffs[i] = 0;

  for (i = 1; i < maxdepth; i++) {
    register int marks_i = levels[i].mark;

    for (j = 0; j < i; j++) {
      register int diff = marks_i - levels[j].mark;
      if (diff <= OGRNG_BITMAPS_LENGTH)
        break;

      if (diff+diff <= max) {       /* Principle #1 */
        register u32 mask = 1 << (diff & 31);
        diff = (diff >> 5) - (OGRNG_BITMAPS_LENGTH / 32);
        if ((diffs[diff] & mask) != 0)
          return 0;                 /* Distance already taken = not Golomb */

        diffs[diff] |= mask;
      }
    }
  }
  return -1;      /* Is golomb */
}

int ogr_cycle_entry(struct OgrState *oState, int *pnodes, u32 upchoose)
{
  int retval = CORE_S_CONTINUE;
//  struct OgrState *oState = (struct OgrState *)state;
//  u16* pchoose = precomp_limits[oState->maxdepth - OGR_NG_MIN].choose_array;
  int safesize = OGRNG_BITMAPS_LENGTH * 2;
  int depth;

  /* Now that the core always exits when the specified number of nodes has
  ** been processed, the "with_time_constraints" setting is obsolete.
  */
//  with_time_constraints = with_time_constraints;
  ++oState->depth;

  /* Invoke the main core.
  ** Except for OGR-26 (and shorter rulers), the core may still find rulers
  ** that are not Golomb. This loop asserts the Golombness of any ruler found
  ** when that makes sense.
  */
  do {
    depth = ogr_cycle_256(oState, pnodes, upchoose);
  } while (depth == oState->maxdepthm1 && oState->Levels[depth].mark > safesize && found_one(oState) == 0);

  if (depth == oState->maxdepthm1) {
    retval = CORE_S_SUCCESS;
  }
  else if (depth <= oState->stopdepth) {
    retval = CORE_S_OK;
  }

  oState->depth = depth - 1;
  return retval;
}
#endif

#else

int ogr_cycle_256(struct OgrState *oState, int *pnodes, /* const u16* */ u32 upchoose)
{
#if 0
  return direct_dma_fetch(upchoose, 0);
#endif

#if 0
  int count;
  v32_t V_ONES = vec_splat_u32(~0);

  count  = *pnodes;
  V_ONES = full_sr(V_ONES, count);
  return spu_extract(V_ONES, 0);
#endif

#if 0
  return LOOKUP_FIRSTBLANK(*pnodes);
#endif
}

#endif