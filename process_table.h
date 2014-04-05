// process_table.h
// Someday, this will be auto-generated

#include "process_table_types.h"
#include "process_table_num.h"

static const Process processes[NUM_PROCESSES] =
{
    {"Demo process", // name
    7, // number of regions
    {
        // Regions:
        {0x20000010, 0x13060029}, // SRAM1 and SRAM2
        {0x08000011, 0x07020029}, // Flash
        {0x40000012, 0x13050029}, // APB1, APB2 and AHB1 peripherals
        {0x50000013, 0x13050029}, // AHB2 peripherals
        {0x10000014, 0x13060029}, // CCM (core-coupled memory)
        {0x22000015, 0x13060031}, // SRAM1 and SRAM2 bit-banding
        {0x42000016, 0x13050031} // APB1, APB2 and AHB1 peripherals bit-banding
    },
    {0}, // no allowed interrupts
    1 // base priority
    }
};
