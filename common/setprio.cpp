/* Copyright distributed.net 1997-1999 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * ------------------------------------------------------------------
 *  'prio' is a value on the scale of 0 to 9, where 0 is the lowest
 *  priority and 9 is the highest priority [9 is what the priority would 
 *  be if priority were not set, ie is 'normal' priority.] 
 * ------------------------------------------------------------------
*/
const char *setprio_cpp(void) {
return "@(#)$Id: setprio.cpp,v 1.50 1999/04/05 17:56:52 cyp Exp $"; }

#include "cputypes.h"  // CLIENT_OS, CLIENT_CPU
#include "client.h"    // MAXCPUS, Packet, FileHeader, Client class, etc
#include "baseincs.h"  // basic (even if port-specific) #includes

// -----------------------------------------------------------------------

static int __SetPriority( unsigned int prio, int set_for_thread )
{
  if (((int)prio) < 0 || prio > 9) 
    prio = 0;
 
  #if (CLIENT_OS == OS_MACH)
    {
    if ( set_for_thread )
      {
      cthread_t thrid = cthread_self();
      int newprio = __mach_get_max_thread_priority( thrid, NULL );
      if (prio < 9) 
        newprio = (newprio * 9)/10;
      if (newprio == 0)
        newprio++;
      if (cthread_priority( thrid, newprio, 0 ) != KERN_SUCCESS)
        return -1;
      }
    }
  #elif (CLIENT_OS == OS_OS2)
    {
    if ( set_for_thread )
      DosSetPriority( PRTYS_THREAD, PRTYC_IDLETIME, ((32 * prio)/10), 0);
    //main thread always runs at normal priority
    }
  #elif (CLIENT_OS == OS_WIN32)
    {
    static int useidleclass = -1;
    int threadprio = 0, classprio = 0;
    HANDLE our_thrid = GetCurrentThread();

    #ifdef WIN32GUI
    if (set_for_thread == 1) 
      prio=0;
    #endif
  
    /* ************************** Article ID: Q106253 *******************
                              process priority class
    THREAD_PRIORITY          Normal, in      Normal, in
                      Idle   Background      Foreground    High    Realtime
    _TIME_CRITICAL     15        15              15         15        31
    _HIGHEST            6         9              11         15        26
    _ABOVE_NORMAL       5         8              10         14        25
    _NORMAL             4         7               9         13        24
    _BELOW_NORMAL       3         6               8         12        23
    _LOWEST             2         5               7         11        22
    _IDLE               1         1               1          1        16
    ******************************************************************* */
  
    if (useidleclass == -1) /* not yet detected */
      {
      SetPriorityClass( GetCurrentProcess(), IDLE_PRIORITY_CLASS );
      Sleep(1);
      SetThreadPriority( our_thrid, THREAD_PRIORITY_TIME_CRITICAL);    
      if (GetThreadPriority( our_thrid ) == THREAD_PRIORITY_TIME_CRITICAL)
        useidleclass = 1;
      else
        {
        useidleclass = 0;
        SetPriorityClass( GetCurrentProcess(), NORMAL_PRIORITY_CLASS );
        Sleep(1);
        }
      SetThreadPriority( our_thrid, THREAD_PRIORITY_NORMAL );    
      }
    if (useidleclass == 1)
      {
      classprio = IDLE_PRIORITY_CLASS;
      if (!set_for_thread) threadprio = THREAD_PRIORITY_TIME_CRITICAL;/* 15 */
      else if (prio >= 7)  threadprio = THREAD_PRIORITY_HIGHEST;      /*  6 */
      else if (prio >= 5)  threadprio = THREAD_PRIORITY_ABOVE_NORMAL; /*  5 */
      else if (prio >= 4)  threadprio = THREAD_PRIORITY_NORMAL;       /*  4 */
      else if (prio >= 3)  threadprio = THREAD_PRIORITY_BELOW_NORMAL; /*  3 */
      else if (prio >= 2)  threadprio = THREAD_PRIORITY_LOWEST;       /*  2 */
      else /* prio < 2 */  threadprio = THREAD_PRIORITY_IDLE;         /*  1 */
      }
    else /* if (useidleclass == 0) */
      {
      classprio = NORMAL_PRIORITY_CLASS;
      if (!set_for_thread) threadprio = THREAD_PRIORITY_NORMAL;       /*  8 */
      else if (prio >= 7)  threadprio = THREAD_PRIORITY_BELOW_NORMAL; /*  6 */
      else if (prio >= 5)  threadprio = THREAD_PRIORITY_LOWEST;       /*  5 */
      else                 threadprio = THREAD_PRIORITY_IDLE;         /*  1 */
      }
    //SetPriorityClass( GetCurrentProcess(), classprio );
    //Sleep(1);
    SetThreadPriority( our_thrid, threadprio );
    }
  #elif (CLIENT_OS == OS_MACOS)
    {
    if ( set_for_thread )
      {
      // nothing
      }
    else
      {
      // nothing
      }
    }
  #elif (CLIENT_OS == OS_WIN16) || (CLIENT_OS == OS_WIN32S)
    {
    if ( set_for_thread )
      {
      // nothing
      }
    else
      {
      // nothing
      }
    }
  #elif (CLIENT_OS == OS_NETWARE)
    {
    if ( set_for_thread )
      {
      // nothing
      }
    else
      {
      // nothing
      }
    }
  #elif (CLIENT_OS == OS_DOS)
    {
    if ( set_for_thread )
      {
      // nothing
      }
    else
      {
      // nothing
      }
    }
  #elif (CLIENT_OS == OS_BEOS)
    {
    if ( set_for_thread )
      {
      // priority of crunching threads is set when they are created.
      }
    else
      {
      // Main thread runs at normal priority, since it does very little;
      }
    }
  #elif (CLIENT_OS == OS_RISCOS)
    {
    if ( set_for_thread )
      {
      // nothing - non threaded
      }
    else
      {
      // nothing
      }
    }
  #elif (CLIENT_OS == OS_VMS)
    {
    if ( set_for_thread )
      {
      // nothing - non threaded
      }
    else
      {
      nice( (10-(prio+1)) >> 1 ); /* map from 0-9 to 4-0 */
      // assumes base priority is the default 4. 0 is highest priority.
      // GO-VMS.COM can also be used
      }
    }
  #elif (CLIENT_OS == OS_AMIGAOS)
    {
    if ( set_for_thread )
      {
      //nothing - non threaded
      }
    else
      {
      int pri = -(((9-prio) * 10)/5); /* scale from 0-9 to -20 to zero */
      SetTaskPri(FindTask(NULL), pri ); 
      }
    }
  #elif (CLIENT_OS == OS_QNX)
    {
    if ( set_for_thread )
      {
      // nothing - non threaded
      }
    else
      {
      setprio( 0, prio-3 );
      }
    }
  #elif (CLIENT_OS == OS_IRIX)
    {
    if ( set_for_thread )
      {
      //nothing - priority is set when created.
      }
    else if (prio == 0)
      {
      schedctl( NDPRI, 0, NDPLOMIN );
      schedctl( RENICE, 0, 39);
      } 
    else if (prio < 9)
      schedctl( NDPRI, 0, (NDPLOMIN - NDPNORMMIN)/prio);
    }
  #else
    {
    #if (CLIENT_OS == OS_FREEBSD)
    #ifndef PRI_OTHER_MAX
    #define PRI_OTHER_MAX 10
    #endif
    #ifndef PRI_OTHER_MIN
    #define PRI_OTHER_MIN 20
    #endif
    #endif
    if ( set_for_thread )
      {
      #if defined(_POSIX_THREADS_SUPPORTED) //defined in cputypes.h
        #if defined(_POSIX_THREAD_PRIORITY_SCHEDULING)
          //nothing - priority is set when created
        #else
          //SCHED_OTHER policy
          int newprio;
          if ( prio == 9 )
            newprio = PRI_OTHER_MAX;
          else
            newprio = (PRI_OTHER_MIN + PRI_OTHER_MAX + 1) / 10;
          if (pthread_setprio(pthread_self(), newprio ) < 0)
            return -1;
        #endif
      #endif
      }
    else 
      {
      static int oldnice = -1;
      int newnice = ((22*(9-prio))+5)/10;  /* scale from 0-9 to 20-0 */
      if (oldnice != -1)
        {
        errno = 0;
        nice( -oldnice );   // note: assumes nice() handles the 
        if ( errno )        // (-20 to 20) range and not 0-40 
          return -1;
        }
      if ( newnice != 0 )
        {
        errno = 0;
        nice( newnice );
        if ( errno )
          return -1;
        }
      oldnice = newnice;
      }
    }
  #endif

  return 0;
}

// -----------------------------------------------------------------------

int SetGlobalPriority(unsigned int prio) 
{
  return __SetPriority( prio, 0 );
}

// -----------------------------------------------------------------------

int SetThreadPriority(unsigned int prio)
{
  return __SetPriority( prio, 1 );
}

// -----------------------------------------------------------------------

