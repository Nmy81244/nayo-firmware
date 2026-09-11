cmake_minimum_required(VERSION 3.22)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)

project(nayo_firmware C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${MCU_FLAGS} -O2 -Wall -Wextra -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${MCU_FLAGS} -O2 -Wall -Wextra -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/System/AT32F405xC_FLASH.ld)
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections --specs=nano.specs -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map")

add_compile_definitions(AT32F405CCT7)
add_compile_definitions(USE_STDPERIPH_DRIVER)
add_compile_options(-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

include_directories(
        Core/Inc
        Drivers/AT32F405_BSP/Inc
        Drivers/CMSIS/dsp/Inc
        Drivers/CMSIS/cm4/Inc
        Drivers/CMSIS/dsp/Inc/dsp
        Drivers/CMSIS/cm4/Inc/dsp
        Drivers/CMSIS/cm4/Src
        System
)

file(GLOB_RECURSE SOURCES
        "Core/Src/*.c"
        "Core/Src/*.cpp"
        "Drivers/AT32F405_BSP/Src/*.c"
        "System/*.s"
        "System/*.c"
)

add_executable(${PROJECT_NAME}.elf ${SOURCES})

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
        COMMAND ${CMAKE_SIZE} ${PROJECT_NAME}.elf
        COMMENT "Generating binary"
)