/* Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.

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

#include "sql/dd/impl/system_views/referential_constraints.h"

namespace dd {
namespace system_views {

const Referential_constraints &Referential_constraints::instance()
{
  static Referential_constraints *s_instance= new Referential_constraints();
  return *s_instance;
}

Referential_constraints::Referential_constraints()
{
  m_target_def.set_view_name(view_name());

  m_target_def.add_field(FIELD_CONSTRAINT_CATALOG,
                         "CONSTRAINT_CATALOG", "cat.name");
  m_target_def.add_field(FIELD_CONSTRAINT_SCHEMA,
                         "CONSTRAINT_SCHEMA", "sch.name");
  m_target_def.add_field(FIELD_CONSTRAINT_NAME, "CONSTRAINT_NAME", "fk.name");
  m_target_def.add_field(FIELD_UNIQUE_CONSTRAINT_CATALOG,
                         "UNIQUE_CONSTRAINT_CATALOG",
                         "fk.referenced_table_catalog");
  m_target_def.add_field(FIELD_UNIQUE_CONSTRAINT_SCHEMA,
                         "UNIQUE_CONSTRAINT_SCHEMA",
                         "fk.referenced_table_schema");
  m_target_def.add_field(FIELD_UNIQUE_CONSTRAINT_NAME,
                         "UNIQUE_CONSTRAINT_NAME",
                         "fk.unique_constraint_name");
  m_target_def.add_field(FIELD_MATCH_OPTION, "MATCH_OPTION",
                         "fk.match_option");
  m_target_def.add_field(FIELD_UPDATE_RULE, "UPDATE_RULE", "fk.update_rule");
  m_target_def.add_field(FIELD_DELETE_RULE, "DELETE_RULE", "fk.delete_rule");
  m_target_def.add_field(FIELD_TABLE_NAME, "TABLE_NAME", "tbl.name");
  m_target_def.add_field(FIELD_REFERENCED_TABLE_NAME, "REFERENCED_TABLE_NAME",
                         "fk.referenced_table_name");

  m_target_def.add_from("mysql.foreign_keys fk");
  m_target_def.add_from("JOIN mysql.tables tbl ON fk.table_id = tbl.id");
  m_target_def.add_from("JOIN mysql.schemata sch ON fk.schema_id= sch.id");
  m_target_def.add_from("JOIN mysql.catalogs cat ON cat.id=sch.catalog_id");

  m_target_def.add_where("CAN_ACCESS_TABLE(sch.name, tbl.name)");
  m_target_def.add_where("AND IS_VISIBLE_DD_OBJECT(tbl.hidden)");
}

}
}
