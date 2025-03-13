#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "parallel.h"

void parallel_set_thread_limit(int number_of_threads) {}
void parallel_set_blocksize   (int blocksize_in)      {}

void foreach_in_range(void (*func)(void*, int, size_t, size_t), void* array, int length, size_t n) {
	(*func)( array, length, 0, n );
}

void foreach_in_range_two(void (*func)(void*, void**, int, size_t, size_t), void* array1, void* array2, int length, size_t n) {
	(*func)( array1, array2, length, 0, n );
}

spin_mutex_t* spin_mutex_craete ()                    { return NULL; }
void          spin_mutex_lock   (spin_mutex_t* mutex) {}
void          spin_mutex_unlock (spin_mutex_t* mutex) {}
void          spin_mutex_destroy(spin_mutex_t* mutex) {}

