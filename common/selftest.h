/* Hey, Emacs, this a -*-C++-*- file !
 *
 * Copyright distributed.net 1997-2003 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
*/ 
#ifndef __SELFTEST_H__
#define __SELFTEST_H__ "@(#)$Id: selftest.h,v 1.9.4.2 2005/05/05 23:16:59 kakace Exp $"

/* returns number of tests if all passed or negative number if a test failed */
long SelfTest( unsigned int contest );
long StressTest( unsigned int contest );

#if defined(HAVE_RC5_72_CORES)
long StressRC5_72(void);  /* RC5-72/stress.cpp */
#endif

#endif /* __SELFTEST_H__ */
