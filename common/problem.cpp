/*
 * Copyright distributed.net 1997-2000 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * -------------------------------------------------------------------
 * Eagleson's Law:
 *    Any code of your own that you haven't looked at for six or more
 *    months, might as well have been written by someone else.  (Eagleson
 *    is an optimist, the real number is more like three weeks.)
 * -------------------------------------------------------------------
*/
const char *problem_cpp(void) {
return "@(#)$Id: problem.cpp,v 1.108.2.51 2000/02/15 03:41:19 sampo Exp $"; }

/* ------------------------------------------------------------- */

#include <assert.h>   //assert() macro

#include "cputypes.h"
#include "baseincs.h"
#include "version.h"  //CLIENT_BUILD_FRAC
#include "client.h"   //CONTEST_COUNT
#include "clitime.h"  //CliClock()
#include "logstuff.h" //LogScreen()
#include "probman.h"  //GetProblemPointerFromIndex()
#include "problem.h"  //ourselves
#include "selcore.h"  //selcoreGetSelectedCoreForContest()
#include "cpucheck.h" //hardware detection
#include "console.h"  //ConOutErr
#include "triggers.h" //RaiseExitRequestTrigger()

//#define STRESS_THREADS_AND_BUFFERS /* !be careful with this! */

#ifndef MINIMUM_ITERATIONS
#define MINIMUM_ITERATIONS 24
/* 
   MINIMUM_ITERATIONS determines minimum number of iterations that will 
   be requested, as well as the boundary on which number of iterations will
   be aligned. This then automatically implies keysdone alignment as well.
   [This applies to partially completed work loaded from disk as well,
   since partially completed work will (should!) never end up on a cruncher 
   that it did not originate on because core cpu, core #, client os and 
   client version are saved in partially completed work, and work is reset 
   by LoadState if any of them don't match.
   24 was chosen because it is evenly divisible by any/all the
   pipeline_counts currently in use (1,2,3,4[,6?])
*/   
#endif

/* ------------------------------------------------------------------- */

static unsigned int __problem_counter = 0; 
/* 
   problem_counter is what we copy as threadindex and then increment 
   for the next problem. Its addressed/incremented in the constructor 
   and addressed/decremented in the destructor, both of which are 
   'thread safe' in the sense that they are never called from the 
   actual crunching threads themselves.
*/

Problem::~Problem()
{
  __problem_counter--;
  initialized = started = 0;
}

/* ------------------------------------------------------------------- */

Problem::Problem(void)
{
  threadindex = __problem_counter++;
  initialized = 0;
  started = 0;

  //align core_membuffer to 16byte boundary
  {
    char *p = &__core_membuffer_space[0];
    while ((((unsigned long)p) & ((1UL << CORE_MEM_ALIGNMENT) - 1)) != 0)
      p++;
    core_membuffer = p;
  }

  {
    unsigned int sz = sizeof(int);
    if (sz < sizeof(u32)) /* need to do it this way to suppress compiler warnings. */
    {
      LogScreen("FATAL: sizeof(int) < sizeof(u32)\n");
      //#error "everything assumes a 32bit CPU..."
      RaiseExitRequestTrigger();
      return;
    }
    else
    {
      /* 
       this next part is essential for alpha, but is probably beneficial to
       all platforms. If it fails for your os/cpu, we may need to redesign 
       how objects are allocated/how rc5unitwork is addressed, so let me know.
                                                         -cyp Jun 14 1999
      */
      RC5UnitWork *w = &rc5unitwork;
      unsigned long ww = ((unsigned long)w);
    
      #if (CLIENT_CPU == CPU_ALPHA) /* sizeof(long) can be either 4 or 8 */
      ww &= 0x7; /* (sizeof(longword)-1); */
      #else
      ww &= (sizeof(int)-1); /* int alignment */
      #endif        
      if (ww) 
      {
        Log("rc5unitwork for problem %d is misaligned!\n", threadindex);
        RaiseExitRequestTrigger();
        return;
      }  
    }
  }

//LogScreen("Problem created. threadindex=%u\n", threadindex);

  #ifdef STRESS_THREADS_AND_BUFFERS 
  {
    static int runlevel = 0;
    if (runlevel != -12345)
    {
      if ((++runlevel) != 1)
      {
        --runlevel;
        return;
      }
      RaisePauseRequestTrigger();
      LogScreen("Warning! STRESS_THREADS_AND_BUFFERS is defined.\n"
                "Are you sure that the client is pointing at\n"
                "a test proxy? If so, type 'yes': ");
      char getyes[10];
      ConInStr(getyes,4,0);
      ClearPauseRequestTrigger();
      if (strcmpi(getyes,"yes") != 0)
      {
        runlevel = +12345;
        RaiseExitRequestTrigger();
        return;
      }
      runlevel = -12345;
    }
  }
  #endif    
}

/* ------------------------------------------------------------------- */

// for some odd reasons, the RC5 algorithm requires keys in reversed order
//         key.hi   key.lo
// ie key 01234567:89ABCDEF is sent to rc5_unit_func like that :
//        EFCDAB89:67452301
// This function switches from one format to the other.
//
// [Even if it looks like a little/big endian problem, it isn't. Whatever
//  endianess the underlying system has, we must swap every byte in the key
//  before sending it to rc5_unit_func()]
//
// Note that DES has a similiar but far more complex system, but everything
// is handled by des_unit_func().

static void  __SwitchRC5Format(u32 *hi, u32 *lo)
{                                                                       
    register u32 tempkeylo = *hi; /* note: we switch the order */  
    register u32 tempkeyhi = *lo;                                  
                                                                        
    *lo =                                                          
      ((tempkeylo >> 24) & 0x000000FFL) |                               
      ((tempkeylo >>  8) & 0x0000FF00L) |                               
      ((tempkeylo <<  8) & 0x00FF0000L) |                               
      ((tempkeylo << 24) & 0xFF000000L);                                
    *hi =                                                          
      ((tempkeyhi >> 24) & 0x000000FFL) |                               
      ((tempkeyhi >>  8) & 0x0000FF00L) |                               
      ((tempkeyhi <<  8) & 0x00FF0000L) |                               
      ((tempkeyhi << 24) & 0xFF000000L);                                
}                                                                       

/* ------------------------------------------------------------------- */

// Input:  - an RC5 key in 'mangled' (reversed) format or a DES key
//         - an incrementation count
//         - a contest identifier (0==RC5 1==DES 2==OGR 3==CSC)
//
// Output: the key incremented

static void __IncrementKey(u32 *keyhi, u32 *keylo, u32 iters, int contest)        
{                                                                   
  switch (contest)                                                  
  {                                                                 
    case RC5:
      __SwitchRC5Format(keyhi,keylo);                                      
      *keylo = *keylo + iters;                                             
      if (*keylo < iters) *keyhi = *keyhi + 1;
      __SwitchRC5Format (keyhi,keylo);                                      
      break;                                                        
    case DES:
    case CSC:
      *keylo = *keylo + iters;                                             
      if (*keylo < iters) *keyhi = *keyhi + 1; /* Account for carry */
      break;                                                        
    case OGR:
      /* This should never be called for OGR */                     
      break;                                                        
  }                                                                 
}

/* ------------------------------------------------------------- */

u32 Problem::CalcPermille() /* % completed in the current block, to nearest 0.1%. */
{ 
  u32 retpermille = 0;
  if (initialized && last_resultcode >= 0)
  {
    if (!started)
      retpermille = startpermille;
    else if (last_resultcode != RESULT_WORKING)
      retpermille = 1000;
    else
    {
      switch (contest)
      {
        case RC5:
        case DES:
        case CSC:
                {
                retpermille = (u32)( ((double)(1000.0)) *
                (((((double)(contestwork.crypto.keysdone.hi))*((double)(4294967296.0)))+
                             ((double)(contestwork.crypto.keysdone.lo))) /
                ((((double)(contestwork.crypto.iterations.hi))*((double)(4294967296.0)))+
                             ((double)(contestwork.crypto.iterations.lo)))) ); 
                break;
                }
        case OGR:
                WorkStub curstub;
                ogr->getresult(core_membuffer, &curstub, sizeof(curstub));
                // This is just a quick&dirty calculation that resembles progress.
                retpermille = curstub.stub.diffs[contestwork.ogr.workstub.stub.length]*10
                            + curstub.stub.diffs[contestwork.ogr.workstub.stub.length+1]/10;
                break;
      }
    }
    if (retpermille > 1000)
      retpermille = 1000;
  }
  return retpermille;
}

/* ------------------------------------------------------------------- */

int Problem::LoadState( ContestWork * work, unsigned int contestid, 
              u32 _iterations, int expected_cputype, 
              int expected_corenum, int expected_os,
              int expected_buildfrac )
{
  last_resultcode = -1;
  started = initialized = 0;
  timehi = timelo = 0;
  runtime_sec = runtime_usec = 0;
  completion_timelo = completion_timehi = 0;
  last_runtime_sec = last_runtime_usec = 0;
  memset((void *)&profiling, 0, sizeof(profiling));
  startpermille = permille = 0;
  loaderflags = 0;
  contest = contestid;
  tslice = _iterations;
  was_reset = 0;

  if (!IsProblemLoadPermitted(threadindex, contestid))
    return -1;

  client_cpu = CLIENT_CPU; /* usual case */
  use_generic_proto = 0; /* assume RC5/DES unit_func is _not_ generic form */
  cruncher_is_asynchronous = 0; /* not a co-processor */
  memset( &unit_func, 0, sizeof(unit_func) );
  coresel = selcoreSelectCore( contestid, threadindex, &client_cpu, this );
  if (coresel < 0)
    return -1;
    
  //----------------------------------------------------------------

  switch (contest) 
  {
    case RC5: 
    if ((MINIMUM_ITERATIONS % pipeline_count) != 0)
    {
      LogScreen("(MINIMUM_ITERATIONS % pipeline_count) != 0)\n");
      return -1;
    }  
    /* fallthrough */  
    #if defined(HAVE_DES_CORES)
    case DES:
    #endif
    #if defined(HAVE_CSC_CORES)
    case CSC: // HAVE_CSC_CORES
    #endif
    {
      // copy over the state information
      contestwork.crypto.key.hi = ( work->crypto.key.hi );
      contestwork.crypto.key.lo = ( work->crypto.key.lo );
      contestwork.crypto.iv.hi = ( work->crypto.iv.hi );
      contestwork.crypto.iv.lo = ( work->crypto.iv.lo );
      contestwork.crypto.plain.hi = ( work->crypto.plain.hi );
      contestwork.crypto.plain.lo = ( work->crypto.plain.lo );
      contestwork.crypto.cypher.hi = ( work->crypto.cypher.hi );
      contestwork.crypto.cypher.lo = ( work->crypto.cypher.lo );
      contestwork.crypto.keysdone.hi = ( work->crypto.keysdone.hi );
      contestwork.crypto.keysdone.lo = ( work->crypto.keysdone.lo );
      contestwork.crypto.iterations.hi = ( work->crypto.iterations.hi );
      contestwork.crypto.iterations.lo = ( work->crypto.iterations.lo );

      if (contestwork.crypto.keysdone.lo || contestwork.crypto.keysdone.hi)
      {
        if (client_cpu != expected_cputype || coresel != expected_corenum ||
            CLIENT_OS != expected_os || CLIENT_BUILD_FRAC!=expected_buildfrac)
        { 
          contestwork.crypto.keysdone.lo = contestwork.crypto.keysdone.hi = 0;
          was_reset = 1;
        }  
      }

      #if 0
      //this next if is from original TimC post-load-from-disk code, but it
      //doesn't look valid anymore
      if (((wrdata.work.crypto.iterations.lo) & 0x00000001L) == 1)
      {
        // If packet was finished with an 'odd' number of keys done, 
        // then make redo the last key
        wrdata.work.crypto.iterations.lo = wrdata.work.crypto.iterations.lo & 0xFFFFFFFEL;
        wrdata.work.crypto.key.lo = wrdata.work.crypto.key.lo & 0xFEFFFFFFL;
      }
      #endif

      //determine starting key number. accounts for carryover & highend of keysdone
      rc5unitwork.L0.hi = contestwork.crypto.key.hi + contestwork.crypto.keysdone.hi + 
         ((((contestwork.crypto.key.lo & 0xffff) + (contestwork.crypto.keysdone.lo & 0xffff)) + 
           ((contestwork.crypto.key.lo >> 16) + (contestwork.crypto.keysdone.lo >> 16))) >> 16);
      rc5unitwork.L0.lo = contestwork.crypto.key.lo + contestwork.crypto.keysdone.lo;
      if (contest == RC5)
        __SwitchRC5Format(&(rc5unitwork.L0.hi), &(rc5unitwork.L0.lo));
      refL0.lo = rc5unitwork.L0.lo;
      refL0.hi = rc5unitwork.L0.hi;

      // set up the unitwork structure
      rc5unitwork.plain.hi = contestwork.crypto.plain.hi ^ contestwork.crypto.iv.hi;
      rc5unitwork.plain.lo = contestwork.crypto.plain.lo ^ contestwork.crypto.iv.lo;
      rc5unitwork.cypher.hi = contestwork.crypto.cypher.hi;
      rc5unitwork.cypher.lo = contestwork.crypto.cypher.lo;

      if (contestwork.crypto.keysdone.lo!=0 || contestwork.crypto.keysdone.hi!=0 )
      {
        startpermille = (u32)( ((double)(1000.0)) *
        (((((double)(contestwork.crypto.keysdone.hi))*((double)(4294967296.0)))+
                           ((double)(contestwork.crypto.keysdone.lo))) /
        ((((double)(contestwork.crypto.iterations.hi))*((double)(4294967296.0)))+
                        ((double)(contestwork.crypto.iterations.lo)))) );
      }     
      break;
    }
    #if defined(HAVE_OGR_CORES)
    case OGR:
    {
      contestwork.ogr = work->ogr;
      if (contestwork.ogr.nodes.hi != 0 || contestwork.ogr.nodes.lo != 0)
      {
        if (client_cpu != expected_cputype || coresel != expected_corenum ||
            CLIENT_OS != expected_os || CLIENT_BUILD_FRAC!=expected_buildfrac)
        { 
          was_reset = 1;
          contestwork.ogr.workstub.worklength = contestwork.ogr.workstub.stub.length;
          contestwork.ogr.nodes.hi = contestwork.ogr.nodes.lo = 0;
        }  
      }
      extern CoreDispatchTable *ogr_get_dispatch_table();
      #if(CLIENT_OS == OS_MACOS)
      extern CoreDispatchTable *vec_ogr_get_dispatch_table();
      if(coresel == 1)
        ogr = vec_ogr_get_dispatch_table();
      else
        ogr = ogr_get_dispatch_table();
      #else
      ogr = ogr_get_dispatch_table();
      #endif
      int r = ogr->init();
      if (r != CORE_S_OK)
        return -1;
      r = ogr->create(&contestwork.ogr.workstub, 
                      sizeof(WorkStub), core_membuffer, MAX_MEM_REQUIRED_BY_CORE);
      if (r != CORE_S_OK)
        return -1;
      if (contestwork.ogr.workstub.worklength > contestwork.ogr.workstub.stub.length)
      {
        // This is just a quick&dirty calculation that resembles progress.
        startpermille = contestwork.ogr.workstub.stub.diffs[contestwork.ogr.workstub.stub.length]*10
                      + contestwork.ogr.workstub.stub.diffs[contestwork.ogr.workstub.stub.length+1]/10;
      }
      break;
    }  
    #endif
    default:  
      return -1;
  }

  //---------------------------------------------------------------

  last_resultcode = RESULT_WORKING;
  initialized = 1;

  return( 0 );
}

/* ------------------------------------------------------------------- */

int Problem::RetrieveState( ContestWork * work, unsigned int *contestid, int dopurge )
{
  if (!initialized)
    return -1;
  if (work) // store back the state information
  {
    switch (contest) {
      case RC5:
      case DES:
      case CSC:
        // nothing special needs to be done here
        break;
      case OGR:
        ogr->getresult(core_membuffer, &contestwork.ogr.workstub, sizeof(WorkStub));
        break;
    }
    memcpy( (void *)work, (void *)&contestwork, sizeof(ContestWork));
  }
  if (contestid)
    *contestid = contest;
  if (dopurge)
    initialized = 0;
  if (last_resultcode < 0)
    return -1;
  return ( last_resultcode );
}

/* ------------------------------------------------------------- */

int Problem::Run_RC5(u32 *keyscheckedP /* count of ... */, int *resultcode)
{
  s32 rescode = -1;

  /* a brace to ensure 'keystocheck' is not referenced in the common part */
  {
    u32 keystocheck = *keyscheckedP;
    // don't allow a too large of a keystocheck be used ie (>(iter-keysdone)) 
    // (technically not necessary, but may save some wasted time)
    if (contestwork.crypto.keysdone.hi == contestwork.crypto.iterations.hi)
    {
      u32 todo = contestwork.crypto.iterations.lo-contestwork.crypto.keysdone.lo;
      if (todo < keystocheck)
        keystocheck = todo;
    }

    if (keystocheck < MINIMUM_ITERATIONS)
      keystocheck = MINIMUM_ITERATIONS;
    else if ((keystocheck % MINIMUM_ITERATIONS) != 0)
      keystocheck += (MINIMUM_ITERATIONS - (keystocheck % MINIMUM_ITERATIONS));

    #if 0
    LogScreen("align iterations: effective iterations: %lu (0x%lx),\n"
              "suggested iterations: %lu (0x%lx)\n"
              "pipeline_count = %lu, iterations%%pipeline_count = %lu\n", 
              (unsigned long)keystocheck, (unsigned long)keystocheck,
              (unsigned long)(*keyscheckedP), (unsigned long)(*keyscheckedP),
              pipeline_count, keystocheck%pipeline_count );
    #endif

    if (use_generic_proto)
    {
      //we don't care about pipeline_count when using unified cores. 
      //we _do_ need to care that the keystocheck and starting key are aligned.

      *keyscheckedP = keystocheck; /* Pass 'keystocheck', get back 'keyschecked'*/
      rescode = (*(unit_func.gen))(&rc5unitwork,keyscheckedP,core_membuffer);

      if (rescode >= 0 && cruncher_is_asynchronous) /* co-processor or similar */
      {
        keystocheck = *keyscheckedP; /* always so */
        /* how this works:
         - for RESULT_FOUND, we don't need to do anything, since keyscheckedP 
           has the real count of iters done. If we were still using old style
           method of determining RESULT_FOUND by (keyscheckedP < keystocheck), 
           then we would simply need to set 'keystocheck = 1 + *keyscheckedP'
           to make it greater.
         - for RESULT_NOTHING/RESULT_WORKING
           unlike normal cores, where RESULT_NOTHING and RESULT_WORKING
           are synonymous (RESULT_NOTHING from the core's perspective ==
           RESULT_WORKING from the client's perspective), async cores tell 
           us which-is-which through the keyscheckedP pointer. As long as 
           they are _WORKING, the *keyscheckedP (ie iterations_done) will be 
           zero. (And of course, incrementations checks will pass as long
           as iterations_done is zero :).
        */
        //(these next 3 lines are quite useless, since the actual state
        //is set lower down, but leave them in anyway to show how it works)
        //remember: keystocheck has already been set equal to *keyscheckedP
        if (rescode != RESULT_FOUND) /* RESULT_NOTHING/RESULT_WORKING */
        {
          rescode = *resultcode = RESULT_NOTHING; //assume we know something
          if (*keyscheckedP == 0)  /* still working */
            rescode = *resultcode = RESULT_WORKING;
        }  
      }
    }
    else /* old style */
    {  
      *keyscheckedP = (*(unit_func.rc5))(&rc5unitwork,(keystocheck/pipeline_count));
      //don't use the next few lines as a guide for conversion to unified
      //prototypes!  look at the end of rc5/ansi/rc5ansi_2-rg.cpp instead.
      if (*keyscheckedP < keystocheck)
        rescode = RESULT_FOUND;
      else if (*keyscheckedP == keystocheck)
        rescode = RESULT_WORKING; /* synonymous with RESULT_NOTHING */
      else 
        rescode = -1;  
    }
  } /* brace to ensure that 'keystocheck' is not referenced beyond here */
  /* -- the code from here on down is identical to that of CSC -- */

  if (rescode < 0) /* "kiter" error */
  {
    *resultcode = -1;
    return -1;
  }
  *resultcode = (int)rescode;

  // Increment reference key count
  __IncrementKey(&refL0.hi, &refL0.lo, *keyscheckedP, contest);

  // Compare ref to core key incrementation
  if (((refL0.hi != rc5unitwork.L0.hi) || (refL0.lo != rc5unitwork.L0.lo))
      && (*resultcode != RESULT_FOUND) )
  {
    if (contestwork.crypto.iterations.hi == 0 &&
        contestwork.crypto.iterations.lo == 0x20000) /* test case */
    {
      Log("RC5 incrementation mismatch:\n"
          "Debug Information: %08x:%08x - %08x:%08x\n",
          rc5unitwork.L0.hi, rc5unitwork.L0.lo, refL0.hi, refL0.lo);
    }      
    *resultcode = -1;
    return -1;
  };

  // Checks passed, increment keys done count.
  contestwork.crypto.keysdone.lo += *keyscheckedP;
  if (contestwork.crypto.keysdone.lo < *keyscheckedP)
    contestwork.crypto.keysdone.hi++;

  // Update data returned to caller
  if (*resultcode == RESULT_FOUND)  //(*keyscheckedP < keystocheck)
  {
    // found it!
    u32 keylo = contestwork.crypto.key.lo;
    contestwork.crypto.key.lo += contestwork.crypto.keysdone.lo;
    contestwork.crypto.key.hi += contestwork.crypto.keysdone.hi;
    if (contestwork.crypto.key.lo < keylo) 
      contestwork.crypto.key.hi++; // wrap occured ?
    return RESULT_FOUND;
  }

  if ( ( contestwork.crypto.keysdone.hi > contestwork.crypto.iterations.hi ) ||
       ( ( contestwork.crypto.keysdone.hi == contestwork.crypto.iterations.hi ) &&
       ( contestwork.crypto.keysdone.lo >= contestwork.crypto.iterations.lo ) ) )
  {
    // done with this block and nothing found
    *resultcode = RESULT_NOTHING;
    return RESULT_NOTHING;
  }
  // more to do, come back later.
  *resultcode = RESULT_WORKING;
  return RESULT_WORKING;    // Done with this round
}  

/* ------------------------------------------------------------- */

int Problem::Run_CSC(u32 *iterationsP, int *resultcode)
{
#ifndef HAVE_CSC_CORES
  *iterationsP = 0;
  *resultcode = -1;
  return -1;
#else  
  s32 rescode = (*(unit_func.gen))( &rc5unitwork, iterationsP, core_membuffer );

  if (rescode < 0) /* "kiter" error */
  {
    *resultcode = -1;
    return -1;
  }
  *resultcode = (int)rescode;

  // Increment reference key count
  __IncrementKey (&refL0.hi, &refL0.lo, *iterationsP, contest);

  // Compare ref to core key incrementation
  if ((refL0.hi != rc5unitwork.L0.hi) || (refL0.lo != rc5unitwork.L0.lo))
  { 
    if (contestwork.crypto.iterations.hi == 0 &&
        contestwork.crypto.iterations.lo == 0x20000) /* test case */
    {
      Log("CSC incrementation mismatch:\n"
          "expected %08x:%08x, got %08x:%08x\n",
          refL0.lo, refL0.hi, rc5unitwork.L0.lo, rc5unitwork.L0.hi );
    }
    *resultcode = -1;
    return -1;
  }

  // Checks passed, increment keys done count.
  contestwork.crypto.keysdone.lo += *iterationsP;
  if (contestwork.crypto.keysdone.lo < *iterationsP)
    contestwork.crypto.keysdone.hi++;

  // Update data returned to caller
  if (*resultcode == RESULT_FOUND)
  {
    u32 keylo = contestwork.crypto.key.lo;
    contestwork.crypto.key.lo += contestwork.crypto.keysdone.lo;
    contestwork.crypto.key.hi += contestwork.crypto.keysdone.hi;
    if (contestwork.crypto.key.lo < keylo) 
      contestwork.crypto.key.hi++; // wrap occured ?
    return RESULT_FOUND;
  }

  if ( ( contestwork.crypto.keysdone.hi > contestwork.crypto.iterations.hi ) ||
       ( ( contestwork.crypto.keysdone.hi == contestwork.crypto.iterations.hi ) &&
       ( contestwork.crypto.keysdone.lo >= contestwork.crypto.iterations.lo ) ) )
  {
    *resultcode = RESULT_NOTHING;
    return RESULT_NOTHING;
  }
  // more to do, come back later.
  *resultcode = RESULT_WORKING;
  return RESULT_WORKING; // Done with this round
#endif  
}

/* ------------------------------------------------------------- */

int Problem::Run_DES(u32 *iterationsP, int *resultcode)
{
#ifndef HAVE_DES_CORES
  *iterationsP = 0;  /* no keys done */
  *resultcode = -1; /* core error */
  return -1;
#else

  //iterationsP == in: suggested iterations, out: effective iterations
  u32 kiter = (*(unit_func.des))( &rc5unitwork, iterationsP, core_membuffer );

  __IncrementKey ( &refL0.hi, &refL0.lo, *iterationsP, contest);
  // Increment reference key count

  if (((refL0.hi != rc5unitwork.L0.hi) ||  // Compare ref to core
      (refL0.lo != rc5unitwork.L0.lo)) &&  // key incrementation
      (kiter == *iterationsP))
  {
    #if 0 /* can you spell "thread safe"? */
    Log("Internal Client Error #23: Please contact help@distributed.net\n"
        "Debug Information: %08x:%08x - %08x:%08x\n",
        rc5unitwork.L0.lo, rc5unitwork.L0.hi, refL0.lo, refL0.hi);
    #endif
    *resultcode = -1;
    return -1;
  };

  contestwork.crypto.keysdone.lo += kiter;
  if (contestwork.crypto.keysdone.lo < kiter)
    contestwork.crypto.keysdone.hi++;
    // Checks passed, increment keys done count.

  // Update data returned to caller
  if (kiter < *iterationsP)
  {
    // found it!
    u32 keylo = contestwork.crypto.key.lo;
    contestwork.crypto.key.lo += contestwork.crypto.keysdone.lo;
    contestwork.crypto.key.hi += contestwork.crypto.keysdone.hi;
    if (contestwork.crypto.key.lo < keylo) 
      contestwork.crypto.key.hi++; // wrap occured ?
    *resultcode = RESULT_FOUND;
    return RESULT_FOUND;
  }
  else if (kiter != *iterationsP)
  {
    #if 0 /* can you spell "thread safe"? */
    Log("Internal Client Error #24: Please contact help@distributed.net\n"
        "Debug Information: k: %x t: %x\n"
        "Debug Information: %08x:%08x - %08x:%08x\n", kiter, *iterationsP,
        rc5unitwork.L0.lo, rc5unitwork.L0.hi, refL0.lo, refL0.hi);
    #endif
    *resultcode = -1; /* core error */
    return -1;
  };

  if ( ( contestwork.crypto.keysdone.hi > contestwork.crypto.iterations.hi ) ||
     ( ( contestwork.crypto.keysdone.hi == contestwork.crypto.iterations.hi ) &&
     ( contestwork.crypto.keysdone.lo >= contestwork.crypto.iterations.lo ) ) )
  {
    // done with this block and nothing found
    *resultcode = RESULT_NOTHING;
    return RESULT_NOTHING;
  }

  // more to do, come back later.
  *resultcode = RESULT_WORKING;
  return RESULT_WORKING; // Done with this round
#endif /* #ifdef HAVE_DES_CORES */
}

/* ------------------------------------------------------------- */

int Problem::Run_OGR(u32 *iterationsP, int *resultcode)
{
#if !defined(HAVE_OGR_CORES)
  iterationsP = iterationsP;
#else
  int r, nodes;

  if (*iterationsP > 0x100000UL)
    *iterationsP = 0x100000UL;

  nodes = (int)(*iterationsP);
  r = ogr->cycle(core_membuffer, &nodes);
  *iterationsP = (u32)nodes;

  u32 newnodeslo = contestwork.ogr.nodes.lo + nodes;
  if (newnodeslo < contestwork.ogr.nodes.lo) {
    contestwork.ogr.nodes.hi++;
  }
  contestwork.ogr.nodes.lo = newnodeslo;

  switch (r) 
  {
    case CORE_S_OK:
    {
      r = ogr->destroy(core_membuffer);
      if (r == CORE_S_OK) 
      {
        *resultcode = RESULT_NOTHING;
        return RESULT_NOTHING;
      }
      break;
    }
    case CORE_S_CONTINUE:
    {
      *resultcode = RESULT_WORKING;
      return RESULT_WORKING;
    }
    case CORE_S_SUCCESS:
    {
      if (ogr->getresult(core_membuffer, &contestwork.ogr.workstub, sizeof(WorkStub)) == CORE_S_OK)
      {
        //Log("OGR Success!\n");
        contestwork.ogr.workstub.stub.length = 
                  (u16)(contestwork.ogr.workstub.worklength);
        *resultcode = RESULT_FOUND;
        return RESULT_FOUND;
      }
      break;
    }
  }
  /* Something bad happened */
#endif
 *resultcode = -1; /* this will cause the problem to be discarded */
 return -1;
}

/* ------------------------------------------------------------- */

int Problem::Run(void) /* returns RESULT_*  or -1 */
{
  static volatile int using_ptime = -1;
  struct timeval stop, start, pstart, clock_stop;
  int retcode, core_resultcode;
  u32 iterations;

  if ( !initialized )
    return ( -1 );

  if ( last_resultcode != RESULT_WORKING ) /* _FOUND, _NOTHING or -1 */
    return ( last_resultcode );

  CliClock(&start);
  if (using_ptime)
  {
    if (CliGetProcessTime(&pstart) < 0)
      using_ptime = 0;
  }      
    
  if (!started)
  {
    timehi = start.tv_sec; timelo = start.tv_usec;
    completion_timelo = completion_timehi = 0;
    runtime_sec = runtime_usec = 0;
    memset((void *)&profiling, 0, sizeof(profiling));
    started=1;

#ifdef STRESS_THREADS_AND_BUFFERS 
    contest = RC5;
    contestwork.crypto.key.hi = contestwork.crypto.key.lo = 0;
    contestwork.crypto.keysdone.hi = contestwork.crypto.iterations.hi;
    contestwork.crypto.keysdone.lo = contestwork.crypto.iterations.lo;
    runtime_usec = 1; /* ~1Tkeys for a 2^20 packet */
    completion_timelo = 1;
    last_resultcode = RESULT_NOTHING;
    return RESULT_NOTHING;
#endif    
  }

  /* 
    On return from the Run_XXX contestwork must be in a state that we
    can put away to disk - that is, do not expect the loader (probfill 
    et al) to fiddle with iterations or key or whatever.
    
    The Run_XXX functions do *not* update problem.last_resultcode, they use
    core_resultcode instead. This is so that members of the problem object
    that are updated after the resultcode has been set will not be out of
    sync when the main thread gets it with RetrieveState(). 
    
    note: although the value returned by Run_XXX is usually the same as 
    the core_resultcode it is not always so. For instance, if 
    post-LoadState() initialization  failed, but can be deferred, Run_XXX 
    may choose to return -1, but keep core_resultcode at RESULT_WORKING.
  */

  iterations = tslice;
  last_runtime_usec = last_runtime_sec = 0;
  core_resultcode = last_resultcode;
  retcode = -1;

  switch (contest)
  {
    case RC5: retcode = Run_RC5( &iterations, &core_resultcode );
              break;
    case DES: retcode = Run_DES( &iterations, &core_resultcode );
              break;
    case OGR: retcode = Run_OGR( &iterations, &core_resultcode );
              break;
    case CSC: retcode = Run_CSC( &iterations, &core_resultcode );
              break;
    default: retcode = core_resultcode = last_resultcode = -1;
       break;
  }

  
  if (retcode < 0) /* don't touch tslice or runtime as long as < 0!!! */
  {
    return -1;
  }
  
  core_run_count++;
  if (using_ptime)
  {
    //Warning for GetProcessTime(): if the OSs thread model is ala SunOS's LWP,
    //ie, threads don't get their own pid, then GetProcessTime() functionality 
    //is limited to single thread/benchmark/test only (the way it is now),
    //otherwise it will be return process time for all threads. 
    //This is asserted below too.
    if (CliGetProcessTime(&stop) < 0)
      using_ptime = 0;
    else if (using_ptime < 0)
    {
      CliClock(&clock_stop);
      if (((clock_stop.tv_sec < stop.tv_sec) ||
        (clock_stop.tv_sec==stop.tv_sec) && clock_stop.tv_usec<stop.tv_usec))
        using_ptime = 0; /* clock time can never be less than process time */
      else
        using_ptime = 1;
    }
    if (using_ptime)
    {
      start.tv_sec = pstart.tv_sec;
      start.tv_usec = pstart.tv_usec;
    }
  }
  if (!using_ptime || core_resultcode != RESULT_WORKING )
  {
    CliClock(&clock_stop);
    if (!using_ptime)
    {
      stop.tv_sec = clock_stop.tv_sec;
      stop.tv_usec = clock_stop.tv_usec;
    }
    if ( core_resultcode != RESULT_WORKING ) /* _FOUND, _NOTHING */
    {
      if (((u32)clock_stop.tv_usec) < timelo)
      {
        clock_stop.tv_usec += 1000000;
        clock_stop.tv_sec--;
      }
      completion_timehi = (((u32)clock_stop.tv_sec) - timehi);
      completion_timelo = (((u32)clock_stop.tv_usec) - timelo);
      if (completion_timelo >= 1000000)
      {
        completion_timelo-= 1000000;
        completion_timehi++;
      }
    }
  }
  if (stop.tv_sec < start.tv_sec || 
     (stop.tv_sec == start.tv_sec && stop.tv_usec <= start.tv_usec))
  {
    //AIEEE! clock is whacky (or unusably inaccurate if ==)
  }
  else
  {
    if (stop.tv_usec < start.tv_usec)
    {
      stop.tv_sec--;
      stop.tv_usec+=1000000L;
    }
    runtime_usec += (last_runtime_usec = (stop.tv_usec - start.tv_usec));
    runtime_sec  += (last_runtime_sec = (stop.tv_sec - start.tv_sec));
    if (runtime_usec >= 1000000L)
    {
      runtime_sec++;
      runtime_usec-=1000000L;
    }
  }

  tslice = iterations;

  last_resultcode = core_resultcode;
  return last_resultcode;
}

/* ----------------------------------------------------------------------- */

int IsProblemLoadPermitted(long prob_index, unsigned int contest_i)
{
  prob_index = prob_index; /* possibly unused */

  #if (CLIENT_OS == OS_RISCOS) && defined(HAVE_X86_CARD_SUPPORT)
  if (prob_index == 1 && /* thread number reserved for x86 card */
     contest_i != RC5 && /* RISC OS x86 thread only supports RC5 */
     GetNumberOfDetectedProcessors() > 1) /* have x86 card */
    return 0;
  #endif
  
  switch (contest_i)
  {
    case RC5: 
    {
      return 1;
    }
    case DES:
    {
      #ifdef HAVE_DES_CORES
      return 1;
      #else
      return 0;
      #endif
    }
    case OGR:
    {
      #ifdef HAVE_OGR_CORES
      return 1;
      #else
      return 0;
      #endif
    }
    case CSC:
    {
      #ifdef HAVE_CSC_CORES
      return 1;
      #else
      return 0;
      #endif
    }
  }
  return 0;
}

