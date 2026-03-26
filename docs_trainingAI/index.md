# docs_trainingAI — Hardware Reference Index

Thư mục này chứa tài liệu kỹ thuật phần cứng phục vụ AI-assisted development cho VinixOS.
Mục đích: cung cấp context chính xác về AM335x SoC và kiến trúc ARM khi làm việc với drivers, BSP, và assembly code.

---

## AM335x Technical Reference Manual

Tài liệu gốc: *AM335x ARM Cortex-A8 Microprocessors Technical Reference Manual* (Texas Instruments).
Target: BeagleBone Black — SoC TI AM3358, CPU ARMv7-A Cortex-A8.

| Chapter | Nội dung |
| ------- | -------- |
| [Ch.02 — Memory Map](am335x/Chapter_02_Memory_Map.md) | Địa chỉ vật lý toàn bộ peripheral, SRAM, DDR, boot ROM |
| [Ch.03 — ARM MPU Subsystem](am335x/Chapter_03_ARM_MPU_Subsystem.md) | Cortex-A8 core, cache, MMU, memory protection |
| [Ch.04 — PRU-ICSS](am335x/Chapter_04_Programmable_Real-Time_Unit_Subsystem_and_Industrial_Communication_Subsystem_PRU-ICSS.md) | Programmable Real-Time Unit, industrial communication |
| [Ch.05 — Graphics Accelerator SGX](am335x/Chapter_05_Graphics_Accelerator_SGX.md) | PowerVR SGX530 GPU subsystem |
| [Ch.06 — Interrupts](am335x/Chapter_06_Interrupts.md) | INTC: 128 IRQ sources, routing, priority, MIR registers |
| [Ch.07 — Memory Subsystem](am335x/Chapter_07_Memory_Subsystem.md) | EMIF, DDR3 controller, GPMC, ECC |
| [Ch.08 — PRCM](am335x/Chapter_08_Power_Reset_and_Clock_Management_PRCM.md) | Clock domains, CM_PER/CM_WKUP registers, reset management |
| [Ch.09 — Control Module](am335x/Chapter_09_Control_Module.md) | Pin mux (CONF_*), device ID, SysConfig registers |
| [Ch.10 — Interconnects](am335x/Chapter_10_Interconnects.md) | L3/L4 bus topology, bandwidth, error handling |
| [Ch.11 — EDMA](am335x/Chapter_11_Enhanced_Direct_Memory_Access_EDMA.md) | Enhanced DMA: channels, PaRAM, event triggers |
| [Ch.12 — Touchscreen Controller](am335x/Chapter_12_Touchscreen_Controller.md) | ADC, touchscreen, general-purpose ADC |
| [Ch.13 — LCD Controller](am335x/Chapter_13_LCD_Controller.md) | LCDC raster mode, DMA, pixel clock, timing registers |
| [Ch.14 — Ethernet Subsystem](am335x/Chapter_14_Ethernet_Subsystem.md) | CPSW switch, MDIO, MII/RGMII |
| [Ch.15 — PWMSS](am335x/Chapter_15_Pulse-Width_Modulation_Subsystem_PWMSS.md) | PWM subsystem: eCAP, eQEP, ePWM |
| [Ch.16 — USB](am335x/Chapter_16_Universal_Serial_Bus_USB.md) | USB0/USB1 controllers, OTG, CPPI DMA |
| [Ch.17 — Interprocessor Communication](am335x/Chapter_17_Interprocessor_Communication.md) | Mailbox, spinlock giữa ARM và PRU |
| [Ch.18 — MMC](am335x/Chapter_18_Multimedia_Card_MMC.md) | MMC/SD/SDIO controller — dùng để load kernel từ SD card |
| [Ch.19 — UART](am335x/Chapter_19_Universal_Asynchronous_ReceiverTransmitter_UART.md) | UART registers, FIFO, interrupt, baud rate — UART0 @ 0x44E09000 |
| [Ch.20 — Timers](am335x/Chapter_20_Timers.md) | DMTimer0–7, WDT, 32kHz timer — DMTimer2 dùng cho scheduler tick |
| [Ch.21 — I2C](am335x/Chapter_21_I2C.md) | I2C0/I2C1/I2C2 registers, FIFO, interrupt — I2C0 dùng cho TDA19988 |
| [Ch.22 — McASP](am335x/Chapter_22_Multichannel_Audio_Serial_Port_McASP.md) | Multichannel Audio Serial Port |
| [Ch.23 — CAN](am335x/Chapter_23_Controller_Area_Network_CAN.md) | DCAN0/DCAN1 — Controller Area Network |
| [Ch.24 — McSPI](am335x/Chapter_24_Multichannel_Serial_Port_Interface_McSPI.md) | SPI controller, master/slave, FIFO |
| [Ch.25 — GPIO](am335x/Chapter_25_General-Purpose_InputOutput.md) | GPIO0–3 registers, direction, interrupt, debounce |
| [Ch.26 — Initialization](am335x/Chapter_26_Initialization.md) | Boot sequence, BootROM, booting từ SD/UART/USB |
| [Ch.27 — Debug Subsystem](am335x/Chapter_27_Debug_Subsystem.md) | JTAG, ETM, CoreSight debug infrastructure |

---

## ARM Architecture — Assembly Programming

Tài liệu gốc: *ARM Assembly Language: Fundamentals and Techniques* (William Hohl & Christopher Hinds).
Target: ARMv7-A instruction set, assembler GNU as, calling convention AAPCS.

| Chapter | Nội dung |
| ------- | -------- |
| [Ch.01 — Overview of Computing Systems](arm-arch/01_An_Overview_of_Computing_Systems.md) | Von Neumann model, memory hierarchy, instruction cycle |
| [Ch.02 — The Programmer's Model](arm-arch/02_The_Programmers_Model.md) | ARM registers (r0–r15), CPSR, processor modes, pipeline |
| [Ch.03 — Introduction to Instruction Sets](arm-arch/03_Introduction_to_Instruction_Sets_v4T_and_v7-M.md) | ARMv4T vs v7-M, encoding, condition codes |
| [Ch.04 — Assembler Rules and Directives](arm-arch/04_Assembler_Rules_and_Directives.md) | GNU as syntax, `.section`, `.global`, `.equ`, `.align` |
| [Ch.05 — Loads, Stores, and Addressing](arm-arch/05_Loads_Stores_and_Addressing.md) | LDR/STR, addressing modes: pre/post-index, offset |
| [Ch.06 — Constants and Literal Pools](arm-arch/06_Constants_and_Literal_Pools.md) | MOV immediate, LDR =const, literal pool placement |
| [Ch.07 — Integer Logic and Arithmetic](arm-arch/07_Integer_Logic_and_Arithmetic.md) | ADD/SUB/MUL, logical ops, shifts, flags |
| [Ch.08 — Branches and Loops](arm-arch/08_Branches_and_Loops.md) | B/BL/BX, condition codes, loop patterns |
| [Ch.09 — Floating-Point Basics](arm-arch/09_Introduction_to_Floating-Point_Basics_Data_Types_a.md) | VFP/NEON data types, IEEE 754, FPU registers |
| [Ch.10 — Floating-Point Rounding and Exceptions](arm-arch/10_Introduction_to_Floating-Point_Rounding_and_Except.md) | Rounding modes, FP exceptions, FPSCR |
| [Ch.11 — Floating-Point Data-Processing Instructions](arm-arch/11_Floating-Point_Data-Processing_Instructions.md) | VADD/VSUB/VMUL, VCMP, VCVT |
| [Ch.12 — Tables](arm-arch/12_Tables.md) | Lookup tables, TBB/TBH, jump tables |
| [Ch.13 — Subroutines and Stacks](arm-arch/13_Subroutines_and_Stacks.md) | AAPCS calling convention, stack frame, BL/BX LR, STMFD/LDMFD |
| [Ch.14 — ARM7TDMI Exception Handling](arm-arch/14_ARM7TDMI_Exception_Handling.md) | Vector table, exception modes, LR adjust, SPSR |
| [Ch.15 — v7-M Exception Handling](arm-arch/15_v7-M_Exception_Handling.md) | Cortex-M NVIC, exception entry/exit, stack frame |
| [Ch.16 — Memory-Mapped Peripherals](arm-arch/16_Memory-Mapped_Peripherals.md) | MMIO access patterns, volatile, barriers (DSB/DMB) |
| [Ch.17 — ARM Thumb and Thumb-2 Instructions](arm-arch/17_ARM_Thumb_and_Thumb-2_Instructions.md) | Thumb-2 encoding, interworking, BLX |
| [Ch.18 — Mixing C and Assembly](arm-arch/18_Mixing_C_and_Assembly.md) | `extern` linkage, inline asm, register constraints, ABI boundary |

---
