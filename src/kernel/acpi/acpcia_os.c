#include <main.h>
#include "acpi.h"
#include <accommon.h>
#include <math.h>
#include <kprint.h>
#include <io.h>

#include <kernel/memory/pmm.h>
#include <kernel/memory/paging.h>
#include <kernel/memory/kheap.h>
#include <kernel/tasking.h>

#include <kernel/interrupts/interrupts.h>

#ifdef X86_64
#define ACPI_TEMP_ADDRESS   0xFFFFFF00FEF00000
#define ACPI_FIRST_ADDRESS  0xFFFFFF00FEF01000
#define ACPI_LAST_ADDRESS   0xFFFFFF00FEFFF000
#else
#define ACPI_TEMP_ADDRESS   0xFEF00000
#define ACPI_FIRST_ADDRESS  0xFEF01000
#define ACPI_LAST_ADDRESS   0xFEFFF000
#endif


// ACPI RDSP signature.
#define ACPI_RSDP_PATTERN1  "RSD "
#define ACPI_RSDP_PATTERN2  "PTR "

ACPI_STATUS AcpiOsInitialize() {
    return (AE_OK);
}

ACPI_STATUS AcpiOsTerminate() {
    return (AE_OK);
}

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
    // Search the BIOS area of low memory for the RSDP.
    for (uintptr_t i = (memInfo.kernelVirtualOffset + 0x000E0000); i < (memInfo.kernelVirtualOffset + 0x000FFFFF); i += 16) {
        uint32_t *block = (uint32_t*)i;
        if ((memcmp(block, ACPI_RSDP_PATTERN1, 4) == 0) && (memcmp(block + 1, ACPI_RSDP_PATTERN2, 4) == 0)) {
            // Verify checksum matches.
            uint32_t checksumResult = 0;
            uint8_t *rsdpBytes = (uint8_t*)block;
            for (uint8_t i = 0; i < 20; i++)
                checksumResult += rsdpBytes[i];

            // If the low byte isn't zero, RSDP is bad.
            if ((uint8_t)checksumResult != 0)
                continue;

            // Return pointer.
            return ((ACPI_PHYSICAL_ADDRESS)block) - memInfo.kernelVirtualOffset;
        }      
    }

    // RSDP couldn't be discovered.
    return NULL;
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *PredefinedObject, ACPI_STRING *NewValue) {
    *NewValue = NULL;
    return (AE_OK);
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *ExistingTable, ACPI_TABLE_HEADER **NewTable) {
    *NewTable = NULL;
    return (AE_OK);
}

ACPI_STATUS AcpiOsPhysicalTableOverride ( ACPI_TABLE_HEADER *ExistingTable, ACPI_PHYSICAL_ADDRESS *NewAddress, UINT32 *NewTableLength) {
    *NewAddress = NULL;
    return (AE_OK);
}

void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length) {
    uint32_t pageCount = DIVIDE_ROUND_UP(MASK_PAGEFLAGS_4K(PhysicalAddress) + Length, PAGE_SIZE_4K);

    // Get next available virtual range.
    uintptr_t page = ACPI_FIRST_ADDRESS;
    uint32_t currentCount = 0;
    uint64_t phys;
    while (currentCount < pageCount) {     
        // Check if page is free.
        if (!paging_get_phys(page, &phys))
            currentCount++;
        else {
            // Move to next page.
            page += PAGE_SIZE_4K;
            if (page > ACPI_LAST_ADDRESS)
                panic("ACPI: Out of virtual addresses!\n");

            // Start back at zero.
            currentCount = 0;
        }
    }

    // Check if last page is outside bounds.
    if (page + ((pageCount - 1) * PAGE_SIZE_4K) > ACPI_LAST_ADDRESS)
        panic("ACPI: Out of virtual addresses!\n");

    // Map range.
    paging_map_region_phys(page, page + ((pageCount - 1) * PAGE_SIZE_4K), MASK_PAGE_4K(PhysicalAddress), true, true);

    // Return address.
    return page + MASK_PAGEFLAGS_4K(PhysicalAddress);
}

void AcpiOsUnmapMemory(void *where, ACPI_SIZE length) {
    // Get page count and unmap table.
    uint32_t pageCount = DIVIDE_ROUND_UP(MASK_PAGEFLAGS_4K((uintptr_t)where) + length, PAGE_SIZE_4K);
    page_t startPage = MASK_PAGE_4K((uintptr_t)where);
    paging_unmap_region_phys(startPage, startPage + ((pageCount - 1) * PAGE_SIZE_4K));
}

ACPI_STATUS AcpiOsGetPhysicalAddress(void *LogicalAddress, ACPI_PHYSICAL_ADDRESS *PhysicalAddress) {

}


void *AcpiOsAllocate(ACPI_SIZE Size) {
    // Allocate from kernel heap.
    return kheap_alloc(Size);
}

void AcpiOsFree(void *Memory) {
    // Free space in heap.
    kheap_free(Memory);
}

BOOLEAN AcpiOsReadable(void *Memory, ACPI_SIZE Length) {

}

BOOLEAN AcpiOsWritable(void *Memory, ACPI_SIZE Length) {

}

ACPI_THREAD_ID AcpiOsGetThreadId() {
    return 1;
}

ACPI_OSD_EXEC_CALLBACK functionA;
void *ContextA;
void acpica_thread() {
    //kprintf_nlock("ACPI: subtread.\n");
    functionA(ContextA);
    _kill();
    while(true);
}

ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void *Context) {
    //kprintf_nlock("ACPI: execute type: 0x%X\n", Type);
    
    functionA = Function;
    ContextA = Context;
    tasking_add_process(tasking_create_process("acpica", (uintptr_t)acpica_thread));
    return (AE_OK);
}

void AcpiOsSleep(UINT64 Milliseconds) {
    sleep(Milliseconds);
}

void AcpiOsStall(UINT32 Microseconds) {
    sleep(Microseconds / 1000);
}

void AcpiOsWaitEventsComplete ( void) {

}

/*ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX *OutHandle) {

}

void AcpiOsDeleteMutex(ACPI_MUTEX Handle) {

}

ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout) {

}

void AcpiOsReleaseMutex(ACPI_MUTEX Handle) {

}*/

ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE *OutHandle) {
    *OutHandle = kheap_alloc(sizeof(ACPI_SEMAPHORE));
    return (AE_OK);
}

ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle) {
    kheap_free(Handle);
    return (AE_OK);
}

ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout) {
    return (AE_OK);
}

ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units) {
return (AE_OK);
}

ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle) {
return (AE_OK);
}

void AcpiOsDeleteLock(ACPI_HANDLE Handle) {
return (AE_OK);
}

ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle) {
return (AE_OK);
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) {
return (AE_OK);
}

static void *context;
static ACPI_OSD_HANDLER handler;
static void acpi_callback(registers_t *regs) {
    kprintf_nlock("ACPI: SCI triggered!\n");
    handler(context);
}

ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptLevel, ACPI_OSD_HANDLER Handler, void *Context) {
    interrupts_irq_install_handler(InterruptLevel, acpi_callback);
    handler = Handler;
    context = Context;
    return (AE_OK);
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER Handler) {
    interrupts_irq_remove_handler(InterruptNumber);
    return (AE_OK);
}

ACPI_STATUS AcpiOsReadMemory ( ACPI_PHYSICAL_ADDRESS Address, UINT64 *Value, UINT32 Width) {
    kprintf("ACPI: read memory\n");
    return;
}

ACPI_STATUS AcpiOsWriteMemory ( ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width)  {
    kprintf("ACPI: write memory\n");
    return;
}

ACPI_STATUS AcpiOsReadPort ( ACPI_IO_ADDRESS Address, UINT32 *Value, UINT32 Width) {
    switch (Width) {
        case 8:
            *Value = inb(Address);
            return (AE_OK);

        case 16:
            *Value = inw(Address);
            return (AE_OK);

        case 32:
            *Value = inl(Address);
            return (AE_OK);
    }
    return (AE_ERROR);
}

ACPI_STATUS AcpiOsWritePort ( ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width) {
    switch (Width) {
        case 8:
            outb(Address, (uint8_t)(Value & 0xFF));
            return (AE_OK);

        case 16:
            outw(Address, (uint16_t)(Value & 0xFFFF));
            return (AE_OK);

        case 32:
            outl(Address, Value);
            return (AE_OK);
    }
    return (AE_ERROR);
}

ACPI_STATUS
AcpiOsReadPciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  *Value,
    UINT32                  Width) {
        kprintf("ACPI: Attempted to read PCI.\n");
    }

ACPI_STATUS
AcpiOsWritePciConfiguration (
    ACPI_PCI_ID             *PciId,
    UINT32                  Reg,
    UINT64                  Value,
    UINT32                  Width) {
kprintf("ACPI: Attempted to write PCI.\n");
    }

void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf ( const char *Format, ...) {
    // Get args.
    va_list args;
	va_start(args, Format);
    kprintf_va(Format, args);
}

void AcpiOsVprintf ( const char *Format, va_list Args) {
    kprintf_va(Format, Args);
}

UINT64 AcpiOsGetTimer ( void) {
    return pit_ticks() * 10;
}

ACPI_STATUS AcpiOsSignal ( UINT32 Function,  void *Info) {
    panic("ACPI: signal\n");
}

ACPI_STATUS
AcpiOsEnterSleep (
    UINT8                   SleepState,
    UINT32                  RegaValue,
    UINT32                  RegbValue) {
        return (AE_OK);
    }