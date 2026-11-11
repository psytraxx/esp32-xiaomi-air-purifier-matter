cmake_minimum_required(VERSION 3.16.0)

# Default config; a variant adds an overlay on the command line, e.g. the WPA3
# build: -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wpa3"
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(air_purifier_matter)
