#include "memoman.h"
#include <stdio.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define ALIGNMENT 8
#define COALESCE_THRESHOLD 10
#define NUM_SIZE_CLASSES 21
#define NUM_FREE_LISTS 8

static char heap[1024 * 1024];
static char* current = heap;
static size_t total_allocated = 0;
static block_header_t* free_list[NUM_FREE_LISTS] = {NULL};
static block_header_t* size_classes[21] = {NULL};

// ============================================================================
// UTILITIES
// ============================================================================

// Round up to nearest multiple of ALIGNMENT
static inline size_t align_size(size_t size) { return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1); }

// Convert between header and user pointers
static inline void* header_to_user(block_header_t* header) { return (char*)header + sizeof(block_header_t); }
static inline block_header_t* user_to_header(void* ptr) { return (block_header_t*)((char*)ptr - sizeof(block_header_t)); }

// ============================================================================
// SIZE CLASSES
// ============================================================================

// Find size class index, or -1 if too large
static inline int get_size_class(size_t size) {
  if (size <= 128) {
    if (size <= 16) return 0;
    if (size <= 24) return 1;
    if (size <= 32) return 2;
    if (size <= 48) return 3;
    if (size <= 64) return 4;
    if (size <= 96) return 5;
    return 6;  // 128
  }
  
  if (size <= 2048) {
    if (size <= 192) return 7;
    if (size <= 256) return 8;
    if (size <= 512) return 9;
    if (size <= 1024) return 10;
    return 11;  // 2048
  }
  
  if (size <= 16384) {
    if (size <= 4096) return 12;
    if (size <= 8192) return 13;
    return 14;
  }
  
  if (size <= 65536) {
    if (size <= 32768) return 15;
    return 16;
  }
  
  if (size <= 131072) return 17;
  if (size <= 262144) return 18;  
  if (size <= 524288) return 19;  
  if (size <= 1048576) return 20;   
  
  return -1;  
}

static inline int get_free_list_index(size_t size) {
  if (size <= 2048) return -1; // shouldn't happen
  int index = 0;
  size_t min_size = 2048;
  while (min_size * 2 <= size && index < NUM_FREE_LISTS - 1) {
    min_size *= 2;
    index++;
  }
  return index;
}

// O(1) allocation from size class
static inline void* pop_from_class(int class) {
  if (class < 0 || class >= NUM_SIZE_CLASSES || !size_classes[class]) { return NULL; }
  
  block_header_t* block = size_classes[class];
  size_classes[class] = block->next;
  block->is_free = 0;
  block->next = NULL;
  
  return header_to_user(block);
}

// ============================================================================
// ALLOCATION
// ============================================================================

void* memomall(size_t size) {
  if (size == 0) return NULL;  
  size = align_size(size);  

  // Fast path: try size class (O(1))
  int class = get_size_class(size);
  
  if (class >= 0) {
    void* ptr = pop_from_class(class);
    if (ptr) return ptr;
  } 

  // Slow path: best-fit search in appropriate free_list
  int list_index = get_free_list_index(size);
  
  if (list_index >= 0) {
    block_header_t** prev_ptr = &free_list[list_index];
    block_header_t* current_block = free_list[list_index];
    block_header_t* best_fit = NULL;
    block_header_t** best_prev = NULL;  
    while (current_block != NULL) {
      if (current_block->size >= size) {
        if (best_fit == NULL || current_block->size < best_fit->size) {
          best_fit = current_block;
          best_prev = prev_ptr;
        }
      }
      prev_ptr = &current_block->next;
      current_block = current_block->next;
    }

    if (best_fit) {
      *best_prev = best_fit->next;  
      
      // Split if enough space remains
      size_t remaining_size = best_fit->size - size;
      size_t min_split_size = sizeof(block_header_t) + ALIGNMENT;  
      
      if (remaining_size >= min_split_size) {
        char* split_point = (char*)header_to_user(best_fit) + size;
        block_header_t* new_block = (block_header_t*)split_point;
        new_block->size = remaining_size - sizeof(block_header_t);
        new_block->is_free = 1;
        int new_list_index = get_free_list_index(new_block->size);
        new_block->next = free_list[new_list_index];
        free_list[new_list_index] = new_block;
        best_fit->size = size;
      }  
      
      best_fit->is_free = 0;
      best_fit->next = NULL;
      
      return header_to_user(best_fit);
    }
  }  

  // Fresh allocation
  size_t total_size = sizeof(block_header_t) + size;  

  // Out of memory
  if (current + total_size > heap + sizeof(heap)) { return NULL; }  
  block_header_t* header = (block_header_t*)current;
  header->size = size;
  header->is_free = 0;
  header->next = NULL;  
  current += total_size;
  total_allocated += total_size;  
  return header_to_user(header);
}

void memofree(void* ptr) {
  if (ptr == NULL) return;  

  block_header_t* header = user_to_header(ptr);
  header->is_free = 1;  
  int class = get_size_class(header->size);

  if (class >= 0) {
    // Small blocks: O(1) push to size class
    header->next = size_classes[class];
    size_classes[class] = header;
    return;
  } 

  // Large blocks: Insert into correct free_list (sorted by address for coalescing)
  int list_index = get_free_list_index(header->size);
  block_header_t* curr = free_list[list_index];
  block_header_t* prev = NULL;
  
  while (curr && (char*)curr < (char*)header) {
    prev = curr;
    curr = curr->next;
  }

  header->next = curr;
  if (prev) prev->next = header;
  else free_list[list_index] = header;  
  
  // Coalesce with next block if possible
  char* block_end = (char*)header + sizeof(block_header_t) + header->size;
  block_header_t* next = (block_header_t*)block_end;
  
  if ((char*)next < heap + sizeof(heap) && next->is_free) {
    int next_list_index = get_free_list_index(next->size);

    // Remove next from its free list
    block_header_t* next_curr = free_list[next_list_index];
    block_header_t* next_prev = NULL;
    
    while (next_curr) {
      if (next_curr == next) {
        if (next_prev) next_prev->next = next_curr->next;
        else free_list[next_list_index] = next_curr->next;
        break;
      }
      next_prev = next_curr;
      next_curr = next_curr->next;
    
    }
    
    header->size += sizeof(block_header_t) + next->size;
    header->next = next->next;
  } 

  // Coalesce with previous block if possible
  // Scan free list for a block ending at header
  curr = free_list[list_index];
  prev = NULL;
  
  while (curr) {
    char* curr_end = (char*)curr + sizeof(block_header_t) + curr->size;
    
    if (curr_end == (char*)header && curr->is_free) {
      curr->size += sizeof(block_header_t) + header->size;
      curr->next = header->next;
      // Remove header from free list
      if (prev) prev->next = curr;
      else free_list[list_index] = curr;
      break;
    }

    prev = curr;
    curr = curr->next;
  }
}

// ============================================================================
// MANAGEMENT
// ============================================================================

size_t get_total_allocated(void) { return total_allocated; }
size_t get_free_space(void) { return sizeof(heap) - (current - heap); }
block_header_t* get_free_list(void) { return free_list[0]; }

void reset_allocator(void) {
  current = heap;
  total_allocated = 0;
  
  for (int i = 0; i < NUM_FREE_LISTS; i++) free_list[i] = NULL;
  for (int i = 0; i < NUM_SIZE_CLASSES; i++) size_classes[i] = NULL;

}

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
  size_t used_heap = current - heap;
  size_t free_heap = sizeof(heap) - used_heap;
  printf("\n=== Heap Statistics ===\n");
  printf("Total heap size: %zu bytes (%.2f MB)\n", sizeof(heap), sizeof(heap) / (1024.0 * 1024.0));
  printf("Used heap space: %zu bytes\n", used_heap);
  printf("Free heap space: %zu bytes\n", free_heap);
  printf("Usage: %.1f%%\n", (double)used_heap / sizeof(heap) * 100);
  printf("====================\n\n");
#endif
}

void print_free_list(void) {
#ifdef DEBUG_OUTPUT
  printf("\n=== Free List ===\n");
  for (int i = 0; i < NUM_FREE_LISTS; i++) {
    printf("Free List %d:\n", i);
    block_header_t* current_block = free_list[i];
    int count = 0;
    while (current_block != NULL) {
      printf("  Block %d: %zu bytes at %p\n", count++, current_block->size, (void*)current_block);
      current_block = current_block->next;
    }
    if (count == 0) printf("  Empty\n");
  }
  printf("====================\n\n");
#endif
}