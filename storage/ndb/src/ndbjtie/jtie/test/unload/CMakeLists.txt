# Copyright (c) 2010, 2017, Oracle and/or its affiliates. All rights reserved.
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

FILE(GLOB JAVA_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/test/*.java)

SET(CLASSPATH
  ${CMAKE_BINARY_DIR}/storage/ndb/src/ndbjtie/jtie/test/unload/target/classes)

CREATE_JAR(jtie-test-unload ${JAVA_SOURCES}
  CLASSPATH ${CLASSPATH})

SET(JAR "${JAVA_NDB_VERSION}.jar")
SET(JTIE_TEST_UNLOAD_JAR "${CMAKE_CURRENT_BINARY_DIR}/jtie-test-unload-${JAR}")
GET_FILENAME_COMPONENT(PARENT ${CMAKE_CURRENT_BINARY_DIR} PATH)
SET(JTIE_MYJAPI_DIR "${PARENT}/myjapi")
SET(JTIE_MYJAPI_JAR "${JTIE_MYJAPI_DIR}/jtie-test-myjapi-${JAR}")

ADD_TEST(
  NAME test_jtie_unload
  COMMAND
  ${Java_JAVA_EXECUTABLE_PATH} ${Java_JAVA_ARCH_OPT} "-ea" "-Xcheck:jni"
  "-classpath" "${JTIE_TEST_UNLOAD_JAR}:${JTIE_MYJAPI_JAR}"
  "-Djava.library.path=${JTIE_MYJAPI_DIR}"
  "-Dcom.mysql.jtie.test.MyLoadUnloadTest.target_package_prefixes=test.,myjapi."
  "-Dcom.mysql.jtie.test.MyLoadUnloadTest.target_class_name=test.MyJapiTest"
  "-Dcom.mysql.jtie.test.MyLoadUnloadTest.target_method_name=test"
  "test.MyLoadUnloadTest"
)
