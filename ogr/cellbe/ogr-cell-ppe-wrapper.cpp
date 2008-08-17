/* 
 * Copyright distributed.net 1997-2003 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
*/
const char *ogr_cell_ppe_wrapper_cpp(void) {
return "@(#)$Id: ogr-cell-ppe-wrapper.cpp,v 1.1.2.5 2008/08/17 05:08:50 stream Exp $"; }

#ifndef CORE_NAME
#error CORE_NAME undefined.
#endif

#define OGROPT_HAVE_OGR_CYCLE_ASM 2
#define OGROPT_ALTERNATE_CYCLE 2
#define OGR_GET_DISPATCH_TABLE_FXN    spe_ogr_get_dispatch_table
#define OGROPT_OGR_CYCLE_ALTIVEC 1

    /* define the local variables used for the top recursion state */
  #if (CLIENT_OS == OS_LINUX)
    #define SETUP_TOP_STATE(lev)  \
      U comp0 = lev->comp0,       \
        comp1 = lev->compV.u[0],  \
        comp2 = lev->compV.u[1],  \
        comp3 = lev->compV.u[2],  \
        comp4 = lev->compV.u[3];  \
      U list0 = lev->list0,       \
        list1 = lev->listV.u[0],  \
        list2 = lev->listV.u[1],  \
        list3 = lev->listV.u[2],  \
        list4 = lev->listV.u[3];  \
      U dist0 = lev->dist0,       \
        dist1 = lev->distV.u[0],  \
        dist2 = lev->distV.u[1],  \
        dist3 = lev->distV.u[2],  \
        dist4 = lev->distV.u[3];  \
      int newbit = 1;
  #else
    #define SETUP_TOP_STATE(lev)                            \
      v8_t Varray[32];                                      \
      U comp0, dist0, list0;                                \
      vector unsigned int compV, listV, distV;              \
      vector unsigned int zeroes = vec_splat_u32(0);        \
      vector unsigned int ones = ONES_DECL;                 \
      listV = lev->listV.v;                                 \
      distV = lev->distV.v;                                 \
      compV = lev->compV.v;                                 \
      list0 = lev->list0;                                   \
      dist0 = lev->dist0;                                   \
      comp0 = lev->comp0;                                   \
      size_t listOff = offsetof(struct Level, list0);       \
      if ((listOff&15) != 12) return CORE_E_INTERNAL;       \
      int newbit = 1;                                       \
      { /* Initialize Varray[] */                           \
        vector unsigned char val = vec_splat_u8(0);         \
        vector unsigned char one = vec_splat_u8(1);         \
        int i;                                              \
        for (i = 0; i < 32; i++) {                          \
          Varray[i] = val;                                  \
          val = vec_add(val, one);                          \
        }                                                   \
      }
  #endif
          
    /* shift the list to add or extend the first mark */
  #if (CLIENT_OS == OS_LINUX)
    #define COMP_LEFT_LIST_RIGHT(lev, s) {  \
      U temp1, temp2;                       \
      int ss = 32 - (s);                    \
      comp0 <<= s;                          \
      temp1 = newbit << ss;                 \
      temp2 = list0 << ss;                  \
      list0 = (list0 >> (s)) | temp1;       \
      temp1 = list1 << ss;                  \
      list1 = (list1 >> (s)) | temp2;       \
      temp2 = list2 << ss;                  \
      list2 = (list2 >> (s)) | temp1;       \
      temp1 = list3 << ss;                  \
      list3 = (list3 >> (s)) | temp2;       \
      temp2 = comp1 >> ss;                  \
      list4 = (list4 >> (s)) | temp1;       \
      temp1 = comp2 >> ss;                  \
      comp0 |= temp2;                       \
      temp2 = comp3 >> ss;                  \
      comp1 = (comp1 << (s)) | temp1;       \
      temp1 = comp4 >> ss;                  \
      comp2 = (comp2 << (s)) | temp2;       \
      comp4 = comp4 << (s);                 \
      comp3 = (comp3 << (s)) | temp1;       \
      newbit = 0;                           \
    }
  #else
    #define COMP_LEFT_LIST_RIGHT(lev, s)                    \
    {                                                       \
      U comp1;                                              \
      v32_t Vs, Vss, listV1, bmV;                           \
      int ss = 32 - (s);                                    \
      Vs = (v32_t) Varray[s];                               \
      list0 >>= s;                                          \
      newbit <<= ss;                                        \
      listV1 = vec_lde(listOff, (U *)lev);                  \
      list0 |= newbit;                                      \
      comp1 = lev->compV.u[0];                              \
      comp0 <<= s;                                          \
      lev->list0 = list0;                                   \
      compV = vec_slo(compV, (v8_t)Vs);                     \
      Vss = vec_sub(zeroes, Vs);                            \
      comp1 >>= ss;                                         \
      bmV = vec_sl(ones, Vs);                               \
      listV1 = vec_sld(listV1, listV, 12);                  \
      comp0 |= comp1;                                       \
      compV = vec_sll(compV, (v8_t)Vs);                     \
      listV = vec_sel(listV1, listV, bmV);                  \
      listV = vec_rl(listV, Vss);                           \
      lev->compV.v = compV;                                 \
      newbit = 0;                                           \
    }
  #endif

    /*
    ** shift by word size
    */
  #if (CLIENT_OS == OS_LINUX)
    #define COMP_LEFT_LIST_RIGHT_32(lev)  \
      list4 = list3;                      \
      list3 = list2;                      \
      list2 = list1;                      \
      list1 = list0;                      \
      list0 = newbit;                     \
      comp0 = comp1;                      \
      comp1 = comp2;                      \
      comp2 = comp3;                      \
      comp3 = comp4;                      \
      comp4 = 0;                          \
      newbit = 0;
  #else
    #define COMP_LEFT_LIST_RIGHT_32(lev)                    \
      list0 = newbit;                                       \
      v32_t listV1 = vec_lde(listOff, (U *)lev);            \
      lev->list0 = newbit;                                  \
      compV = vec_sld(compV, zeroes, 4);                    \
      comp0 = lev->compV.u[0];                              \
      listV = vec_sld(listV1, listV, 12);                   \
      lev->compV.v = compV;                                 \
      newbit = 0;
  #endif

    /* set the current mark and push a level to start a new mark */
  #if (CLIENT_OS == OS_LINUX)
    #define PUSH_LEVEL_UPDATE_STATE(lev)        \
      lev->list0 = list0; dist0 |= list0;       \
      lev->listV.u[0] = list1; dist1 |= list1;  \
      lev->listV.u[1] = list2; dist2 |= list2;  \
      lev->listV.u[2] = list3; dist3 |= list3;  \
      lev->listV.u[3] = list4; dist4 |= list4;  \
      lev->comp0 = comp0; comp0 |= dist0;       \
      lev->compV.u[0] = comp1; comp1 |= dist1;  \
      lev->compV.u[1] = comp2; comp2 |= dist2;  \
      lev->compV.u[2] = comp3; comp3 |= dist3;  \
      lev->compV.u[3] = comp4; comp4 |= dist4;  \
      newbit = 1;
  #else
    #define PUSH_LEVEL_UPDATE_STATE(lev)                    \
      (lev+1)->list0 = list0;                               \
      distV = vec_or(distV, listV);                         \
      lev->comp0 = comp0;                                   \
      dist0 |= list0;                                       \
      compV = vec_or(compV, distV);                         \
      lev->listV.v = listV;                                 \
      newbit = 1;                                           \
      (lev+1)->compV.v = compV;                             \
      comp0 |= dist0;
  #endif

    /* pop a level to continue work on previous mark */
  #if (CLIENT_OS == OS_LINUX)
    #define POP_LEVEL(lev)      \
      list0 = lev->list0;       \
      list1 = lev->listV.u[0];  \
      list2 = lev->listV.u[1];  \
      list3 = lev->listV.u[2];  \
      list4 = lev->listV.u[3];  \
      dist0 &= ~list0;          \
      comp0 = lev->comp0;       \
      dist1 &= ~list1;          \
      comp1 = lev->compV.u[0];  \
      dist2 &= ~list2;          \
      comp2 = lev->compV.u[1];  \
      dist3 &= ~list3;          \
      comp3 = lev->compV.u[2];  \
      dist4 &= ~list4;          \
      comp4 = lev->compV.u[3];  \
      newbit = 0;
  #else
    #define POP_LEVEL(lev)                                  \
      listV = lev->listV.v;                                 \
      list0 = lev->list0;                                   \
      comp0 = lev->comp0;                                   \
      distV = vec_andc(distV, listV);                       \
      dist0 &= ~list0;                                      \
      compV = lev->compV.v;                                 \
      newbit = 0;
  #endif

    /* save the local state variables */
  #if (CLIENT_OS == OS_LINUX)
    #define SAVE_FINAL_STATE(lev)   \
      lev->list0 = list0;           \
      lev->listV.u[0] = list1;      \
      lev->listV.u[1] = list2;      \
      lev->listV.u[2] = list3;      \
      lev->listV.u[3] = list4;      \
      lev->dist0 = dist0;           \
      lev->distV.u[0] = dist1;      \
      lev->distV.u[1] = dist2;      \
      lev->distV.u[2] = dist3;      \
      lev->distV.u[3] = dist4;      \
      lev->comp0 = comp0;           \
      lev->compV.u[0] = comp1;      \
      lev->compV.u[1] = comp2;      \
      lev->compV.u[2] = comp3;      \
      lev->compV.u[3] = comp4;
  #else
    #define SAVE_FINAL_STATE(lev)                           \
      lev->listV.v = listV;                                 \
      lev->distV.v = distV;                                 \
      lev->compV.v = compV;                                 \
      lev->list0 = list0;                                   \
      lev->dist0 = dist0;                                   \
      lev->comp0 = comp0;
  #endif


#include "ogr-cell.h"
#include <libspe2.h>
#include <cstdlib>
#include <cstring>
#include "unused.h"
#include "logstuff.h"

#include "ansi/ogr.cpp"

#define SPE_WRAPPER_FUNCTION(name) SPE_WRAPPER_FUNCTION2(name)
#define SPE_WRAPPER_FUNCTION2(name) ogr_cycle_ ## name ## _spe_wrapper

extern spe_program_handle_t SPE_WRAPPER_FUNCTION(CORE_NAME);

#ifndef HAVE_MULTICRUNCH_VIA_FORK
  #error Code for fork'ed crunchers only - see static Args buffer below
#endif

spe_context_ptr_t ps3_assign_context_to_program(spe_program_handle_t *program);

static int ogr_cycle(void *state, int *pnodes, int with_time_constraints)
{
  // Check size of structures, these offsets must match assembly
  STATIC_ASSERT(sizeof(struct Level) == 80);
  STATIC_ASSERT(sizeof(struct State) == 2448);
  STATIC_ASSERT(sizeof(CellOGRCoreArgs) == 2464);
  STATIC_ASSERT(sizeof(struct State) <= OGR_PROBLEM_SIZE);
  DNETC_UNUSED_PARAM(with_time_constraints);

  static void* myCellOGRCoreArgs_void; // Dummy variable to avoid compiler warnings

  spe_context_ptr_t context;
  unsigned int      entry = SPE_DEFAULT_ENTRY;
  spe_stop_info_t   stop_info;
  int               retval;
  unsigned          thread_index = 99; // todo. enough hacks.

  if (myCellOGRCoreArgs_void == NULL)
  {
    if (posix_memalign(&myCellOGRCoreArgs_void, 128, sizeof(CellOGRCoreArgs)))
    {
      Log("Alert SPE#%d! posix_memalign() failed\n", thread_index);
      abort();
    }
  }

  CellOGRCoreArgs* myCellOGRCoreArgs = (CellOGRCoreArgs*)myCellOGRCoreArgs_void;

  // Copy function arguments to CellOGRCoreArgs struct
  memcpy(&myCellOGRCoreArgs->state, state, sizeof(struct State));
          myCellOGRCoreArgs->pnodes = *pnodes;
	  myCellOGRCoreArgs->signature = CELL_OGR_SIGNATURE;

  context = ps3_assign_context_to_program(&SPE_WRAPPER_FUNCTION(CORE_NAME));
  retval  = spe_context_run(context, &entry, 0, (void*)myCellOGRCoreArgs, NULL, &stop_info);
  if (retval != 0)
  {
    Log("Alert SPE#%d: spe_context_run() returned %d\n", thread_index, retval);
    abort();
  }

  __asm__ __volatile__ ("sync" : : : "memory");

  // Fetch return value of the SPE core
  if (stop_info.stop_reason == SPE_EXIT)
  {
    retval = stop_info.result.spe_exit_code;
    /*
    if (retval != CORE_S_OK && retval != CORE_S_CONTINUE)
      Log("Alert: SPE%d core returned %d\n", thread_index, retval);
    */
  }
  else
  {
    Log("Alert: SPE#%d exit status is %d\n", thread_index, stop_info.stop_reason);
    retval = -1;
  }

  // Copy data from CellCoreArgs struct back to the function arguments
  memcpy(state, &myCellOGRCoreArgs->state, sizeof(struct State));
         *pnodes = myCellOGRCoreArgs->pnodes;

  return retval;
}
