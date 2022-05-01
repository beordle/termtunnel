#ifndef __QUEUE_H__
#define __QUEUE_H__

/**
  * Copyright (C) 2011 by Tobias Thiel
  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:
  * 
  * The above copyright notice and this permission notice shall be included in
  * all copies or substantial portions of the Software.
  * 
  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  * THE SOFTWARE.
  */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> /* (u)intX_t */
#ifndef _WIN32
#include <unistd.h> /* usleep */
#else
#include <windows.h> /* Sleep */
#endif
#include <errno.h> /* EBUSY */

#include <pthread.h> /* pthread_mutex_t, pthread_cond_t */

#ifdef _WIN32
#define sleepmilli(x) Sleep(x)
#else
#define sleepmilli(x) usleep(x * 1000)
#endif

/**
  * type which is used for counting the number of elements.
  * needed for limited queues
  */
#ifndef UINTX_MAX
  typedef uint16_t uintX_t;
  #define UINTX_MAX UINT16_MAX
#endif

/**
 * simple macros to reduce necessary casting to void *
 * or function pointers
 */
#define DEFINE_Q_DESTROY(fname, type) int8_t fname(queue_t *q, void (*ff)(type *)) { return queue_destroy_complete(q, (void (*)(void *))ff); }
#define DEFINE_Q_FLUSH(fname, type) int8_t fname(queue_t *q, void (*ff)(type *)) { return queue_flush_complete(q, (void (*)(void *))ff); }
#define DEFINE_Q_GET(fname, type) int8_t fname(queue_t *q, type **e) { return queue_get(q, (void **)e); }
#define DEFINE_Q_GET_WAIT(fname, type) int8_t fname(queue_t *q, type **e) { return queue_get_wait(q, (void **)e); }
#define DEFINE_Q_PUT(fname, type) int8_t fname(queue_t *q, type *e) { return queue_put(q, (void *)e); }
#define DEFINE_Q_PUT_WAIT(fname, type) int8_t fname(queue_t *q, type *e) { return queue_put_wait(q, (void *)e); }
#define DEFINE_Q_FLUSH_PUT(fname, type) int8_t fname(queue_t *q, void (*ff)(type *), type *e) { return queue_flush_complete_put(q, (void (*)(void *))ff, (void *)e); }

/**
  * returned error codes, everything except Q_OK should be < 0
  */
typedef enum queue_erros_e {
	Q_OK = 0,
	Q_ERR_INVALID = -1,
	Q_ERR_LOCK = -2,
	Q_ERR_MEM = -3,
	Q_ERR_NONEWDATA = -4,
	Q_ERR_INVALID_ELEMENT = -5,
	Q_ERR_INVALID_CB = -6,
	Q_ERR_NUM_ELEMENTS = -7
} queue_errors_t;

typedef struct queue_element_s {
	void *data;
	struct queue_element_s *next;
} queue_element_t;

typedef struct queue_s {
	queue_element_t *first_el, *last_el;
	// (max.) number of elements
	uintX_t num_els;
	uintX_t max_els;
	// no new data allowed
	uint8_t new_data;
	// sorted queue
	int8_t sort;
	int8_t asc_order;
	int (*cmp_el)(void *, void *);
	// multithreaded
	pthread_mutex_t *mutex;
	pthread_cond_t *cond_get;
	pthread_cond_t *cond_put;
} queue_t;

/**
  * initializes and allocates a queue with unlimited elements
  *
  * returns NULL on error, or a pointer to the queue
  */
queue_t *queue_create(void);

/**
  * initializes and allocates a queue
  *
  * max_elements - maximum number of elements which are allowed in the queue, == 0 for "unlimited"
  *
  * returns NULL on error, or a pointer to the queue
  */
queue_t *queue_create_limited(uintX_t max_elements);

/**
  * just like queue_create()
  * additionally you can specify a comparator function so that your elements are ordered in the queue
  * elements will only be ordered if you create the queue with this method
  * the compare function should return 0 if both elements are the same, < 0 if the first is smaller
  * and > 0 if the second is smaller
  *
  * asc - sort in ascending order if not 0
  * cmp - comparator function, NULL will create an error
  *
  * returns NULL on error, or a pointer to the queue
  */
queue_t *queue_create_sorted(int8_t asc, int (*cmp)(void *, void *));

/**
  * see queue_create_limited() and queue_create_sorted()
  */
queue_t *queue_create_limited_sorted(uintX_t max_elements, int8_t asc, int (*cmp)(void *, void *));

/**
  * releases the memory internally allocated and destroys the queue
  * you have to release the memory the elements in the queue use
  *
  * q - the queue to be destroyed
  */
int8_t queue_destroy(queue_t *q);

/**
  * in addition to queue_destroy(), this function will also free the memory of your elements
  *
  * q - the queue to be destroyed
  * ff - the free function to be used for the elements (free() if NULL)
  */
int8_t queue_destroy_complete(queue_t *q, void (*ff)(void *));

/**
  * deletes all the elements from the queue, but does not release their memory
  *
  * q - the queue
  */
int8_t queue_flush(queue_t *q);

/**
  * just like queue_flush, but releases the memory of the elements
  *
  * q - the queue
  * ff - the free function to be used for the elements (free() if NULL)
  */
int8_t queue_flush_complete(queue_t *q, void (*ff)(void *));

/**
  * just like queue_flush, followed by an queue_put atomically
  *
  * q - the queue
  * ff - the free function to be used for the elements (free() if NULL)
  * e - the element
  */
int8_t queue_flush_put(queue_t *q, void (*ff)(void *), void *e);

/**
  * just like queue_flush_complete, followed by an queue_put atomically
  *
  * q - the queue
  * ff - the free function to be used for the elements (free() if NULL)
  * e - the element
  */
int8_t queue_flush_complete_put(queue_t *q, void (*ff)(void *), void *e);

/**
  * returns the number of elements in the queue
  *
  * q - the queue
  *
  * returns number of elements or UINTX_MAX if an error occured
  */
uintX_t queue_elements(queue_t *q);

/**
  * returns wether the queue is empty
  * returns empty if there was an error
  *
  * q - the queue
  *
  * returns zero if queue is NOT empty, < 0 => error
  */
int8_t queue_empty(queue_t *q);

/**
  * put a new element at the end of the queue
  * will produce an error if you called queue_no_new_data()
  *
  * q - the queue
  * e - the element
  *
  * returns 0 if everything worked, > 0 if max_elements is reached, < 0 if error occured
  */
int8_t queue_put(queue_t *q, void *e);

/**
  * the same as queue_put(), but will wait if max_elements is reached,
  * until queue_set_new_data(, 0) is called or elements are removed
  *
  * q - the queue
  * e - the element
  *
  * returns 0 if everything worked, < 0 if error occured
  */
int8_t queue_put_wait(queue_t *q, void *e);

/**
  * get the first element of the queue
  *
  * q - the queue
  * e - pointer which will be set to the element
  *
  * returns 0 if everything worked, > 0 if no elements in queue, < 0 if error occured
  */
int8_t queue_get(queue_t *q, void **e);

/**
  * the same as queue_get(), but will wait if no elements are in the queue,
  * until queue_set_new_data(, 0) is called or new elements are added
  *
  * q - the queue
  * e - pointer which will be set to the element
  *
  * returns 0 if everything worked, < 0 if error occured
  */
int8_t queue_get_wait(queue_t *q, void **e);

/**
  * gets the first element for which the given compare function returns 0 (equal)
  * does NOT wait if no elements in the queue
  * the compare function should return 0 if both elements are the same, < 0 if the first is smaller
  * and > 0 if the second is smaller
  *
  * q - the queue
  * e - pointer which will be set to the element
  * cmp - comparator function, NULL will create an error
  * cmpel - element with which should be compared
  *
  * returns 0 if everything worked, < 0 => error, Q_ERR_NUM_ELEMENTS(<0) if no element fulfills the requirement
  */
int8_t queue_get_filtered(queue_t *q, void **e, int (*cmp)(void *, void *), void *cmpel);

/**
  * sets wether the queue will accept new data
  * defaults to 1 on creation
  * you should use this function when you're done and queue_put_wait() and queue_get_wait() should return
  * queue_put()/queue_put_wait() won't have any effect when new data isn't accepted.
  *
  * q - the queue
  * v - 0 new data NOT accepted
  */
int8_t queue_set_new_data(queue_t *q, uint8_t v);

/**
  * returns wether the queue will accept new data
  * also returns 0, if there was an error
  *
  * q - the queue
  *
  * return 0 if new data is NOT accepted
  */
uint8_t queue_get_new_data(queue_t *q);

#endif /* __QUEUE_H__ */
