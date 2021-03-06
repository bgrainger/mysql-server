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

#include <string.h>
#include <sys/types.h>
#include <string>
#include <utility>

#include "../../components/mysql_server/component_sys_var_service.h"
#include "../components/mysql_server/server_component.h"
#include "lex_string.h"
#include "m_ctype.h"
#include "m_string.h"
#include "map_helpers.h"
#include "my_compiler.h"
#include "my_getopt.h"
#include "my_inttypes.h"
#include "my_loglevel.h"
#include "my_macros.h"
#include "my_psi_config.h"
#include "my_sys.h"
#include "mysql/components/service.h"
#include "mysql/components/service_implementation.h"
#include "mysql/components/services/component_sys_var_service.h"
#include "mysql/components/services/log_shared.h"
#include "mysql/components/services/psi_memory_bits.h"
#include "mysql/psi/mysql_memory.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysql/service_mysql_alloc.h"
#include "mysql/udf_registration_types.h"
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/persisted_variable.h"// Persisted_variables_cache
#include "sql/set_var.h"
#include "sql/sql_plugin_var.h"
#include "sql/sql_show.h"
#include "sql/sql_table.h"
#include "sql/sys_vars_shared.h"
#include "sql/thr_malloc.h"
#include "sql_string.h"

#define FREE_RECORD(sysvar)                                                 \
  my_free((void *)                                                             \
          (reinterpret_cast<sys_var_pluginvar *> (sysvar)->plugin_var->name)); \
  my_free(reinterpret_cast<sys_var_pluginvar *> (sysvar)->plugin_var);         \
  delete reinterpret_cast<sys_var_pluginvar *> (sysvar);

PSI_memory_key key_memory_comp_sys_var;

#ifdef HAVE_PSI_INTERFACE
static PSI_memory_info comp_sys_var_memory[]=
{
  {&key_memory_comp_sys_var, "component_system_variables", 0, 0, PSI_DOCUMENT_ME}
};

void comp_sys_var_init_psi_keys(void)
{
  const char* category= "component_sys_vars";
  int count;

  count= static_cast<int>(array_elements(comp_sys_var_memory));
  mysql_memory_register(category, comp_sys_var_memory, count);
}
#endif /* HAVE_PSI_INTERFACE */

void mysql_comp_sys_var_services_init()
{

#ifdef HAVE_PSI_INTERFACE
  comp_sys_var_init_psi_keys();
#endif
  return;
}

int mysql_add_sysvar(sys_var *first)
{
  sys_var *var;

  var= first;
  /* A write lock should be held on LOCK_system_variables_hash */
  /* this fails if there is a conflicting variable name. see HASH_UNIQUE */
  mysql_rwlock_wrlock(&LOCK_system_variables_hash);
  if (!get_system_variable_hash()->emplace(to_string(var->name), var).second)
  {
    my_message_local(ERROR_LEVEL, "duplicate variable name '%s'!?",
                     var->name.str);
    mysql_rwlock_unlock(&LOCK_system_variables_hash);
    return 1;
  }
  /* Update system_variable_hash version. */
  system_variable_hash_version++;
  mysql_rwlock_unlock(&LOCK_system_variables_hash);
  return 0;
}

/**
  Register system variables.

  @param component_name Name of the component
  @param var_name Name of the variable
  @param flags tells about the variable type
  @param comment variable comment information
  @param check_func trigger function to be called at Check time
  @param update_func trigger function to be called at Update time
  @param check_arg type defined check constraints block
  @param variable_value place holder for variable value
  @return Status of performed operation
  @retval false success
  @retval true failure
*/
DEFINE_BOOL_METHOD(mysql_component_sys_variable_imp::register_variable,
  (const char *component_name,
   const char *var_name,
   int flags,
   const char *comment,
   mysql_sys_var_check_func check_func,
   mysql_sys_var_update_func update_func,
   void *check_arg,
   void *variable_value))
{
  try
  {
    struct sys_var_chain chain= { NULL, NULL };
    sys_var *sysvar MY_ATTRIBUTE((unused));
    char *com_sys_var_name, *optname;
    int com_sys_var_len;
    st_mysql_sys_var *opt= NULL;
    my_option *opts= NULL;
    bool ret= true;
    int opt_error;
    int *argc= get_remaining_argc();
    char ***argv= get_remaining_argv();
    void *mem;
    get_opt_arg_source *opts_arg_source;

    com_sys_var_len= strlen(component_name) + strlen(var_name) + 2;
    com_sys_var_name= (char *) my_malloc(key_memory_comp_sys_var,
                                        com_sys_var_len, MYF(0));
    strxmov(com_sys_var_name, component_name, ".", var_name, NullS);
    my_casedn_str(&my_charset_latin1, com_sys_var_name);

    if (!(mem= my_multi_malloc(key_memory_comp_sys_var, MY_ZEROFILL,
                               &opts, (sizeof(my_option) * 2),
                               &optname, com_sys_var_len,
                               &opts_arg_source, sizeof(get_opt_arg_source),
                               NULL)))
    {
       sql_print_error("Out of memory for component system variable '%s'.",
                       var_name);
       return ret;
    }

    strxmov(optname, component_name, ".", var_name, NullS);

    convert_underscore_to_dash(optname, com_sys_var_len-1);

    opts->name= optname;
    opts->comment= comment;
    opts->id= 0;

    opts->arg_source= opts_arg_source;
    opts->arg_source->m_path_name[0]= 0;
    opts->arg_source->m_source= enum_variable_source::COMPILED;

    switch (flags & PLUGIN_VAR_TYPEMASK) {
    case PLUGIN_VAR_BOOL:
      SYSVAR_BOOL_TYPE(bool) *sysvar_bool;

      sysvar_bool=
        (sysvar_bool_type *) my_malloc(key_memory_comp_sys_var,
                                       sizeof(sysvar_bool_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_bool, bool, check_func_bool,
                                   update_func_bool)

      BOOL_CHECK_ARG(bool) *bool_arg;
      bool_arg= (bool_check_arg_s *) check_arg;
      sysvar_bool->def_val= bool_arg->def_val;

      opt= (st_mysql_sys_var *) sysvar_bool;

      break;
    case PLUGIN_VAR_INT:
      SYSVAR_INTEGRAL_TYPE(int) *sysvar_int;
      sysvar_int=
        (sysvar_int_type *) my_malloc(key_memory_comp_sys_var,
                                      sizeof(sysvar_int_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_int, int, check_func_int,
                                   update_func_int)

      INTEGRAL_CHECK_ARG(int) *int_arg;
      int_arg= (int_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_int, int_arg)

      opt= (st_mysql_sys_var *) sysvar_int;
      break;
    case PLUGIN_VAR_INT | PLUGIN_VAR_UNSIGNED:
      SYSVAR_INTEGRAL_TYPE(uint) *sysvar_uint;
      sysvar_uint=
        (sysvar_uint_type *) my_malloc(key_memory_comp_sys_var,
                                       sizeof(sysvar_uint_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_uint, uint, check_func_int,
                                   update_func_int)

      INTEGRAL_CHECK_ARG(uint) *uint_arg;
      uint_arg= (uint_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_uint, uint_arg)

      opt= (st_mysql_sys_var *) sysvar_uint;
      break;
    case PLUGIN_VAR_LONG:
      SYSVAR_INTEGRAL_TYPE(long) *sysvar_long;
      sysvar_long=
        (sysvar_long_type *) my_malloc(key_memory_comp_sys_var,
                                       sizeof(sysvar_long_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_long, long, check_func_long,
                                   update_func_long)

      INTEGRAL_CHECK_ARG(long) *long_arg;
      long_arg= (long_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_long, long_arg)

      opt= (st_mysql_sys_var *) sysvar_long;
      break;
    case PLUGIN_VAR_LONG | PLUGIN_VAR_UNSIGNED:
      SYSVAR_INTEGRAL_TYPE(ulong) *sysvar_ulong;
      sysvar_ulong=
        (sysvar_ulong_type *) my_malloc(key_memory_comp_sys_var,
                                        sizeof(sysvar_ulong_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_ulong, ulong, check_func_long,
                                   update_func_long)

      INTEGRAL_CHECK_ARG(ulong) *ulong_arg;
      ulong_arg= (ulong_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_ulong, ulong_arg)

      opt= (st_mysql_sys_var *) sysvar_ulong;
      break;
    case PLUGIN_VAR_LONGLONG:
      SYSVAR_INTEGRAL_TYPE(longlong) *sysvar_longlong;
      sysvar_longlong=
        (sysvar_longlong_type *) my_malloc(key_memory_comp_sys_var,
                                           sizeof(sysvar_longlong_type),
                                           MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_longlong, longlong,
                                   check_func_longlong, update_func_longlong)

      INTEGRAL_CHECK_ARG(longlong) *longlong_arg;
      longlong_arg= (longlong_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_longlong, longlong_arg)

      opt= (st_mysql_sys_var *) sysvar_longlong;
      break;
    case PLUGIN_VAR_LONGLONG | PLUGIN_VAR_UNSIGNED:
      SYSVAR_INTEGRAL_TYPE(ulonglong) *sysvar_ulonglong;
      sysvar_ulonglong=
        (sysvar_ulonglong_type *) my_malloc(key_memory_comp_sys_var,
                                            sizeof(sysvar_ulonglong_type),
                                            MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_ulonglong, ulonglong,
                                   check_func_longlong, update_func_longlong)

      INTEGRAL_CHECK_ARG(ulonglong) *ulonglong_arg;
      ulonglong_arg= (ulonglong_check_arg_s *) check_arg;
      COPY_MYSQL_PLUGIN_VAR_REMAINING(sysvar_ulonglong, ulonglong_arg)

      opt= (st_mysql_sys_var *) sysvar_ulonglong;
      break;
    case PLUGIN_VAR_STR:
      SYSVAR_STR_TYPE(str) *sysvar_str;
      sysvar_str=
        (sysvar_str_type *) my_malloc(key_memory_comp_sys_var,
                                      sizeof(sysvar_str_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_str, char *,
                                   check_func_str, update_func_str)
      if (!update_func)
      {
        if (!(sysvar_str->flags & (PLUGIN_VAR_MEMALLOC | PLUGIN_VAR_READONLY)))
        {
          sysvar_str->flags|= PLUGIN_VAR_READONLY;
          sql_print_warning("variable %s of component %s was forced "
                            "to be read-only: string variable without "
                            "update_func and PLUGIN_VAR_MEMALLOC flag",
                            var_name, component_name);
        }
      }

      STR_CHECK_ARG(str) *str_arg;
      str_arg= (str_check_arg_s *) check_arg;
      sysvar_str->def_val= str_arg->def_val;

      opt= (st_mysql_sys_var *) sysvar_str;
      break;
    case PLUGIN_VAR_ENUM:
      SYSVAR_ENUM_TYPE(enum) *sysvar_enum;
      sysvar_enum=
        (sysvar_enum_type *) my_malloc(key_memory_comp_sys_var,
                                       sizeof(sysvar_enum_type), MYF(0));
      COPY_MYSQL_PLUGIN_VAR_HEADER(sysvar_enum, ulong, check_func_enum,
                                   update_func_long)

      ENUM_CHECK_ARG(enum) *enum_arg;
      enum_arg= (enum_check_arg_s *) check_arg;
      sysvar_enum->def_val= enum_arg->def_val;
      sysvar_enum->typelib= enum_arg->typelib;

      opt= (st_mysql_sys_var *) sysvar_enum;
      break;
    default:
      sql_print_error("Unknown variable type code 0x%x in component '%s'.",
                      flags, component_name);
      goto end;
    }

    plugin_opt_set_limits(opts, opt);
    opts->value= opts->u_max_value= *(uchar ***) (opt + 1);

    opt_error= handle_options(argc, argv, opts, NULL);
    /* Add back the program name handle_options removes */
    (*argc)++;
    (*argv)--;

    if (opt_error)
    {
      sql_print_error("Parsing options for variable '%s' failed.", var_name);
      if (opts)
        my_cleanup_options(opts);
      goto end;
    }

    sysvar= reinterpret_cast<sys_var *>
      (new sys_var_pluginvar(&chain, com_sys_var_name, opt));

    if (sysvar == NULL)
    {
      sql_print_error("Out of memory for component system variable '%s'.",
                      var_name);
      goto end;
    }

    sysvar->set_arg_source(opts->arg_source);
    sysvar->set_is_plugin(false);

    if (mysql_add_sysvar(chain.first))
    {
      FREE_RECORD(sysvar)
      goto end;
    }

    /*
      Once server is started and if there are few persisted plugin variables
      which needs to be handled, we do it here.
    */
    if (mysqld_server_started)
    {
      Persisted_variables_cache *pv= Persisted_variables_cache::get_instance();
      if (pv && pv->set_persist_options(TRUE))
      {
        sql_print_error("Setting persistent options for component variable"
                        " '%s' failed.", com_sys_var_name);
      }
    }
    ret= false;

end:
    my_free (mem);

    return ret;
  }
  catch (...)
  {
    mysql_components_handle_std_exception(__func__);
  }
  return true;
}

const char* get_variable_value(sys_var *system_var, char *val_buf,
                               size_t *val_length)
{
  const char *value= val_buf;
  char show_var_buffer[sizeof(SHOW_VAR)];
  SHOW_VAR *show= (SHOW_VAR *) show_var_buffer;
  const CHARSET_INFO *fromcs;
  const CHARSET_INFO *tocs= &my_charset_utf8mb4_bin;
  uint dummy_err;

  show->type= SHOW_SYS;
  show->name= system_var->name.str;
  show->value= (char *) system_var;

  mysql_mutex_lock(&LOCK_global_system_variables);
  value= get_one_variable(NULL, show, OPT_GLOBAL, show->type, NULL,
                          &fromcs, val_buf, val_length);
  mysql_mutex_unlock(&LOCK_global_system_variables);
  val_buf[*val_length]= '\0';

  /* convert the retrieved value to utf8mb4 */
  size_t new_len= (tocs->mbmaxlen * (*val_length)) / fromcs->mbminlen + 1;
  char *result= new char[new_len];
  memset(result, 0, new_len);
  *val_length= copy_and_convert(result, new_len, tocs, value, *val_length,
                                fromcs, &dummy_err);
  memcpy(val_buf, result, strlen(result)+1);
  delete []result;
  return val_buf;
}

/**
  Get the system variable value from the global structure.

  @param component_name Name of the component
  @param var_name Name of the variable
  @param [out] val Value of the variable
  @param [out] out_length_of_val length of the output buffer
  @return Status of performed operation
  @retval false success
  @retval true failure
*/
DEFINE_BOOL_METHOD(mysql_component_sys_variable_imp::get_variable,
  (const char *component_name,
   const char *var_name, void **val,
   size_t *out_length_of_val))
{
  try
  {
    String com_sys_var_name;
    sys_var *var;

    if (com_sys_var_name.reserve(strlen(component_name) + 1 +
                                 strlen(var_name) + 1) ||
        com_sys_var_name.append(component_name) ||
        com_sys_var_name.append(".") ||
        com_sys_var_name.append(var_name))
      return true; // OOM
    mysql_rwlock_rdlock(&LOCK_system_variables_hash);
    var= intern_find_sys_var(com_sys_var_name.c_ptr(),
                             com_sys_var_name.length());
    mysql_rwlock_unlock(&LOCK_system_variables_hash);

    if (var)
      get_variable_value(var, (char *) *val, out_length_of_val);
    else
      return true;

    return false;
  }
  catch (...)
  {
    mysql_components_handle_std_exception(__func__);
  }
  return true;
}


/**
  Unregister system variables.

  @param component_name Name of the component
  @param var_name Variable name
  @return Status of performed operation
  @retval false success
  @retval true failure
*/
DEFINE_BOOL_METHOD(mysql_component_sys_variable_imp::unregister_variable,
  (const char *component_name,
   const char *var_name))
{
  try
  {
    int result= 0;
    String com_sys_var_name;

    if (com_sys_var_name.reserve(strlen(component_name) + 1 +
                                 strlen(var_name) + 1) ||
        com_sys_var_name.append(component_name) ||
        com_sys_var_name.append(".") ||
        com_sys_var_name.append(var_name))
      return true; // OOM
    mysql_rwlock_wrlock(&LOCK_system_variables_hash);

    sys_var *sysvar= nullptr;
    if (get_system_variable_hash() != nullptr)
    {
      sysvar= find_or_nullptr(*get_system_variable_hash(),
                              to_string(com_sys_var_name));
    }
    if (sysvar == nullptr)
    {
      my_message_local(ERROR_LEVEL, "variable name '%s' not found",
                       com_sys_var_name.c_ptr());
      mysql_rwlock_unlock(&LOCK_system_variables_hash);
      return true;
    }

    result= !get_system_variable_hash()->erase(to_string(sysvar->name));
    /* Update system_variable_hash version. */
    system_variable_hash_version++;
    mysql_rwlock_unlock(&LOCK_system_variables_hash);

    /*
       Freeing the value of string variables if they have PLUGIN_VAR_MEMALLOC
       flag enabled while registering variables.
    */
    int var_flags=
      reinterpret_cast<sys_var_pluginvar *> (sysvar)->plugin_var->flags;
    if (((var_flags & PLUGIN_VAR_TYPEMASK) == PLUGIN_VAR_STR) &&
        (var_flags & PLUGIN_VAR_MEMALLOC))
    {
      char *var_value=
        **(char ***)(reinterpret_cast<sys_var_pluginvar *>
                     (sysvar)->plugin_var + 1);
      if (var_value)
        my_free(var_value);
    }

    FREE_RECORD(sysvar)

    return (result != 0);
  }
  catch (...)
  {
    mysql_components_handle_std_exception(__func__);
  }
  return true;
}
