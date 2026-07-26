#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
#===============================================================================
set(COMPONENT_NAME "ws63_liteos_app")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reset_vector.S
    ${CMAKE_CURRENT_SOURCE_DIR}/clock_init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/tcp_client_demo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_buzzer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_reader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/include/my_wifi_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/include/my_wifi_tcp.c
)

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/project/include
)

set(PRIVATE_HEADER
)

set(PRIVATE_DEFINES
)

set(PUBLIC_DEFINES
)

# use this when you want to add ccflags like -include xxx
set(COMPONENT_PUBLIC_CCFLAGS
)

set(COMPONENT_CCFLAGS
)

set(WHOLE_LINK
    true
)

set(MAIN_COMPONENT
    true
)

build_component()
