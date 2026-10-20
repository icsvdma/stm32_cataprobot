cmake_minimum_required(VERSION 3.22)
project(cataprobot_host_tests LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()
add_executable(test_motor_safety
    test_motor_safety.c
    ../cubeMX_stm32g473_test/Core/Src/motor_safety.c)
target_include_directories(test_motor_safety PRIVATE ../cubeMX_stm32g473_test/Core/Inc)
if(MSVC)
    target_compile_options(test_motor_safety PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(test_motor_safety PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME motor_safety COMMAND test_motor_safety)
add_executable(test_motor_protocol test_motor_protocol.c
    ../cubeMX_stm32g473_test/Core/Src/motor_safety.c
    ../cubeMX_stm32g473_test/Core/Src/motor_protocol.c
    ../cubeMX_stm32g473_test/Core/Src/packet.c)
target_include_directories(test_motor_protocol PRIVATE ../cubeMX_stm32g473_test/Core/Inc)
if(MSVC)
    target_compile_options(test_motor_protocol PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(test_motor_protocol PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME motor_protocol COMMAND test_motor_protocol)
