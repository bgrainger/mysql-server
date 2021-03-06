/*
 * Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef X_TESTS_DRIVER_CONNECTOR_MYSQLX_ALL_MSGS_H_
#define X_TESTS_DRIVER_CONNECTOR_MYSQLX_ALL_MSGS_H_

#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "mysqlxclient/xprotocol.h"


using  Message_by_full_name = std::map<std::string, std::string>;

template<typename Message_id>
using Message_by_name = std::map<
    std::string,
    std::pair<xcl::XProtocol::Message *(*)(), Message_id>>;

using Message_server_by_name =
    Message_by_name<xcl::XProtocol::Server_message_type_id>;

using Message_client_by_name =
    Message_by_name<xcl::XProtocol::Client_message_type_id>;

template<typename Message_id>
using Message_by_id = std::map<
    Message_id,
    std::pair<xcl::XProtocol::Message *(*)(),
              std::string>>;

using Message_server_by_id =
    Message_by_id<xcl::XProtocol::Server_message_type_id>;

using Message_client_by_id =
    Message_by_id<xcl::XProtocol::Client_message_type_id>;

extern Message_by_full_name server_msgs_by_full_name;
extern Message_by_full_name client_msgs_by_full_name;

extern Message_server_by_name server_msgs_by_name;
extern Message_client_by_name client_msgs_by_name;

extern Message_server_by_id server_msgs_by_id;
extern Message_client_by_id client_msgs_by_id;

#endif  // X_TESTS_DRIVER_CONNECTOR_MYSQLX_ALL_MSGS_H_
