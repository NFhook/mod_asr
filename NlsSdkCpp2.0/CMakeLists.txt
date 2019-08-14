cmake_minimum_required(VERSION 2.6)
project(speech-demo)

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

include_directories(${CMAKE_SOURCE_DIR}/include)

link_directories(${CMAKE_SOURCE_DIR}/lib/linux)

#如果使用libnlsCppSdk.a编译报缺少gzclose缺失，请在CMakeLists.txt，或编译命令中加-lrt -lz

add_executable(srDemo demo/speechRecognizerDemo.cpp)
target_link_libraries(srDemo nlsCppSdk nlsCommonSdk opus curl ssl crypto uuid pthread)

add_executable(srSyncDemo demo/speechRecognizerSyncDemo.cpp)
target_link_libraries(srSyncDemo nlsCppSdk nlsCommonSdk opus curl ssl crypto uuid pthread)

add_executable(syDemo demo/speechSynthesizerDemo.cpp)
target_link_libraries(syDemo nlsCppSdk nlsCommonSdk opus curl ssl crypto uuid pthread)

add_executable(stDemo demo/speechTranscriberDemo.cpp)
target_link_libraries(stDemo nlsCppSdk nlsCommonSdk opus curl ssl crypto uuid pthread)

add_executable(stSyncDemo demo/speechTranscriberSyncDemo.cpp)
target_link_libraries(stSyncDemo nlsCppSdk nlsCommonSdk opus curl ssl crypto uuid pthread)

SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/demo) 

