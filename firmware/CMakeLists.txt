cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED ENV{ESP_MATTER_PATH})
  message(FATAL_ERROR "Use the pinned development container (tools/dev.sh)")
endif()
set(PROJECT_VER "0.1.0")
set(PROJECT_VER_NUMBER 1)
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "${MATTER_SDK_PATH}/config/esp32/components" "${ESP_MATTER_PATH}/components")
# esp_insights 1.3.4 (an SDK transitive dependency) omits a CMake requirement
# for its HTTP source. Select the component explicitly without patching vendor code.
# Insights is never initialized by this application.
set(COMPONENTS main esp_http_client)
project(shelly_blu_bridge)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
