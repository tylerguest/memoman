#include "malloc.h"
#include <stdio.h>

#define ALIGNMENT 8 // for performance

// bump allocator
static char heap[1024 * 1024]; // 1MB heap
static char* current = heap;
static size_t total_allocated = 0;

static int debug_output = 0;

static int free_count = 0;
static const int COALESCE_THRESHOLD = 50;

static block_header_t* free_list = NULL;

static void coalesce_free_blocks(void);
static void sort_free_list_by_address(void);

static size_t align_size(size_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static void* header_to_user(block_header_t* header) {
    return (char*)header + sizeof(block_header_t);
}

static block_header_t* user_to_header(void* ptr) {
    return (block_header_t*)((char*)ptr - sizeof(block_header_t));
}

void* my_malloc(size_t size) {
    if (size == 0) return NULL;

    size = align_size(size);

    block_header_t* prev = NULL;
    block_header_t* current_block = free_list;

    while (current_block != NULL) {
        if (current_block->size >= size) {
            if (prev == NULL) {
                free_list = current_block->next;
            } else {
                prev->next = current_block->next;
            }

            size_t remaining_size = current_block->size - size;
            size_t min_split_size = sizeof(block_header_t) + ALIGNMENT;

            if (remaining_size >= min_split_size) {
                char* split_point = (char*)header_to_user(current_block) + size;
                block_header_t* new_block = (block_header_t*)split_point;

                new_block->size = remaining_size - sizeof(block_header_t);
                new_block->is_free = 1;
                new_block->next = free_list;

                free_list = new_block;
                current_block->size = size;

                if (debug_output) printf("Split block: used %zu bytes, created free block of %zu bytes\n",
                       size, new_block->size);
            } else {}

            current_block->is_free = 0;
            current_block->next = NULL;

            void* user_ptr = header_to_user(current_block);
            if (debug_output) printf("Reused block of %zu bytes (requested %zu) at %p\n",
                   current_block->size, size, user_ptr);

            return user_ptr;
        }

        prev = current_block;
        current_block = current_block->next;
    }

    size_t total_size = sizeof(block_header_t) + size;

    // check for space
    if (current + total_size > heap + sizeof(heap)) {
        if (debug_output) printf("Out of memory!\n");
        return NULL;
    }

    block_header_t* header = (block_header_t*)current;
    header->size = size;
    header->is_free = 0;
    header->next = NULL;

    current += total_size;
    total_allocated += total_size;

    void* user_ptr = header_to_user(header);
    if (debug_output) printf("Allocated %zu bytes (+ %zu header) at %p\n",
           size, sizeof(block_header_t), user_ptr);

    return user_ptr;
}

void my_free(void* ptr) {
    if (ptr == NULL) return;

    block_header_t* header = user_to_header(ptr);
    header->is_free = 1;

    if (debug_output) printf("Freed block of %zu bytes at %p\n", header->size, ptr);

    header->next = free_list;
    free_list = header;

    if (++free_count >= COALESCE_THRESHOLD) {
        coalesce_free_blocks();
        free_count = 0;
    }
}

static void coalesce_free_blocks(void) {
    if (free_list == NULL) return;

    if (debug_output) printf("Attempting to coalesce adjacent blocks...\n");

    sort_free_list_by_address();

    block_header_t* current = free_list;

    while (current != NULL && current->next != NULL) {
        block_header_t* next = current->next;
        
        char* current_end = (char*)header_to_user(current) + current->size;
        char* next_start = (char*)next;

        if (current_end == next_start) {
            if (debug_output) printf("Coalescing blocks: %zu + %zu = %zu bytes\n",
                    current->size,
                    sizeof(block_header_t) + next->size,
                    current->size + sizeof(block_header_t) + next->size);
            current->size += sizeof(block_header_t) + next->size;
            current->next = next->next;
            continue;
        }
        current = current->next;
    }
}

static void sort_free_list_by_address(void) {
    if (free_list == NULL || free_list->next == NULL) return;

    int swapped;
    do {
        swapped = 0;
        block_header_t* prev = NULL;
        block_header_t* current = free_list;

        while (current != NULL && current->next != NULL) {
            if (current > current->next) {
                block_header_t* next = current->next;
                if (prev == NULL) {
                    free_list = next;
                } else { prev->next = next; }
                
                current->next = next->next;
                next->next = current;

                swapped = 1;
                prev = next;
            } else {
                prev = current;
                current = current->next;
            }
        }
    } while (swapped);
}

void print_heap_stats(void) {
    size_t used_heap = current - heap;
    size_t free_heap = sizeof(heap) - used_heap;

    if (debug_output) printf("\n=== Heap Statistics ===\n");
    if (debug_output) printf("Total heap size: %zu bytes (%.2f MB)\n",
           sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
    if (debug_output) printf("Used heap space: %zu bytes\n", used_heap);
    if (debug_output) printf("Free heap space: %zu bytes\n", free_heap);
    if (debug_output) printf("Usage: %.1f%%\n",(double)used_heap / sizeof(heap) * 100);
    if (debug_output) printf("====================\n\n");
}

void print_free_list(void) {
    if (debug_output) printf("\n=== Free List ===\n");
    block_header_t* current = free_list;
    int count = 0;

    while (current != NULL) {
        if (debug_output) printf("Block %d: %zu bytes at %p\n",
                count++, current->size, (void*)current);
        current = current->next;
    }

    if (count == 0) { if (debug_output) printf("Free list is empty\n"); }
    if (debug_output) printf("====================\n\n");
}

size_t get_total_allocated(void) {
    return total_allocated;
}

size_t get_free_space(void) {
    return sizeof(heap) - (current - heap);
}

void reset_allocator(void) {
    current = heap;
    total_allocated = 0;
    free_list = NULL;
    
    if (debug_output) printf("Allocator reset - all memory freed\n");
}