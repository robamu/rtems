/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Copyright (C) 2020 embedded brains GmbH (http://www.embedded-brains.de)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../spfatal_support/spfatal.h"

#define FATAL_ERROR_TEST_NAME       "33"
#define FATAL_ERROR_DESCRIPTION     "provoke too large TLS size"
#define FATAL_ERROR_EXPECTED_SOURCE INTERNAL_ERROR_CORE
#define FATAL_ERROR_EXPECTED_ERROR  INTERNAL_ERROR_TOO_LARGE_TLS_SIZE

static _Thread_local int tls[ RTEMS_TASK_STORAGE_ALIGNMENT ];

static void force_error( void )
{
  int var;

  var = tls[ 0 ];
  RTEMS_OBFUSCATE_VARIABLE( var );
  tls[ 0 ] = var;

  /* Not reached */
  rtems_test_assert( 0 );
}

#define CONFIGURE_MAXIMUM_THREAD_LOCAL_STORAGE_SIZE RTEMS_TASK_STORAGE_ALIGNMENT

#include "../spfatal_support/spfatalimpl.h"
