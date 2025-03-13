#include <cstddef>
#include <cstdint>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/global_control.h>
#include <tbb/parallel_scan.h>
#include <tbb/spin_mutex.h>


typedef struct spin_mutex_st {
  tbb::spin_mutex mutex;
} spin_mutex_t;

extern "C" {
	static int nthreads  = 0;
	static int blocksize = 10;

	void parallel_set_thread_limit(int number_of_threads) {
		if (number_of_threads > 0) {
			nthreads = number_of_threads;
		}
	}

	void parallel_set_blocksize(int blocksize_in) {
		if (blocksize_in > 0) {
			blocksize = blocksize_in;
		}
	}

    void foreach_in_range(void (*func)(void*, int, size_t, size_t), void* array, int length, size_t n) {
    	tbb::global_control control(tbb::global_control::max_allowed_parallelism, nthreads);

    	//printf("blocksize = %d\n",blocksize>0 ? blocksize : block_size);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, n, blocksize),
            [array, length, func](const tbb::blocked_range<size_t>& subrange) {
                func(array, length, subrange.begin(), subrange.end());
            }
        );
    }
    
    void foreach_in_range_two(void (*func)(void*, void*, int, size_t, size_t), void* array1, void* array2, int length, size_t n) {
    	tbb::global_control control(tbb::global_control::max_allowed_parallelism, nthreads);

    	//printf("blocksize = %d\n",blocksize>0 ? blocksize : block_size);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, n, blocksize),
            [array1, array2, length, func](const tbb::blocked_range<size_t>& subrange) {
                func(array1, array2, length, subrange.begin(), subrange.end());
            }
        );
    }

    void parallel_scan_c(void** input, void** sums, void* create_array , void* (*f)(void*, void*, void*, int), int length, int stride){
    	tbb::global_control control(tbb::global_control::max_allowed_parallelism, nthreads);

        tbb::parallel_scan(
            tbb::blocked_range<size_t>(0, length, blocksize),
            (void*) NULL, /* starting value (identity elements) */
            // now define the scan operation
            [input, sums, create_array, f, length, stride](const tbb::blocked_range<size_t>& r, void* sum, bool is_final_scan) {
                void* temp = sum;
                for (size_t i = r.begin(); i != r.end(); ++i) {
                    int j = i + 1;
                    if (stride == -1){
                        j = length - 1 - i;
                    }

                    temp = f(temp, input[j], create_array, is_final_scan);

                    if (is_final_scan) {
                        sums[i] = temp;
                    }
                }
                return temp;
            },
            // and now the combining operation
            [f, create_array](void* left, void* right) { return f(left, right, create_array, 0); }
            // there is also a version with an explicit is_final flag
        );
    }
   
   
spin_mutex_t* spin_mutex_create() {
    spin_mutex_t* wrapper = (spin_mutex_t*) malloc(sizeof(spin_mutex_t));
    if (wrapper) {
        // Construct the spin_mutex in place
        new (&wrapper->mutex) tbb::spin_mutex();
    }
    return wrapper;
}

void spin_mutex_lock(spin_mutex_t* mutex) {
    if (mutex) {
        mutex->mutex.lock();
    }
}

void spin_mutex_unlock(spin_mutex_t* mutex) {
    if (mutex) {
        mutex->mutex.unlock();
    }
}

void spin_mutex_destroy(spin_mutex_t* mutex) {
    if (mutex) {
        mutex->mutex.~spin_mutex();
        free(mutex);
    }
}
    
}
