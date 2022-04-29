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

#include "queue.h"
#include "queue_internal.h"

int8_t queue_lock_internal(queue_t *q) {
	if (q == NULL)
		return Q_ERR_INVALID;
	// all errors are unrecoverable for us
	if(0 != pthread_mutex_lock(q->mutex))
		return Q_ERR_LOCK;
	return Q_OK;
}

int8_t queue_unlock_internal(queue_t *q) {
	if (q == NULL)
		return Q_ERR_INVALID;
	// all errors are unrecoverable for us
	if(0 != pthread_mutex_unlock(q->mutex))
		return Q_ERR_LOCK;
	return Q_OK;
}

int8_t queue_destroy_internal(queue_t *q, uint8_t fd, void (*ff)(void *)) {
	// this method will not immediately return on error,
	// it will try to release all the memory that was allocated.
	int error = Q_OK;
	// make sure no new data comes and wake all waiting threads
	error = queue_set_new_data(q, 0);
	error = queue_lock_internal(q);
	
	// release internal element memory
	error = queue_flush_internal(q, fd, ff);
	
	// destroy lock and queue etc
	error = pthread_cond_destroy(q->cond_get);
	free(q->cond_get);
	error = pthread_cond_destroy(q->cond_put);
	free(q->cond_put);
	
	error = queue_unlock_internal(q);
	while(EBUSY == (error = pthread_mutex_destroy(q->mutex)))
		sleepmilli(100);
	free(q->mutex);
	
	// destroy queue
	free(q);
	return error;
}

int8_t queue_flush_internal(queue_t *q, uint8_t fd, void (*ff)(void *)) {
	if(q == NULL)
		return Q_ERR_INVALID;
	
	queue_element_t *qe = q->first_el;
	queue_element_t *nqe = NULL;
	while(qe != NULL) {
		nqe = qe->next;
		if(fd != 0 && ff == NULL) {
			free(qe->data);
		} else if(fd != 0 && ff != NULL) {
			ff(qe->data);
		}
		free(qe);
		qe = nqe;
	}
	q->first_el = NULL;
	q->last_el = NULL;
	q->num_els = 0;
	
	return Q_OK;
}

int8_t queue_put_internal(queue_t *q , void *el, int (*action)(pthread_cond_t *, pthread_mutex_t *)) {
	if(q == NULL) // queue not valid
		return Q_ERR_INVALID;
		
	if(q->new_data == 0) { // no new data allowed
		return Q_ERR_NONEWDATA;
	}
	
	// max_elements already reached?
	// if condition _needs_ to be in sync with while loop below!
	if(q->num_els == (UINTX_MAX - 1) || (q->max_els != 0 && q->num_els == q->max_els)) {
		if(action == NULL) {
			return Q_ERR_NUM_ELEMENTS;
		} else {
			while ((q->num_els == (UINTX_MAX - 1) || (q->max_els != 0 && q->num_els == q->max_els)) && q->new_data != 0)
				action(q->cond_put, q->mutex);
			if(q->new_data == 0) {
				return Q_ERR_NONEWDATA;
			}
		}
	}
	
	queue_element_t *new_el = (queue_element_t *)malloc(sizeof(queue_element_t));
	if(new_el == NULL) { // could not allocate memory for new elements
		return Q_ERR_MEM;
	}
	new_el->data = el;
	new_el->next = NULL;
	
	if(q->sort == 0 || q->first_el == NULL) {
		// insert at the end when we don't want to sort or the queue is empty
		if(q->last_el == NULL)
			q->first_el = new_el;
		else
			q->last_el->next = new_el;
		q->last_el = new_el;
	} else {
		// search appropriate place to sort element in
		queue_element_t *s = q->first_el; // s != NULL, because of if condition above
		queue_element_t *t = NULL;
		int asc_first_el = q->asc_order != 0 && q->cmp_el(s->data, el) >= 0;
		int desc_first_el = q->asc_order == 0 && q->cmp_el(s->data, el) <= 0;
		
		if(asc_first_el == 0 && desc_first_el == 0) {
			// element will be inserted between s and t
			for(s = q->first_el, t = s->next; s != NULL && t != NULL; s = t, t = t->next) {
				if(q->asc_order != 0 && q->cmp_el(s->data, el) <= 0 && q->cmp_el(el, t->data) <= 0) {
					// asc: s <= e <= t
					break;
				} else if(q->asc_order == 0 && q->cmp_el(s->data, el) >= 0 && q->cmp_el(el, t->data) >= 0) {
					// desc: s >= e >= t
					break;
				}
			}
			// actually insert
			s->next = new_el;
			new_el->next = t;
			if(t == NULL)
				q->last_el = new_el;
		} else if(asc_first_el != 0 || desc_first_el != 0) {
			// add at front
			new_el->next = q->first_el;
			q->first_el = new_el;
		}
	}
	q->num_els++;
	// notify only one waiting thread, so that we don't have to check and fall to sleep because we were to slow
	pthread_cond_signal(q->cond_get);
	
	return Q_OK;
}

int8_t queue_get_internal(queue_t *q, void **e, int (*action)(pthread_cond_t *, pthread_mutex_t *), int (*cmp)(void *, void *), void *cmpel) {
	if(q == NULL) { // queue not valid
		*e = NULL;
		return Q_ERR_INVALID;
	}
	
	// are elements in the queue?
	if(q->num_els == 0) {
		if(action == NULL) {
			*e = NULL;
			return Q_ERR_NUM_ELEMENTS;
		} else {
			while(q->num_els == 0 && q->new_data != 0)
				action(q->cond_get, q->mutex);
			if (q->num_els == 0 && q->new_data == 0)
				return Q_ERR_NONEWDATA;
		}
	}
	
	// get first element (which fulfills the requirements)
	queue_element_t *el_prev = NULL, *el = q->first_el;
	while(cmp != NULL && el != NULL && 0 != cmp(el, cmpel)) {
		el_prev = el;
		el = el->next;
	}
	
	if(el != NULL && el_prev == NULL) {
		// first element is removed
		q->first_el = el->next;
		if(q->first_el == NULL)
			q->last_el = NULL;
		q->num_els--;
		*e = el->data;
		free(el);
	} else if(el != NULL && el_prev != NULL) {
		// element in the middle is removed
		el_prev->next = el->next;
		q->num_els--;
		*e = el->data;
		free(el);
	} else {
		// element is invalid
		*e = NULL;
		return Q_ERR_INVALID_ELEMENT;
	}
	
	// notify only one waiting thread
	pthread_cond_signal(q->cond_put);

	return Q_OK;
}
