# Copyright (c) 2013, 2017, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


IF(NOT GMOCK_FOUND)
  RETURN()
ENDIF()

INCLUDE_DIRECTORIES(${BOOST_PATCHES_DIR} ${BOOST_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${SSL_INCLUDE_DIRS})

IF(SSL_DEFINES)
  ADD_DEFINITIONS(${SSL_DEFINES})
ENDIF()


INCLUDE_DIRECTORIES(
  ${GMOCK_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/plugin/keyring
  ${CMAKE_SOURCE_DIR}/plugin/keyring/common
  ${CMAKE_SOURCE_DIR}/unittest/gunit
)

SET(TESTS
  keyring-api
  keys_container
  buffered_file_io
  file_io
)

SET(ALL_KEYRING_TESTS)
FOREACH(test ${TESTS})
  LIST(APPEND ALL_KEYRING_TESTS ${test}-t.cc)
ENDFOREACH()


SET(SRC_FILES)
LIST(APPEND SRC_FILES
  ${CMAKE_SOURCE_DIR}/plugin/keyring/common/keyring_key.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/common/keys_container.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/buffered_file_io.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/buffer.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/hash_to_buffer_serializer.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/digest.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/file_io.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/checker/checker.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/checker/checker_factory.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/checker/checker_ver_1_0.cc
  ${CMAKE_SOURCE_DIR}/plugin/keyring/checker/checker_ver_2_0.cc
  ${CMAKE_SOURCE_DIR}/unittest/gunit/keyring/buffered_file_io_10.cc
  ${CMAKE_SOURCE_DIR}/unittest/gunit/keyring/psi_memory_key.cc
  )

IF(WIN32)
  LIST(APPEND SRC_FILES ../../../sql/nt_servc.cc)
ENDIF()
ADD_LIBRARY(keyring_test STATIC ${SRC_FILES})

IF (MERGE_UNITTESTS)
  MYSQL_ADD_EXECUTABLE(merge_keyring_file_tests-t
    ${ALL_KEYRING_TESTS} ADD_TEST merge_keyring_file_tests)
  TARGET_LINK_LIBRARIES(merge_keyring_file_tests-t keyring_test)

  TARGET_LINK_LIBRARIES(merge_keyring_file_tests-t
    gunit_large strings dbug regex mysys sql_main sql_gis)
  TARGET_LINK_LIBRARIES(merge_keyring_file_tests-t
    sql_main binlog rpl master slave sql_gis sql_main)

  IF(WITH_PERFSCHEMA_STORAGE_ENGINE)
    TARGET_LINK_LIBRARIES(merge_keyring_file_tests-t perfschema)
  ENDIF()

  ADD_DEPENDENCIES(merge_keyring_file_tests-t GenError)

ENDIF(MERGE_UNITTESTS)

FOREACH(test ${TESTS})
  MYSQL_ADD_EXECUTABLE(${test}-t ${test}-t.cc SKIP_INSTALL ${EXCLUDE_FROM_ALL})
  TARGET_LINK_LIBRARIES(${test}-t keyring_test)

  TARGET_LINK_LIBRARIES(${test}-t
    gunit_large strings dbug regex mysys sql_main sql_gis)
  TARGET_LINK_LIBRARIES(${test}-t
    sql_main binlog rpl master slave sql_main sql_gis)

  IF(WITH_PERFSCHEMA_STORAGE_ENGINE)
    TARGET_LINK_LIBRARIES(${test}-t perfschema)
    TARGET_LINK_LIBRARIES(${test}-t perfschema)
  ENDIF()

  IF(NOT MERGE_UNITTESTS)
    ADD_TEST(${test} ${test}-t)
  ENDIF()
ENDFOREACH()

