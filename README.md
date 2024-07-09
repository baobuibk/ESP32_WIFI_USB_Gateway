# ESP32 WIFI USB GATEWAY

The table below lists the default pin assignments.

When using an ESP32-S3-USB-OTG board, this example runs without any extra modifications required. Only an SD card needs to be inserted into the slot.

ESP32-S3 pin  | SD card pin | Notes
--------------|-------------|------------
GPIO36        | CLK         | 10k pullup
GPIO35        | CMD         | 10k pullup
GPIO37        | D0          | 10k pullup
GPIO38        | D1          | not used in 1-line SD mode; 10k pullup in 4-line mode
GPIO40        | D2          | not used in 1-line SD mode; 10k pullup in 4-line mode
GPIO41        | D3          | not used in 1-line SD mode, but card's D3 pin must have a 10k pullup