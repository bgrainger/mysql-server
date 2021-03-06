/* Copyright (c) 2000, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "sql/derror.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>

#include "../storage/perfschema/pfs_error.h"
#include "m_string.h"
#include "my_byteorder.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_loglevel.h"
#include "my_macros.h"
#include "my_sys.h"
#include "mysql/psi/mysql_file.h"
#include "mysql/service_mysql_alloc.h"
#include "mysql/udf_registration_types.h"
#include "mysqld_error.h"
#include "sql/current_thd.h"
#include "sql/log.h"
#include "sql/mysqld.h"                         // lc_messages_dir
#include "sql/psi_memory_key.h"
#include "sql/session_tracker.h"
#include "sql/sql_class.h"                      // THD
#include "sql/sql_locale.h"
#include "sql/system_variables.h"
#include "sql/table.h"

CHARSET_INFO *error_message_charset_info;

static const char *ERRMSG_FILE = "errmsg.sys";


/**
  Find the error message record for a given MySQL error code in the
  array of registered messages.
  The result is an index for said array; this value should not be
  considered stable between subsequent invocations of the server.

  @param mysql_errno  the error code to look for

  @retval -1          no message registered for this error code
  @retval >=0         index
 */
int mysql_errno_to_builtin(uint mysql_errno)
{
  int offset= 0; // Position where the current section starts in the array.
  int i;
  int temp_errno= (int)mysql_errno;

  for (i= 0; i < NUM_SECTIONS; i++)
  {
    if (temp_errno >= errmsg_section_start[i] &&
        temp_errno < (errmsg_section_start[i] + errmsg_section_size[i]))
      return mysql_errno - errmsg_section_start[i] + offset;
    offset+= errmsg_section_size[i];
  }
  return -1; /* General error */
}


/*
  Error messages are stored sequentially in an array.
  But logically error messages are organized in sections where
  each section contains errors that are consecutively numbered.
  This function maps from a "logical" mysql_errno to an array
  index and returns the string.
*/
const char* MY_LOCALE_ERRMSGS::lookup(int mysql_errno)
{
  int offset= 0; // Position where the current section starts in the array.
  for (int i= 0; i < NUM_SECTIONS; i++)
  {
    if (mysql_errno < (errmsg_section_start[i] + errmsg_section_size[i]))
      return errmsgs[mysql_errno - errmsg_section_start[i] + offset];
    offset+= errmsg_section_size[i];
  }
  return "Invalid error code";
}


const char* ER_DEFAULT(int mysql_errno)
{
  return my_default_lc_messages->errmsgs->lookup(mysql_errno);
}


const char* ER_THD(const THD *thd, int mysql_errno)
{
  return thd->variables.lc_messages->errmsgs->lookup(mysql_errno);
}


C_MODE_START
const char *get_server_errmsgs(int mysql_errno)
{
  if (current_thd)
    return ER_THD(current_thd, mysql_errno);

  if ((my_default_lc_messages != nullptr) &&
      (my_default_lc_messages->errmsgs->is_loaded()))
    return ER_DEFAULT(mysql_errno);

  {
    server_error *sqlstate_map= &error_names_array[1];
    int           i= mysql_errno_to_builtin(mysql_errno);

    if (i >= 0)
      return sqlstate_map[i].text;
  }

  return nullptr;
}
C_MODE_END


bool init_errmessage()
{
  DBUG_ENTER("init_errmessage");

  /* Read messages from file. */
  (void)my_default_lc_messages->errmsgs->read_texts();

  if (!my_default_lc_messages->errmsgs->is_loaded())
    DBUG_RETURN(true); /* Fatal error, not able to allocate memory. */

  /* Register messages for use with my_error(). */
  for (int i= 0; i < NUM_SECTIONS; i++)
  {
    if (my_error_register(get_server_errmsgs,
                          errmsg_section_start[i],
                          errmsg_section_start[i] +
                          errmsg_section_size[i] - 1))
    {
      my_default_lc_messages->errmsgs->destroy();
      DBUG_RETURN(true);
    }
  }

  DBUG_RETURN(false);
}


void deinit_errmessage()
{
  for (int i= 0; i < NUM_SECTIONS; i++)
  {
    my_error_unregister(errmsg_section_start[i],
                        errmsg_section_start[i] +
                        errmsg_section_size[i] - 1);
  }
}


/**
  Read text from packed textfile in language-directory.

  @retval false          On success
  @retval true           On failure

  @note If we can't read messagefile then it's panic- we can't continue.
*/

bool MY_LOCALE_ERRMSGS::read_texts()
{
  uint i;
  uint no_of_errmsgs;
  size_t length;
  File file;
  char name[FN_REFLEN];
  char lang_path[FN_REFLEN];
  uchar *start_of_errmsgs= nullptr;
  uchar *pos= nullptr;
  uchar head[32];
  uint error_messages= 0;

  DBUG_ENTER("read_texts");

  for (int i= 0; i < NUM_SECTIONS; i++)
    error_messages+= errmsg_section_size[i];

  convert_dirname(lang_path, language, NullS);
  (void) my_load_path(lang_path, lang_path, lc_messages_dir);
  if ((file= mysql_file_open(key_file_ERRMSG,
                             fn_format(name, ERRMSG_FILE, lang_path, "", 4),
                             O_RDONLY,
                             MYF(0))) < 0)
  {
    /*
      Trying pre-5.5 sematics of the --language parameter.
      It included the language-specific part, e.g.:

      --language=/path/to/english/
    */
    if ((file= mysql_file_open(key_file_ERRMSG,
                               fn_format(name, ERRMSG_FILE,
                                         lc_messages_dir, "", 4),
                               O_RDONLY,
                               MYF(0))) < 0)
    {
      LogErr(ERROR_LEVEL, ER_ERRMSG_CANT_FIND_FILE, name);
      goto open_err;
    }

    LogErr(WARNING_LEVEL, ER_ERRMSG_LOADING_55_STYLE, lc_messages_dir);
  }

  // Read the header from the file
  if (mysql_file_read(file, (uchar*) head, 32, MYF(MY_NABP)))
    goto read_err;
  if (head[0] != (uchar) 254 || head[1] != (uchar) 254 ||
      head[2] != 3 || head[3] != 1 || head[4] != 1)
    goto read_err;

  error_message_charset_info= system_charset_info;
  length= uint4korr(head+6);
  no_of_errmsgs= uint4korr(head+10);

  if (no_of_errmsgs < error_messages)
  {
    LogErr(ERROR_LEVEL, ER_ERRMSG_MISSING_IN_FILE,
           name,no_of_errmsgs, error_messages);
    (void) mysql_file_close(file, MYF(MY_WME));
    goto open_err;
  }

  // Free old language and allocate for the new one
  my_free(errmsgs);
  if (!(errmsgs= (const char**)
        my_malloc(key_memory_errmsgs,
                  length+no_of_errmsgs*sizeof(char*), MYF(0))))
  {
    LogErr(ERROR_LEVEL, ER_ERRMSG_OOM, name);
    (void) mysql_file_close(file, MYF(MY_WME));
    DBUG_RETURN(true);
  }

  // Get pointer to Section2.
  start_of_errmsgs= (uchar*) (errmsgs + no_of_errmsgs);

  /*
    Temporarily read message offsets into Section2.
    We cannot read these 4 byte offsets directly into Section1,
    as pointer size vary between processor architecture.
  */
  if (mysql_file_read(file, start_of_errmsgs, (size_t) no_of_errmsgs*4,
                      MYF(MY_NABP)))
    goto read_err_init;

  // Copy the message offsets to Section1.
  for (i= 0, pos= start_of_errmsgs; i< no_of_errmsgs; i++)
  {
    errmsgs[i]= (char*) start_of_errmsgs+uint4korr(pos);
    pos+= 4;
  }

  // Copy all the error text messages into Section2.
  if (mysql_file_read(file, start_of_errmsgs, length, MYF(MY_NABP)))
    goto read_err_init;

  (void) mysql_file_close(file, MYF(0));

  DBUG_RETURN(false);

read_err_init:
  /*
    At this point, we've already thrown away any old, valid setup
    we may have had, and we have a half set up message-set.
    Release the mess and fall through to init from built-ins below!
  */
  my_free(errmsgs);
  errmsgs= nullptr;
read_err:
  LogErr(ERROR_LEVEL, ER_ERRMSG_CANT_READ, name);
  (void) mysql_file_close(file, MYF(MY_WME));
open_err:
  /*
    We may have failed now, but we may still have succeeded earlier,
    so check whether we've got errmsgs from the previous time!
  */
  if (!errmsgs)
  {
    /*
      If we can't read the messages from disk, allocate space just for
      the pointers, and set up pointers to reference our built-in defaults
      for the messages. Since (messages + pointers) is allocated and freed
      as one contiguous memory block, this will still be released correctly
      at shutdown.
    */
    if ((errmsgs= (const char **) my_malloc(key_memory_errmsgs,
                                            error_messages *
                                             sizeof(char*), MYF(0))))
    {
      server_error *sqlstate_map= &error_names_array[1];

      for (uint i= 0; i < error_messages; ++i)
        errmsgs[i]= sqlstate_map[i].text;
    }
  }

  DBUG_RETURN(true);
} /* read_texts */


void MY_LOCALE_ERRMSGS::destroy()
{
  my_free(errmsgs);
}
