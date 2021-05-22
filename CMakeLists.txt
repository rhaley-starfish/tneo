cmake_minimum_required(VERSION 3.19)
project(tneo C ASM)

LIST(APPEND TNEO_SRC
    src/core/tn_timer.c
    src/core/tn_timer_dyn.c
    src/core/tn_sys.c
    src/core/tn_timer_static.c
    src/core/tn_mutex.c
    src/core/tn_list.c
    src/core/tn_eventgrp.c
    src/core/tn_dqueue.c
    src/core/tn_fmem.c
    src/core/tn_sem.c
    src/core/tn_tasks.c
    src/tn_app_check.c
    src/arch/cortex_m/tn_arch_cortex_m_c.c
    src/arch/cortex_m/tn_arch_cortex_m.S
    )

INCLUDE_DIRECTORIES(
    ../inc
    src
    src/arch/
    src/core/
    src/core/internal
)

add_library(${PROJECT_NAME} ${TNEO_SRC})

target_compile_definitions(${PROJECT_NAME} PUBLIC
        __ARM_ARCH_7EM__
        STM32F446xx
        STM32F446RETx
        STM32F4
        USE_FULL_ASSERT
        USE_STDPERIPH_DRIVER
        HSE_VALUE=20000000
        )


target_compile_options(${PROJECT_NAME} PUBLIC
        -mcpu=cortex-m4
        -mthumb
        -mfloat-abi=hard
        -mfpu=fpv4-sp-d16
        -fdata-sections
        -ffunction-sections
        -Wall
        )

target_link_options(${PROJECT_NAME} PUBLIC
        -T../src/STM32F446RETx_FLASH.ld
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -Wl,-Map=${PROJECT_NAME}.map,--cref
        -Wl,--gc-sections
        )