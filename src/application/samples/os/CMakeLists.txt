#===============================================================================
# @brief    CMake file for OSAL and RTOS samples.
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
#===============================================================================

if(DEFINED CONFIG_SAMPLE_SUPPORT_OSAL_KMALLOC)
    add_subdirectory_if_exist(memory/osal_kmalloc)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_OSAL_TASK_CONCURRENCY)
    add_subdirectory_if_exist(task/osal_task_concurrency)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_OSAL_INTERRUPT)
    add_subdirectory_if_exist(interrupt/osal_interrupt)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)
