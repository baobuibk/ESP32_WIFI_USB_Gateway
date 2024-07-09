# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/dongkhoa/esp/esp-idf/components/bootloader/subproject"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/tmp"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/src/bootloader-stamp"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/src"
  "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/media/dongkhoa/New Volume/ESP32_WIFI_USB_Gateway/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
