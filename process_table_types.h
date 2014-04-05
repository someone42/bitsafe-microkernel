#define MAX_IRQ_HANDLERS        12

typedef struct MemoryRegionStruct
{
    uint32_t rbar;
    uint32_t rasr;
} MemoryRegion;

typedef struct ProcessStruct
{
    char *name;
    uint8_t num_regions;
    MemoryRegion regions[8];
    uint8_t irq_handlers[MAX_IRQ_HANDLERS];
    uint8_t priority;
    uint32_t initial_sp;
    uint32_t initial_pc;
} Process;
