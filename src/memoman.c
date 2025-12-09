#define _GNU_SOURCE

#include "memoman.h"
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

// **** Configuration ****

#define ALIGNMENT 8
#define COALESCE_THRESHOLD 10
#define NUM_SIZE_CLASSES 21
#define NUM_FREE_LISTS 8
#define INITIAL_HEAP_SIZE (1024 * 1024)
#define MAX_HEAP_SIZE (1024 * 1024 * 1024)
#define HEAP_GROWTH_FACTOR 2
#define LARGE_ALLOC_THRESHOLD (1024 * 1024) // 1MB

static char* heap = NULL;
static char* current = NULL;
static size_t heap_size = 0;
static size_t heap_capacity = 0;
static size_t total_allocated = 0;
static block_header_t* free_list[NUM_FREE_LISTS] = { NULL };
static block_header_t* size_classes[NUM_SIZE_CLASSES] = { NULL };
static large_block_t* large_blocks = NULL;

// **** Utilities ****

static inline size_t align_size(size_t size) { 
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1); 
}

// *** convert between header and user pointers ***
static inline void* header_to_user(block_header_t* header) { 
  return (char*)header + sizeof(block_header_t); 
}
static inline block_header_t* user_to_header(void* ptr) { 
  return (block_header_t*)((char*)ptr - sizeof(block_header_t)); 
}

// **** Size Classes ****

static inline int get_size_class(size_t size) {
  if (size <= 128) {
    if (size <= 16) return 0;
    if (size <= 24) return 1;
    if (size <= 32) return 2;
    if (size <= 48) return 3;
    if (size <= 64) return 4;
    if (size <= 96) return 5;
    return 6; 
  }
  
  if (size <= 2048) {
    if (size <= 192) return 7;
    if (size <= 256) return 8;
    if (size <= 512) return 9;
    if (size <= 1024) return 10;
    return 11;
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
  if (size <= 2048) return -1; 
  int index = 0;
  size_t min_size = 2048;
  while (min_size * 2 <= size && index < NUM_FREE_LISTS - 1) {
    min_size *= 2;
    index++;
  }
  return index;
}

static inline void* pop_from_class(int class) {
  if (class < 0 || class >= NUM_SIZE_CLASSES || !size_classes[class]) { return NULL; }
  block_header_t* block = size_classes[class];
  size_classes[class] = block->next;
  block->is_free = 0;
  block->next = NULL;
  return header_to_user(block);
}

// **** Allocation ****

int memoinit(void) {
  if (heap != NULL) return 0;  // already initialized

  heap = mmap(NULL, MAX_HEAP_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

  if (heap == MAP_FAILED) {
    heap = NULL;
  }

  if (mprotect(heap, INITIAL_HEAP_SIZE, PROT_READ | PROT_WRITE) != 0) {
    munmap(heap, MAX_HEAP_SIZE);
    heap = NULL;
    return -1;
  }

  heap_capacity = INITIAL_HEAP_SIZE;
  current = heap;
  heap_size = 0;
  return 0;
}

void memodestroy(void) {
  if (heap != NULL) {
    munmap(heap, heap_capacity);
    heap = NULL;
    heap_capacity = 0;
    heap_size = 0;
    current = NULL;
  }

  // clean up free lists
  for (int i = 0; i < NUM_FREE_LISTS; i++) free_list[i] = NULL;
  for (int i = 0; i < NUM_SIZE_CLASSES; i++) size_classes[i] = NULL;
}

static int grow_heap(size_t min_additional) {
  size_t new_capacity = heap_capacity * HEAP_GROWTH_FACTOR;
  while (new_capacity < heap_capacity + min_additional) { new_capacity *= HEAP_GROWTH_FACTOR; }
  if (new_capacity > MAX_HEAP_SIZE) { new_capacity = MAX_HEAP_SIZE; }
  if (new_capacity <= heap_capacity) { return -1; } // can't grow further
  
  if (mprotect(heap, new_capacity, PROT_READ | PROT_WRITE) != 0) { return -1; }
  
  heap_capacity = new_capacity;
  return 0;
}

void* memomall(size_t size) {
  // lazy initialization
  if (heap == NULL) { if (memoinit() != 0) return NULL; }
  if (size == 0) return NULL;  

  // large allocations: direct mmap
  if (size >= LARGE_ALLOC_THRESHOLD) {
    size_t total_size = sizeof(large_block_t) + align_size(size);
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;

    large_block_t* block = (large_block_t*)ptr;
    block->size = total_size;
    block->next = large_blocks;
    large_blocks = block;

    return (char*)ptr + sizeof(large_block_t);
  }

  size = align_size(size);  

  int class = get_size_class(size);
  if (class >= 0) {
    void* ptr = pop_from_class(class);
    if (ptr) return ptr;
  } 

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

  size_t total_size = sizeof(block_header_t) + size;  
  
  if (current + total_size > heap + heap_capacity) { 
    // try to grow heap
    if (grow_heap(total_size) != 0) { return NULL; }
  }  
  
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
  
  // check if large allocation
  large_block_t** large_prev = &large_blocks;
  large_block_t* large_curr = large_blocks;

  while(large_curr) {
    if ((char*)large_curr + sizeof(large_block_t) == ptr) {
      *large_prev = large_curr->next;
      munmap(large_curr, large_curr->size);
      return;
    }
    large_prev = &large_curr->next;
    large_curr = large_curr->next;
  }

  block_header_t* header = user_to_header(ptr);
  header->is_free = 1;  
  int class = get_size_class(header->size);

  if (class >= 0) {
    header->next = size_classes[class];
    size_classes[class] = header;
    return;
  } 

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
  
  // forward coalescing - CHECK BOUNDS FIRST
  char* block_end = (char*)header + sizeof(block_header_t) + header->size;
  block_header_t* next = (block_header_t*)block_end;
  
  // verify next block is withing our heap and is valid
  if (heap != NULL &&
      (char*)next >= heap &&
      (char*)next < heap + heap_capacity &&
      (char*)next + sizeof(block_header_t) <= heap + heap_capacity &&
      next->is_free) {
    int next_list_index = get_free_list_index(next->size);

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
  
  // backward coalescing
  curr = free_list[list_index];
  prev = NULL;
  
  while (curr) {
    char* curr_end = (char*)curr + sizeof(block_header_t) + curr->size;
    
    if (curr_end == (char*)header && curr->is_free) {
      curr->size += sizeof(block_header_t) + header->size;
      curr->next = header->next;
      if (prev) prev->next = curr;
      else free_list[list_index] = curr;
      break;
    }

    prev = curr;
    curr = curr->next;
  }
}

// **** Management ****

size_t get_total_allocated(void) { return total_allocated; }
size_t get_free_space(void) { return heap_capacity - (current - heap); }
block_header_t* get_free_list(void) { return free_list[0]; }

void reset_allocator(void) {
  // free large blocks
  while (large_blocks) {
    large_block_t* next = large_blocks->next;
    munmap(large_blocks, large_blocks->size);
    large_blocks = next;
  }

  // reset main heap
  if (heap) {
    current = heap;
    heap_size = 0;

    for (int i = 0; i < NUM_FREE_LISTS; i++) free_list[i] = NULL;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) size_classes[i] = NULL;
  }

}

void print_heap_stats(void) {
#ifdef DEBUG_OUTPUT
  if (heap == NULL) return;
  size_t used_heap = current - heap;
  size_t free_heap = heap_capacity - used_heap;
  printf("\n=== Heap Statistics ===\n");
  printf("Total heap size: %zu bytes (%.2f MB)\n", heap_capacity, heap_capacity / (1024.0 * 1024.0));
  printf("Used heap space: %zu bytes\n", used_heap);
  printf("Free heap space: %zu bytes\n", free_heap);
  printf("Usage: %.1f%%\n", (double)used_heap / heap_capacity * 100);
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