#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_SPEED_SERVER_SAMPLE)
    add_subdirectory_if_exist(ble_speed_server)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_SPEED_CLIENT_SAMPLE)
    add_subdirectory_if_exist(ble_speed_client)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_CFG_SAMPLE)
    add_subdirectory_if_exist(ble_wifi_cfg_client)
    add_subdirectory_if_exist(ble_wifi_cfg_server)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_HID_BTN_SAMPLE)
    add_subdirectory_if_exist(ble_hid_btn)
endif()

add_subdirectory_if_exist(ble_hello)

set(SOURCES "${SOURCES}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" PARENT_SCOPE)
