# EEE316 Term Project - Source Archive

## Project Manifest
* **Project ID:** P18
* **Project Title:** AMG8833 Thermal Camera with False-Colour Display
* **Team:** Group 3
* **Hardware Platform:** STM32F407G-DISC1
* **Submission Date:** 12/05/2026

## Toolchain & Reproducibility
To reproduce our build, the following toolchains were used:
* **Compiler:** arm-none-eabi-gcc 13.2.Rel1
* **Build System:** Make
* **Flasher/Debugger:** OpenOCD 0.12.0

### Build Instructions
Navigate to the `01_firmware/` directory and run the following command to compile the bare-metal C code:
`make`

To flash the compiled firmware to the STM32F407 Discovery board, run:
`make flash`

## Folder Structure Overview
* `01_firmware/`: Contains all bare-metal C source code (`main.c`), the `Makefile`, and firmware files.
* `02_schematics/`: Full circuit schematic (Proteus) and system block diagram (PDF).
* `03_pcb_or_breadboard/`: Photographs of the actual hardware setup and breadboard connections.
* `04_simulations/`: Proteus simulation files and artificial thermal frame data used during initial verification.
* `05_test_data/`: TeraTerm CSV logs containing raw 8x8 temperature data used to generate the MATLAB/Python heatmaps.
* `06_bom/`: Bill of Materials (BOM) listing components and TRY prices.
* `07_datasheets/`: Reference datasheets for the AMG8833, ST7735 TFT, and STM32F407 MCU.
* `08_extras/`: MATLAB and Python visualization scripts used for data analysis and enhanced heatmaps.