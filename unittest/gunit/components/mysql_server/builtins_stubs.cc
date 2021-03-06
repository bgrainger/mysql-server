/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */


#ifndef BUILTIN_STUBS
#define BUILTIN_STUBS

#include <mysql/components/services/log_shared.h>
#include <atomic>

#include "my_compiler.h"
#include "sql/sql_class.h"

std::atomic<int32> connection_events_loop_aborted_flag;
thread_local THD  *current_thd= nullptr;
ulong              log_error_verbosity;
ulong              opt_log_timestamps= 0;
char              *opt_log_error_services= NULL;
const char        *log_error_dest= "stderr";

my_thread_id log_get_thread_id(THD *thd MY_ATTRIBUTE((unused)))
{ return -1; }

void        log_write_errstream(const char *buffer MY_ATTRIBUTE((unused)),
                                size_t length MY_ATTRIBUTE((unused)))
{ }

const char *mysql_errno_to_symbol(int mysql_errno MY_ATTRIBUTE((unused)))
{ return NULL; }

int         mysql_symbol_to_errno(const char *error_symbol MY_ATTRIBUTE((unused)))
{ return -1; }

const char *mysql_errno_to_sqlstate(uint mysql_errno MY_ATTRIBUTE((unused)))
{ return NULL; }

int         log_vmessage(int log_type MY_ATTRIBUTE((unused)),
                         va_list lili MY_ATTRIBUTE((unused)))
{ return -1;   }

int         log_message(int log_type MY_ATTRIBUTE((unused)), ...)
{ return -1;   }

C_MODE_START
const char *get_server_errmsgs(int mysql_errcode MY_ATTRIBUTE((unused)))
{ return NULL; }
C_MODE_END

#endif
