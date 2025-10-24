# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/amelia/esp-idf/components/bootloader/subproject"
  "/home/amelia/stepper_controller/build/bootloader"
  "/home/amelia/stepper_controller/build/bootloader-prefix"
  "/home/amelia/stepper_controller/build/bootloader-prefix/tmp"
  "/home/amelia/stepper_controller/build/bootloader-prefix/src/bootloader-stamp"
  "/home/amelia/stepper_controller/build/bootloader-prefix/src"
  "/home/amelia/stepper_controller/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/amelia/stepper_controller/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/amelia/stepper_controller/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
