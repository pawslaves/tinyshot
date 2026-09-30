# Unit tests: the upload signature, the multipart/form-data writer and the prnt.sc reply parser.
#
# The root CMakeLists.txt adds this directory when BUILD_TESTING is on, so this file only has
# to create the targets, link them and register them with CTest.

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

enable_testing()

function(ls_add_test name)
  add_executable(${name} ${name}.cpp ${ARGN})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
  target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Test)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Upload signature bytes (md5 over the ANSI bytes of "<token>*<unixtime>").
ls_add_test(tst_signature ../src/net/signature.cpp)

# Multipart/form-data byte layout the prnt.sc uploader expects.
ls_add_test(tst_multipart ../src/net/multipart.cpp)

# <key>value</key> reply parsing of the prnt.sc uploader.
ls_add_test(tst_tagresponse ../src/net/tagresponse.cpp)
