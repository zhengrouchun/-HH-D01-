#===============================================================================
# @brief    custom application sample
#===============================================================================
set(SOURCES
    "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tcp_client_demo.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/clearchain_http.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rc522_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rc522_ws63_gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/my_wifi_api.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/my_wifi_tcp.c"
    PARENT_SCOPE
)

set(PRIVATE_HEADER
    "${PRIVATE_HEADER}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PARENT_SCOPE
)
