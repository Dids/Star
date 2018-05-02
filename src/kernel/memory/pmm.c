#include <main.h>
#include <kprint.h>
#include <string.h>

#include <kernel/memory/pmm.h>
#include <kernel/memory/paging.h>

// Constants determined by linker and early boot.
extern uint32_t MULTIBOOT_INFO;
extern uintptr_t KERNEL_VIRTUAL_OFFSET;
extern uintptr_t KERNEL_VIRTUAL_START;
extern uintptr_t KERNEL_VIRTUAL_END;

extern uint32_t DMA_FRAMES_FIRST;
extern uint32_t DMA_FRAMES_LAST;
extern uint32_t PAGE_FRAME_STACK_START;
extern uint32_t PAGE_FRAME_STACK_END;
extern uint32_t EARLY_PAGES_LAST;

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
// PAE constants.
extern uint32_t PAGE_FRAME_STACK_PAE_START;
extern uint32_t PAGE_FRAME_STACK_PAE_END;
extern bool PAE_ENABLED;
#endif

// Used to store info about memory in the system.
mem_info_t memInfo;
uint32_t earlyPagesLast;

// DMA bitmap. Each bit represents a 64KB page, in order.
static bool dmaFrames[PMM_NO_OF_DMA_FRAMES];

// Page frame stack, stores addresses to 4K page frames in physical memory.
static uintptr_t *pageFrameStack;
static uint32_t pageFramesAvailable = 0;

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
// Page frame stack, stores addresses to 4K page frames in physical memory. This stack contains only PAE pages.
static uint64_t *pageFrameStackPae;
static uint32_t pageFramesPaeAvailable = 0;
#endif

/**
 * 
 * DMA MEMORY FUNCTIONS
 * 
 */

/**
 * Gets a free DMA frame.
 * @param frameOut Pointer to where the frame address should be stored.
 * @return True if the function succeeded; otherwise false.
 */
bool pmm_dma_get_free_frame(uintptr_t *frameOut) {
    // Search until we find a free page frame.
    for (uint32_t frame = 0; frame < PMM_NO_OF_DMA_FRAMES; frame++) {
        if (dmaFrames[frame]) {
            // We found a frame.
            *frameOut = memInfo.dmaPageFrameFirst + (frame * PAGE_SIZE_64K);
            dmaFrames[frame] = false;
            return true;
        }
    }

    // No frame found.
    return false;
}

void pmm_dma_set_frame(uintptr_t frame, bool status) {
    // Ensure we are in bounds and aligned.
    if (frame < memInfo.dmaPageFrameFirst || frame > memInfo.dmaPageFrameLast || (frame % PAGE_SIZE_64K != 0))
        panic("PMM: Invalid DMA frame 0x%p specified!\n", frame);

    // Change status of frame.
    dmaFrames[(frame - memInfo.dmaPageFrameFirst) / PAGE_SIZE_64K] = status;
}

uintptr_t pmm_dma_get_phys(uintptr_t frame) {
    // If specified frame is 0, just return 0.
    if (!frame)
        return 0;
    return frame - memInfo.kernelVirtualOffset;
}

uintptr_t pmm_dma_get_virtual(uintptr_t frame) {
    // If specified frame is 0, just return 0.
    if (!frame)
        return 0;
    return frame + memInfo.kernelVirtualOffset;
}

/**
 * Builds the DMA bitmap.
 */
static void pmm_dma_build_bitmap() {
    // Zero out frames and set frame available.
    for (uint32_t frame = 0; frame < PMM_NO_OF_DMA_FRAMES; frame++) {
        memset((void*)((uintptr_t)(memInfo.dmaPageFrameFirst + (frame * PAGE_SIZE_64K))), 0, PAGE_SIZE_64K);
        dmaFrames[frame] = true;
    }

    // Test out frame functions.
    kprintf("PMM: Testing DMA memory manager...\n");
    uintptr_t frame1;
    if (!pmm_dma_get_free_frame(&frame1))
        panic("PMM: Couldn't get DMA frame!\n");

    // Test memory.
    kprintf("PMM: Testing %uKB of memory at 0x%X (0x%X)...", PAGE_SIZE_64K / 1024, frame1, pmm_dma_get_phys(frame1));
    uint32_t *framePtr = (uint32_t*)frame1;
    for (uint32_t i = 0; i < PAGE_SIZE_64K / sizeof(uint32_t); i++)
        framePtr[i] = i;

    bool pass = true;
    for (uint32_t i = 0; i < PAGE_SIZE_64K / sizeof(uint32_t); i++)
        if (framePtr[i] != i) {
            pass = false;
            break;
        }
    kprintf("%s!\n", pass ? "passed" : "failed");

    // Pull another frame.
    uintptr_t frame2;
    if (!pmm_dma_get_free_frame(&frame2))
        panic("PMM: Couldn't get DMA frame!\n");

    // Test memory.
    kprintf("PMM: Testing %uKB of memory at 0x%X (0x%X)...", PAGE_SIZE_64K / 1024, frame2, pmm_dma_get_phys(frame2));
    framePtr = (uint32_t*)frame2;
    for (uint32_t i = 0; i < PAGE_SIZE_64K / sizeof(uint32_t); i++)
        framePtr[i] = i;

    pass = true;
    for (uint32_t i = 0; i < PAGE_SIZE_64K / sizeof(uint32_t); i++)
        if (framePtr[i] != i) {
            pass = false;
            break;
        }
    kprintf("%s!\n", pass ? "passed" : "failed");

    // Free both frames.
    pmm_dma_set_frame(frame1, true);
    pmm_dma_set_frame(frame2, true);
    kprintf("PMM: DMA memory manager test complete.\n");
}

/**
 * 
 * NORMAL MEMORY FUNCTIONS
 * 
 */

/**
 * Gets the current number of page frames available.
 */
uint32_t pmm_frames_available() {
    return pageFramesAvailable;
}

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
/**
 * Pops a page frame off the standard stack.
 * @return 		The physical address of the page frame.
 */
uint32_t pmm_pop_frame_nonpae() {
    // Verify there are frames.
    if (pmm_frames_available() == 0)
        panic("PMM: No more page frames!\n");

    // Get frame off stack.
    uintptr_t frame = *pageFrameStack;

    // Decrement stack and return frame.
    pageFrameStack--;
    pageFramesAvailable--;
    return frame;
}

/**
 * Gets the current number of PAE page frames available.
 */
uint32_t pmm_frames_available_pae() {
    return pageFramesPaeAvailable;
}
#endif

/**
 * Pops a page frame off the stack.
 * @return 		The physical address of the page frame.
 */
uint64_t pmm_pop_frame() {
#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
    // Are there PAE frames available? If so pop one of those.
    if (pmm_frames_available_pae()) {
        // Get frame off stack.
        uint64_t frame = *pageFrameStackPae;

        // Decrement stack and return frame.
        pageFrameStackPae--;
        pageFramesPaeAvailable--;
        return frame;
    }
#endif

    // Verify there are frames.
    if (pmm_frames_available() == 0)
        panic("PMM: No more page frames!\n");

    // Get frame off stack.
    uintptr_t frame = *pageFrameStack;

    // Decrement stack and return frame.
    pageFrameStack--;
    pageFramesAvailable--;
    return frame;
}

/**
 * Pushes a page frame to the stack.
 * @param frame	The physical address of the page frame to push.
 */
void pmm_push_frame(uint64_t frame) {
#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
    // Is the frame above 4GB? If so, its a PAE frame.
    if (frame >= PAGE_SIZE_4G) {
        // If PAE is not enabled, we can't push PAE frames.
        if (!memInfo.paeEnabled)
            panic("PMM: Attempting to push PAE page frame 0x%llX without PAE!\n", frame);

        // Increment stack pointer and check its within bounds.
        pageFrameStackPae++;
        if (((uintptr_t)pageFrameStackPae) < memInfo.pageFrameStackPaeStart || ((uintptr_t)pageFrameStackPae) >= memInfo.pageFrameStackPaeEnd)
            panic("PMM: PAE page frame stack pointer out of bounds!\n");

        // Push frame to stack.
        *pageFrameStackPae = frame;
        pageFramesPaeAvailable++;
        return;
    }
#endif

    // Increment stack pointer and check its within bounds.
    pageFrameStack++;
    if (((uintptr_t)pageFrameStack) < memInfo.pageFrameStackStart || ((uintptr_t)pageFrameStack) >= memInfo.pageFrameStackEnd)
        panic("PMM: Page frame stack pointer out of bounds!\n");

    // Push frame to stack.
    *pageFrameStack = (uintptr_t)frame;
    pageFramesAvailable++;
}

/**
 * Prints the memory map.
 */
void pmm_print_memmap() {
    kprintf("PMM: Physical memory map:\n");
    uint64_t memory = 0;

#ifdef PMM_MULTIBOOT2
    // Get first tag.
    multiboot_tag_t *tag = (multiboot_tag_t*)((uint64_t)&memInfo.mbootInfo->firstTag);
    uint64_t end = (uint64_t)memInfo.mbootInfo + memInfo.mbootInfo->size;

    for (; (tag->type != MULTIBOOT_TAG_TYPE_END) && ((uint64_t)tag < end); tag = (multiboot_tag_t*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            multiboot_tag_mmap_t* mmap = (multiboot_tag_mmap_t*)tag;
            for (multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t*)mmap->entries;
                (uint64_t)entry < (uint64_t)mmap + mmap->size; entry = (multiboot_mmap_entry_t*)((uint64_t)entry + mmap->entry_size)) {
                // Print out info.
                kprintf("PMM:     region start: 0x%llX length: 0x%llX type: 0x%X\n", entry->addr, entry->len, entry->type);
                if (entry->type == 1)
                    memory += entry->len;
            }
        }  
    }
#else
    // Is a memory map present?
    if (memInfo.mbootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        uint32_t base = memInfo.mbootInfo->mmap_addr;
        uint32_t end = base + memInfo.mbootInfo->mmap_length;
        multiboot_memory_map_t* entry = (multiboot_memory_map_t*)base;

        for (; base < end; base += entry->size + sizeof(uint32_t)) {
            entry = (multiboot_memory_map_t*)base;

            // Print out info.
            kprintf("PMM:     region start: 0x%llX length: 0x%llX type: 0x%X\n", entry->addr, entry->len, (uint64_t)entry->type);
            if (entry->type == 1 && (((uint32_t)entry->addr) > 0 || (memInfo.paeEnabled && (entry->addr & 0xF00000000))))
                memory += entry->len;
        }
    }
    else {
        // No memory map, so take the high memory amount instead.
        kprintf("PMM: No memory map found! The reported RAM may be incorrect.\n");
        memory = memInfo.mbootInfo->mem_upper * 1024;
    }
#endif

    // Print summary.
    kprintf("PMM: Kernel start: 0x%p | End: 0x%p\n", memInfo.kernelStart, memInfo.kernelEnd);
    #ifdef X86_64
    kprintf("PMM: Multiboot info start: 0x%p | End: 0x%p\n", memInfo.mbootInfo, ((uintptr_t)memInfo.mbootInfo) + memInfo.mbootInfo->size);
    #endif
    kprintf("PMM: Page frame stack start: 0x%p | End: 0x%p\n", memInfo.pageFrameStackStart, memInfo.pageFrameStackEnd);

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
    if (memInfo.paeEnabled && memInfo.pageFrameStackPaeStart > 0 && memInfo.pageFrameStackPaeEnd > 0)
        kprintf("PMM: PAE page frame stack start: 0x%X | Page stack end: 0x%X\n", memInfo.pageFrameStackPaeStart, memInfo.pageFrameStackPaeEnd);
#endif

    memInfo.memoryKb = memory / 1024;
    kprintf("PMM: Detected usable RAM: %uKB\n", memInfo.memoryKb);
}

/**
 * Builds the page frame stacks.
 */
static void pmm_build_stacks() {
    // Initialize stack.
    kprintf("PMM: Initializing page frame stack at 0x%X...\n", memInfo.pageFrameStackStart);
    pageFrameStack = (uintptr_t*)(memInfo.pageFrameStackStart);
    memset(pageFrameStack, 0, memInfo.pageFrameStackEnd - memInfo.pageFrameStackStart);

    // Perform memory test on stack areas.
    kprintf("PMM: Testing %uKB of memory at 0x%X...", (memInfo.pageFrameStackEnd - memInfo.pageFrameStackStart) / 1024, memInfo.pageFrameStackStart);
    for (uintptr_t i = 0; i <= (memInfo.pageFrameStackEnd - memInfo.pageFrameStackStart) / sizeof(uintptr_t); i++)
        pageFrameStack[i] = i;

    bool pass = true;
    for (uintptr_t i = 0; i <= (memInfo.pageFrameStackEnd - memInfo.pageFrameStackStart) / sizeof(uintptr_t); i++)
        if (pageFrameStack[i] != i) {
            pass = false;
            break;
        }
    kprintf("%s!\n", pass ? "passed" : "failed");
    if (!pass)
        panic("PMM: Memory test of page frame stack area failed.\n");

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
    // If PAE is enabled, initialize PAE stack.
    if (memInfo.paeEnabled && memInfo.pageFrameStackPaeStart > 0 && memInfo.pageFrameStackPaeEnd > 0) {
        // Initialize stack pointer.
        kprintf("PMM: Initializing PAE page frame stack at 0x%X...\n", memInfo.pageFrameStackPaeStart);
        pageFrameStackPae = (uint64_t*)(memInfo.pageFrameStackPaeStart);
        memset(pageFrameStackPae, 0, memInfo.pageFrameStackPaeEnd - memInfo.pageFrameStackPaeStart);

        // Perform memory test on stack areas.
        kprintf("PMM: Testing %uKB of memory at 0x%X...", (memInfo.pageFrameStackPaeEnd - memInfo.pageFrameStackPaeStart) / 1024, memInfo.pageFrameStackPaeStart);
        for (uint64_t i = 0; i <= (memInfo.pageFrameStackPaeEnd - memInfo.pageFrameStackPaeStart) / sizeof(uint64_t); i++)
            pageFrameStackPae[i] = i;

        bool pass = true;
        for (uint64_t i = 0; i <= (memInfo.pageFrameStackPaeEnd - memInfo.pageFrameStackPaeStart) / sizeof(uint64_t); i++)
            if (pageFrameStackPae[i] != i) {
                pass = false;
                break;
            }
        kprintf("%s!\n", pass ? "passed" : "failed");
        if (!pass)
            panic("PMM: Memory test of PAE page frame stack area failed.\n");
    }
#endif

    // Build stack of free page frames.
#ifdef X86_64
    // Get first tag.
    multiboot_tag_t *tag = (multiboot_tag_t*)((uint64_t)&memInfo.mbootInfo->firstTag);
    uint64_t end = (uint64_t)memInfo.mbootInfo + memInfo.mbootInfo->size;

    for (; (tag->type != MULTIBOOT_TAG_TYPE_END) && ((uint64_t)tag < end); tag = (multiboot_tag_t*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            multiboot_tag_mmap_t* mmap = (multiboot_tag_mmap_t*)tag;
            for (multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t*)mmap->entries;
                (uint64_t)entry < (uint64_t)mmap + mmap->size; entry = (multiboot_mmap_entry_t*)((uint64_t)entry + mmap->entry_size)) {
                // If not available memory, skip over.
                if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
                    continue;

                if (entry->addr > 0) {
                    // Add frame to stack.
                    uint64_t pageFrameBase = ALIGN_4K_64BIT(entry->addr);	
                    kprintf("PMM: Adding pages in 0x%llX!\n", pageFrameBase);			
                    for (uint32_t i = 0; i < (entry->len / PAGE_SIZE_4K) - 1; i++) { // Give buffer incase another section of the memory map starts partway through a page.
                        uint64_t addr = pageFrameBase + (i * PAGE_SIZE_4K);

                        // If the address is in conventional memory (low memory), or is reserved by
                        // the kernel or the frame stack, don't mark it free.
                        if (addr <= 0x100000 || (addr >= (memInfo.kernelStart - memInfo.kernelVirtualOffset) &&
                            addr <= (memInfo.pageFrameStackEnd - memInfo.kernelVirtualOffset)) || addr >= entry->addr + entry->len)
                            continue;

                        // Add frame to stack.
                        pmm_push_frame(addr);
                    }
                }
            }
        }  
    }
#else
    // Is a memory map present?
    if (memInfo.mbootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        for (multiboot_memory_map_t *entry = (multiboot_memory_map_t*)memInfo.mbootInfo->mmap_addr; (uint32_t)entry < memInfo.mbootInfo->mmap_addr + memInfo.mbootInfo->mmap_length;
            entry = (multiboot_memory_map_t*)((uint32_t)entry + entry->size + sizeof(entry->size))) {
            
            // If not available memory, skip over.
            if (entry->type != MULTIBOOT_MEMORY_AVAILABLE || entry->len < PAGE_SIZE_4K)
                continue;

            // Add frame to stack.
            uint64_t pageFrameBase = ALIGN_4K_64BIT(entry->addr);	
            kprintf("PMM: Adding pages in 0x%llX!\n", pageFrameBase);			
            for (uint32_t i = 0; i < (entry->len / PAGE_SIZE_4K) - 1; i++) { // Give buffer incase another section of the memory map starts partway through a page.
                uint64_t addr = pageFrameBase + (i * PAGE_SIZE_4K);

                // If the address is in conventional memory (low memory), or is reserved by
                // the kernel or the frame stack, don't mark it free.
                if (addr <= 0x100000 || (addr >= (memInfo.kernelStart - memInfo.kernelVirtualOffset) &&
                    addr <= (memInfo.pageFrameStackEnd - memInfo.kernelVirtualOffset)) || addr >= entry->addr + entry->len)
                    continue;

                // If address is a PAE one, and PAE is not enabled, ignore.
                if (addr >= PAGE_SIZE_4G && !memInfo.paeEnabled)
                    break;

                // Add frame to stack.
                pmm_push_frame(addr);
            }
        }
    }
    else {
        // No memory map, so take the high memory amount instead.
        uint32_t pageFrameBase = ALIGN_4K(0x100000);	
        kprintf("PMM: Adding pages in 0x%p!\n", pageFrameBase);			
        for (uint32_t i = 0; i < ((memInfo.mbootInfo->mem_upper * 1024) / PAGE_SIZE_4K) - 1; i++) { // Give buffer incase another section of the memory map starts partway through a page.
            uint32_t addr = pageFrameBase + (i * PAGE_SIZE_4K);

            // If the address is in conventional memory (low memory), or is reserved by
            // the kernel or the frame stack, don't mark it free.
            if (addr <= 0x100000 || (addr >= (memInfo.kernelStart - memInfo.kernelVirtualOffset) &&
                addr <= (memInfo.pageFrameStackEnd - memInfo.kernelVirtualOffset)))
                continue;

            // Add frame to stack.
            pmm_push_frame(addr);
        }
    }
#endif

    // Print out status.
    kprintf("PMM: Added %u page frames!\n", pageFramesAvailable);
    kprintf("PMM: First page on stack: 0x%p\n", *pageFrameStack);

#ifndef X86_64 // PAE does not apply to the 64-bit kernel.
    if (memInfo.paeEnabled && pageFramesAvailable > 0) {
        kprintf("PMM: Added %u PAE page frames!\n", pageFramesPaeAvailable);
        kprintf("PMM: First page on PAE stack: 0x%llX\n", *pageFrameStackPae);
    }
#endif
}

/**
 * Initializes the physical memory manager.
 */
void pmm_init() {
    kprintf("\e[35mPMM: Initializing physical memory manager...\n");

    // Store away Multiboot info.
    memInfo.mbootInfo = (multiboot_info_t*)(MULTIBOOT_INFO + (uintptr_t)&KERNEL_VIRTUAL_OFFSET);

    // Store where the kernel is. These come from the linker.
    memInfo.kernelVirtualOffset = (uintptr_t)&KERNEL_VIRTUAL_OFFSET;
    memInfo.kernelStart = (uintptr_t)&KERNEL_VIRTUAL_START;
    memInfo.kernelEnd = (uintptr_t)&KERNEL_VIRTUAL_END;

    // Store where the DMA page frames are virtually.
    memInfo.dmaPageFrameFirst = DMA_FRAMES_FIRST + memInfo.kernelVirtualOffset;
    memInfo.dmaPageFrameLast = DMA_FRAMES_LAST + memInfo.kernelVirtualOffset;

    // Store the virtual page frame stack locations. This is determined during early boot in kernel_main_early().
    memInfo.pageFrameStackStart = PAGE_FRAME_STACK_START + memInfo.kernelVirtualOffset;
    memInfo.pageFrameStackEnd = PAGE_FRAME_STACK_END + memInfo.kernelVirtualOffset;
    
#ifdef X86_64 // PAE does not apply to the 64-bit kernel.
    memInfo.paeEnabled = true;
#else
    memInfo.pageFrameStackPaeStart = PAGE_FRAME_STACK_PAE_START + memInfo.kernelVirtualOffset;
    memInfo.pageFrameStackPaeEnd = PAGE_FRAME_STACK_PAE_END + memInfo.kernelVirtualOffset;
    memInfo.paeEnabled = PAE_ENABLED;
#endif
    earlyPagesLast = EARLY_PAGES_LAST;

    // Print memory map.
    pmm_print_memmap();

    // Build DMA bitmap.
    pmm_dma_build_bitmap();

    // Build stacks.
    pmm_build_stacks();
    kprintf("PMM: Initialized!\e[0m\n");
}
