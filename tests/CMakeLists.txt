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
add_executable(test_external_sensors test_external_sensors.c
    ../cubeMX_stm32g473_test/Core/Src/external_sensors.c)
target_include_directories(test_external_sensors PRIVATE fakes ../cubeMX_stm32g473_test/Core/Inc)
if(MSVC)
    # Existing uint8_t bit-mask complements intentionally truncate to 8 bits.
    target_compile_options(test_external_sensors PRIVATE /W4 /WX /utf-8 /wd4310)
else()
    target_compile_options(test_external_sensors PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME external_sensors COMMAND test_external_sensors)
add_executable(test_spi_frame test_spi_frame.c
    ../cubeMX_stm32g473_test/Core/Src/spi_frame.c
    ../cubeMX_stm32g473_test/Core/Src/packet.c)
target_include_directories(test_spi_frame PRIVATE ../cubeMX_stm32g473_test/Core/Inc)
if(MSVC)
    target_compile_options(test_spi_frame PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(test_spi_frame PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME spi_frame COMMAND test_spi_frame)
