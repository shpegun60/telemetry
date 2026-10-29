# Standalone internal-Flash NUCLEO-H7S3L8 image; no RTOS/Qt/vendor copy. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryNucleoH7S3 LANGUAGES C CXX ASM)
foreach(required COBS_ROOT STM32_CUBE_ROOT)
  file(TO_CMAKE_PATH "${${required}}" normalized)
  set(${required} "${normalized}")
  if(NOT IS_DIRECTORY "${${required}}")
    message(FATAL_ERROR "${required} must name an external checkout/SDK")
  endif()
endforeach()
get_filename_component(TELEMETRY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(HAL "${STM32_CUBE_ROOT}/Drivers/STM32H7RSxx_HAL_Driver")
set(CMSIS "${STM32_CUBE_ROOT}/Drivers/CMSIS")
set(DEVICE "${CMSIS}/Device/ST/STM32H7RSxx")
set(HAL_NAMES cortex dma dma_ex exti flash flash_ex gpio pwr pwr_ex rcc rcc_ex uart uart_ex)
set(HAL_SOURCES "${HAL}/Src/stm32h7rsxx_hal.c")
foreach(name IN LISTS HAL_NAMES)
  list(APPEND HAL_SOURCES "${HAL}/Src/stm32h7rsxx_hal_${name}.c")
endforeach()
add_executable(telemetry-nucleo-h7s3 main.cpp board.c runtime.cpp ${HAL_SOURCES}
  "${DEVICE}/Source/Templates/system_stm32h7rsxx.c"
  "${DEVICE}/Source/Templates/gcc/startup_stm32h7s3xx.s"
  "${COBS_ROOT}/src/cobs/Decoder.cpp"
  "${COBS_ROOT}/src/cobs/Encoder.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
  "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
  "${TELEMETRY_ROOT}/lib/integration/encoded/Dispatcher.cpp")
set_target_properties(telemetry-nucleo-h7s3 PROPERTIES SUFFIX ".elf" C_STANDARD 11 CXX_STANDARD 20)
target_compile_definitions(telemetry-nucleo-h7s3 PRIVATE USE_HAL_DRIVER STM32H7S3xx
  TINY_DELEGATE_ENABLE_HEAP_FALLBACK=0 UART_ENGINE_MAX_INSTANCES=1)
target_include_directories(telemetry-nucleo-h7s3 PRIVATE "${CMAKE_CURRENT_LIST_DIR}"
  "${TELEMETRY_ROOT}/lib" "${TELEMETRY_ROOT}/lib/boost_pfr/include"
  "${TELEMETRY_ROOT}/lib/magic_enum" "${COBS_ROOT}/src"
  "${TELEMETRY_ROOT}/lib/delegate" "${COBS_ROOT}/libs/spsc" "${COBS_ROOT}/libs/spsc/src")
target_include_directories(telemetry-nucleo-h7s3 SYSTEM PRIVATE "${HAL}/Inc"
  "${HAL}/Inc/Legacy" "${CMSIS}/Include" "${DEVICE}/Include")
set(MCU -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard)
target_compile_options(telemetry-nucleo-h7s3 PRIVATE ${MCU} -Os -flto -g3
  -ffunction-sections -fdata-sections -fstack-usage -Wall -Wextra -Werror
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-threadsafe-statics>")
target_link_options(telemetry-nucleo-h7s3 PRIVATE ${MCU} -flto -Os -static -nostartfiles -fstack-usage -save-temps=obj
  --specs=nano.specs --specs=nosys.specs "-T${CMAKE_CURRENT_LIST_DIR}/flash.ld"
  "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/telemetry-nucleo-h7s3.map"
  -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--fatal-warnings)
set_property(TARGET telemetry-nucleo-h7s3 APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/flash.ld")
