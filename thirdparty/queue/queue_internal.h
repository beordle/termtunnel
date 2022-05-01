#ifndef __QUEUE_INTERNAL_H__
#define __QUEUE_INTERNAL_H__

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

#include "queue.h"

/**
 * ATTENTION:
 * these functions are internal and should not be used directly.
 * they may _not_ lock properly, expecting the caller to do so
 */

/**
 * locks the queue
 * returns 0 on success, else not usable
 */
int8_t queue_lock_internal(queue_t *q);
int8_t queue_empty_internal(queue_t *q);
/**
 * unlocks the queue
 * returns 0 on success, else not usable
 */
int8_t queue_unlock_internal(queue_t *q);

/**
  * adds an element to the queue.
  * when action is NULL the function returns with an error code.
  * queue _has_ to be locked.
  *
  * q - the queue
  * el - the element
  * action - specifies what should be executed if max_elements is reached.
  *
  * returns < 0 => error, 0 okay
  */
int8_t queue_put_internal(queue_t *q, void *el, int (*action)(pthread_cond_t *, pthread_mutex_t *));

/**
  * gets the first element in the queue.
  * when action is NULL the function returns with an error code.
  * queue _has_ to be locked.
  *
  * q - the queue
  * e - element pointer
  * action - specifies what should be executed if there are no elements in the queue
  * cmp - comparator function, NULL will create an error
  * cmpel - element with which should be compared
  *
  * returns < 0 => error, 0 okay
  */
int8_t queue_get_internal(queue_t *q, void **e, int (*action)(pthread_cond_t *, pthread_mutex_t *), int (*cmp)(void *, void *), void *cmpel);

/**
  * destroys a queue.
  * queue will be locked.
  *
  * q - the queue
  * fd - should element data be freed? 0 => No, Otherwise => Yes
  * ff - function to release the memory, NULL => free()
  */
int8_t queue_destroy_internal(queue_t *q, uint8_t fd, void (*ff)(void *));

/**
  * flushes a queue.
  * deletes all elements in the queue.
  * queue _has_ to be locked.
  *
  * q - the queue
  * fd - should element data be freed? 0 => No, Otherwise => Yes
  * ff - function to release the memory, NULL => free()
  */
int8_t queue_flush_internal(queue_t *q, uint8_t fd, void (*ff)(void *));

#endif /* __QUEUE_INTERNAL_H__ */
