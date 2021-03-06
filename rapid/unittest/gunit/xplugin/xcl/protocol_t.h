/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "client/xprotocol_impl.h"
#include "errmsg.h"
#include "mock/factory.h"
#include "mock/connection.h"
#include "mock/connection_state.h"
#include "mock/message_handler.h"
#include "mock/query_result.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "client/xssl_config.h"
#include "client/xconnection_config.h"
#include "message_helpers.h"


namespace xcl {
namespace test {

using ::testing::_;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Ref;

class Xcl_protocol_impl_tests : public Test {
 public:
  void SetUp() override {
    m_mock_connection = new StrictMock<Mock_connection>();

    EXPECT_CALL(m_mock_factory, create_connection_raw(_))
        .WillOnce(Return(m_mock_connection));

    m_sut.reset(new Protocol_impl(
        m_context,
        &m_mock_factory));
  }

  XQuery_result *expect_factory_new_result() {
    Mock_query_result *result = new Mock_query_result();
    static XQuery_result::Metadata metadata;

    EXPECT_CALL(m_mock_factory, create_result_raw(_, _, _))
      .WillOnce(Return(result));
    EXPECT_CALL(*result, get_metadata(_))
      .WillOnce(ReturnRef(metadata));

    return result;
  }

  void expect_write_header(const XProtocol::Client_message_type_id id,
                           const uint32 expected_payload_size,
                           const int32 error_code = 0) {
    EXPECT_CALL(*m_mock_connection, write(_, 5)).WillOnce(
        Invoke([id, expected_payload_size, error_code](
                    const uchar *data,
                    const std::size_t size MY_ATTRIBUTE((unused))) -> XError {
                 EXPECT_EQ(data[4], id);

                 std::array<uint8, 4> header = {{
                     data[0],
                     data[1],
                     data[2],
                     data[3]}};

#ifdef WORDS_BIGENDIAN
                 std::swap(header[0], header[3]);
                 std::swap(header[1], header[2]);
#endif

                 EXPECT_EQ(expected_payload_size,
                           *reinterpret_cast<int32*>(header.data()) - 1);
                 return XError{ error_code, "" };
               }));
  }

  template <typename Message_type>
  void expect_write_message_without_payload(
      const Message_from_str<Message_type> &message,
      const int32 error_code = 0) {
    const Message_type &message_base = message;

    expect_write_message_without_payload(message_base, error_code);
  }

  template <typename Message_type>
  void expect_write_message_without_payload(
      const Message_type &message,
      const int32 expected_error_code = 0) {
    auto message_binary = Message_encoder::encode(message);
    expect_write_header(
        Client_message<Message_type>::get_id(),
        static_cast<uint32>(message_binary.size()),
        expected_error_code);
  }

  template <typename Message_type>
  void expect_write_message(
      const Message_from_str<Message_type> &message,
      const int32 error_code = 0) {
    const Message_type &message_base = message;

    expect_write_message(message_base, error_code);
  }

  template <typename Message_type>
  void expect_write_message(
      const Message_type &message,
      const int32 error_code = 0) {
    auto message_binary = Message_encoder::encode(message);

    expect_write_header(
        Client_message<Message_type>::get_id(),
        static_cast<uint32>(message_binary.size()));

    EXPECT_CALL(*m_mock_connection, write(_, message_binary.size())).
        WillOnce(Return(XError{ error_code, "" }));
  }

  template <typename Message_type_id>
  void expect_read_header(
      const Message_type_id id,
      const int32 payload_size,
      const int32 error_code = 0) {
    const int32 expected_header_size = 5;

    EXPECT_CALL(*m_mock_connection, read(_, expected_header_size))
        .WillOnce(Invoke(
            [id, payload_size, error_code](
                uchar *data,
                const std::size_t data_length MY_ATTRIBUTE((unused)))
                -> XError {
            // 1byte(type)+ payload_size-bytes(protobuf-msg-payload)
            *reinterpret_cast<int32*>(data) = 1 + payload_size;

#ifdef WORDS_BIGENDIAN
            std::swap(data[0], data[3]);
            std::swap(data[1], data[2]);
#endif

            data[4] = static_cast<uchar>(id);

            return XError{error_code, ""};
          }));
  }

  template <typename Message_type>
  void expect_read_message_without_payload(
      const Message_type &message MY_ATTRIBUTE((unused)),
      const int32 error_code = 0) {
    expect_read_header(
        Server_message<Message_type>::get_id(),
        0,
        error_code);
  }

  template <typename Message_type>
  void expect_read_message(
      const Message_from_str<Message_type> &message) {
    const Message_type &message_base = message;

    expect_read_message(message_base);
  }

  template <typename Message_type>
  void expect_read_message(const Message_type &message) {
    const std::string message_payload =
        Message_encoder::encode(message);

    expect_read_header(
        Server_message<Message_type>::get_id(),
        static_cast<int32>(message_payload.size()));

    EXPECT_CALL(*m_mock_connection,
        read(_, message_payload.size())).WillOnce(Invoke(
            [message_payload](uchar *data,
                              const std::size_t data_length
                              MY_ATTRIBUTE((unused))) -> XError {
              std::copy(message_payload.begin(),
                        message_payload.end(),
                        data);
              return {};
            }));
  }

  template <typename Message_type>
  void assert_read_message(const Message_type &message) {
    XProtocol::Server_message_type_id out_id;
    XError                out_error;

    expect_read_message(message);

    auto result = m_sut->recv_single_message(&out_id, &out_error);
    ASSERT_FALSE(out_error);
    ASSERT_TRUE(result.get());
    ASSERT_EQ(message.GetTypeName(), result->GetTypeName());
    ASSERT_EQ(Message_compare<Message_type>(message), *result);
  }

  StrictMock<Mock_connection> *m_mock_connection;
  StrictMock<Mock_factory>     m_mock_factory;
  StrictMock<Mock_handlers>    m_mock_handlers;

  std::shared_ptr<Context>       m_context {
    std::make_shared<Context>()
  };

  std::shared_ptr<Protocol_impl> m_sut;
};

template <typename T>
class Xcl_protocol_impl_tests_with_msg: public Xcl_protocol_impl_tests {
 public:
  using Msg = T;
  using Msg_ptr = std::unique_ptr<Msg>;
  using Msg_descriptor = Client_message<T>;
  using Type = Xcl_protocol_impl_tests_with_msg<T>;

 public:
  Msg_ptr m_message;
  uint32 m_packet_length { 0 };

  void setup_required_fields_in_message() {
    m_message.reset(new Msg(Msg_descriptor::make_required()));
  }

  XError assert_write_header(const uchar *data, const std::size_t data_length) {
    EXPECT_EQ(5, data_length);
    EXPECT_EQ(Msg_descriptor::get_id(), data[4]);
    std::array<uint8, 4> header = {{
        data[0],
        data[1],
        data[2],
        data[3]}};

#ifdef WORDS_BIGENDIAN
    std::swap(header[0], header[3]);
    std::swap(header[1], header[2]);
#endif

    m_packet_length = *reinterpret_cast<int32*>(header.data()) - 1;

    return {};
  }

  void assert_multiple_handlers(const Handler_result first_handler_consumed) {
    StrictMock<Mock_handlers> mock_handlers[2];

    this->setup_required_fields_in_message();

    const auto id1 = this->m_sut->add_send_message_handler(
        this->m_mock_handlers.get_mock_lambda_send_message_handler());
    const auto id2 = this->m_sut->add_send_message_handler(
        mock_handlers[0].get_mock_lambda_send_message_handler());
    const auto id3 = this->m_sut->add_send_message_handler(
        mock_handlers[1].get_mock_lambda_send_message_handler());

    {
      InSequence s;
      // Sequence of pushed handlers is important
      // Lets verify if the execution is done in sequence
      // from last pushed handler to first hander
      EXPECT_CALL(mock_handlers[1], send_message_handler(
          _,
          Msg_descriptor::get_id(),
          Ref(*this->m_message.get()))).
              WillOnce(Return(first_handler_consumed));
      EXPECT_CALL(mock_handlers[0], send_message_handler(
          _,
          Msg_descriptor::get_id(),
          Ref(*this->m_message.get())))
        .WillOnce(Return(Handler_result::Continue));
      EXPECT_CALL(this->m_mock_handlers, send_message_handler(
          _,
          Msg_descriptor::get_id(),
          Ref(*this->m_message.get())))
        .WillOnce(Return(Handler_result::Continue));

      EXPECT_CALL(*this->m_mock_connection, write(_, 5)).WillOnce(
          Invoke([this](const uchar * data, const std::size_t  size) -> XError {
                  return this->assert_write_header(data, size);
                 }));
      EXPECT_CALL(*this->m_mock_connection, write(_, _)).WillRepeatedly(
          Invoke([this](const uchar * data, const std::size_t  size) -> XError {
                  return this->assert_write_payload(data, size);
                 }));
    }

    auto error = this->m_sut->send(*this->m_message.get());

    ASSERT_FALSE(error);
    ASSERT_EQ(0, this->m_packet_length);

    this->m_sut->remove_send_message_handler(id1);
    this->m_sut->remove_send_message_handler(id2);
    this->m_sut->remove_send_message_handler(id3);
  }

  XError assert_write_payload(
      const uchar *data MY_ATTRIBUTE((unused)),
      const std::size_t data_length) {
    EXPECT_EQ(m_packet_length, data_length);
    m_packet_length -= static_cast<uint32>(data_length);

    return {};
  }
};

}  // namespace test
}  // namespace xcl
