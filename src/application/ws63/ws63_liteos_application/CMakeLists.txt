#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
#===============================================================================
set(COMPONENT_NAME "ws63_liteos_app")

# Server and Client select different source lists, so regenerate CMake when Kconfig changes.
if(DEFINED KCONFIG_PATH)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KCONFIG_PATH}")
endif()

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/reset_vector.S
    ${CMAKE_CURRENT_SOURCE_DIR}/clock_init.c
)

if(NOT DEFINED CONFIG_CLEARCHAIN_DISPLAY_SLE_CLIENT)
    list(APPEND SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/project/tcp_client_demo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_soft_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_tca9555.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_oled.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_key.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_led.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_buzzer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_reader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/r200_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/include/my_wifi_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/project/include/my_wifi_tcp.c
    )
endif()

if(DEFINED CONFIG_CLEARCHAIN_DISPLAY_SLE_SERVER)
    list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_display_link.c)
endif()

if(DEFINED CONFIG_CLEARCHAIN_DISPLAY_SLE_CLIENT)
    list(APPEND SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_display_client.c
        ${CMAKE_CURRENT_SOURCE_DIR}/project/clearchain_display_client_entry.c)
endif()

set(PUBLIC_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/project
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

# The SDK archiver updates an existing .a without removing members that are no longer sources.
# Recreate this role-specific archive whenever its source list changes.
if(TARGET ws63_liteos_app)
    add_custom_command(TARGET ws63_liteos_app PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE:ws63_liteos_app>"
    )
endif()
