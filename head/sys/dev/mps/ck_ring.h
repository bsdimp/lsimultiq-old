/*
 * Copyright 2009-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_RING_H
#define CK_RING_H

#include <machine/atomic.h>

/*
 * Concurrent ring buffer.
 */
#define CK_MD_CACHELINE	64
struct ck_ring {
	volatile u_int c_head;
	char pad[CK_MD_CACHELINE - sizeof(unsigned int)];
	volatile u_int p_tail;
	char _pad[CK_MD_CACHELINE - sizeof(unsigned int)];
	unsigned int size;
	unsigned int mask;
};
typedef struct ck_ring ck_ring_t;

struct ck_ring_buffer {
	void *value;
};
typedef struct ck_ring_buffer ck_ring_buffer_t;

__inline static unsigned int
ck_ring_size(struct ck_ring *ring)
{
	unsigned int c, p;

	c = atomic_load_acq_int(&ring->c_head);
	p = atomic_load_acq_int(&ring->p_tail);
	return (p - c) & ring->mask;
}

__inline static unsigned int
ck_ring_capacity(const struct ck_ring *ring)
{
	return ring->size;
}

/*
 * Atomically enqueues the specified entry. Returns true on success, returns
 * false if the ck_ring is full. This operation only support one active
 * invocation at a time and works in the presence of a concurrent invocation
 * of ck_ring_dequeue_spsc.
 *
 * This variant of ck_ring_enqueue_spsc returns the snapshot of queue length
 * with respect to the linearization point. This can be used to extract ring
 * size without incurring additional cacheline invalidation overhead from the
 * writer.
 */
__inline static bool
_ck_ring_enqueue_spsc_size(struct ck_ring *ring,
    void *restrict buffer,
    const void *restrict entry,
    unsigned int type_size,
    unsigned int *size)
{
	unsigned int consumer, producer, delta;
	unsigned int mask = ring->mask;

	consumer = atomic_load_acq_int(&ring->c_head);
	producer = ring->p_tail;
	delta = producer + 1;
	*size = (producer - consumer) & mask;

	if ((delta & mask) == (consumer & mask))
		return false;

	buffer = (char *)buffer + type_size * (producer & mask);
	memcpy(buffer, entry, type_size);

	/*
	 * Make sure to update slot value before indicating
	 * that the slot is available for consumption.
	 */
	atomic_thread_fence_rel();
	atomic_store_rel_int(&ring->p_tail, delta);
	return true;
}

__inline static bool
ck_ring_enqueue_spsc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *entry,
    unsigned int *size)
{

	return _ck_ring_enqueue_spsc_size(ring, buffer, &entry,
	    sizeof(void *), size);
}

/*
 * Atomically enqueues the specified entry. Returns true on success, returns
 * false if the ck_ring is full. This operation only support one active
 * invocation at a time and works in the presence of a concurrent invocation
 * of ck_ring_dequeue_spsc.
 */
__inline static bool
_ck_ring_enqueue_spsc(struct ck_ring *ring,
    void *restrict destination,
    const void *restrict source,
    unsigned int size)
{
	unsigned int consumer, producer, delta;
	unsigned int mask = ring->mask;

	consumer = atomic_load_acq_int(&ring->c_head);
	producer = ring->p_tail;
	delta = producer + 1;

	if ((delta & mask) == (consumer & mask))
		return false;

	destination = (char *)destination + size * (producer & mask);
	memcpy(destination, source, size);

	/*
	 * Make sure to update slot value before indicating
	 * that the slot is available for consumption.
	 */
	atomic_thread_fence_rel();
	atomic_store_rel_int(&ring->p_tail, delta);
	return true;
}

__inline static bool
ck_ring_enqueue_spsc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    const void *entry)
{

	return _ck_ring_enqueue_spsc(ring, buffer,
	    &entry, sizeof(entry));
}

/*
 * Single consumer and single producer ring buffer dequeue (consumer).
 */
__inline static bool
_ck_ring_dequeue_spsc(struct ck_ring *ring,
    void *restrict buffer,
    void *restrict target,
    unsigned int size)
{
	unsigned int consumer, producer;
	unsigned int mask = ring->mask;

	consumer = ring->c_head;
	producer = atomic_load_acq_int(&ring->p_tail);

	if (consumer == producer)
		return false;

	/*
	 * Make sure to serialize with respect to our snapshot
	 * of the producer counter.
	 */
	atomic_thread_fence_acq();

	buffer = (char *)buffer + size * (consumer & mask);
	memcpy(target, buffer, size);

	/*
	 * Make sure copy is completed with respect to consumer
	 * update.
	 */
	atomic_thread_fence_rel();
	atomic_store_rel_int(&ring->c_head, consumer + 1);
	return true;
}

__inline static bool
ck_ring_dequeue_spsc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_spsc(ring, buffer,
	    data, sizeof(void *));
}

/*
 * Atomically enqueues the specified entry. Returns true on success, returns
 * false if the ck_ring is full. This operation only support one active
 * invocation at a time and works in the presence of up to UINT_MAX concurrent
 * invocations of ck_ring_dequeue_spmc.
 *
 * This variant of ck_ring_enqueue_spmc returns the snapshot of queue length
 * with respect to the linearization point. This can be used to extract ring
 * size without incurring additional cacheline invalidation overhead from the
 * writer.
 */
__inline static bool
ck_ring_enqueue_spmc_size(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *entry,
    unsigned int *size)
{

	return ck_ring_enqueue_spsc_size(ring, buffer,
	    entry, size);
}

/*
 * Atomically enqueues the specified entry. Returns true on success, returns
 * false if the ck_ring is full. This operation only support one active
 * invocation at a time and works in the presence of up to UINT_MAX concurrent
 * invocations of ck_ring_dequeue_spmc.
 */
__inline static bool
ck_ring_enqueue_spmc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *entry)
{

	return ck_ring_enqueue_spsc(ring, buffer, entry);
}

__inline static bool
_ck_ring_trydequeue_spmc(struct ck_ring *ring,
    void *restrict buffer,
    void *data,
    unsigned int size)
{
	unsigned int consumer, producer;
	unsigned int mask = ring->mask;

	consumer = atomic_load_acq_int(&ring->c_head);
	atomic_thread_fence_acq();
	producer = atomic_load_acq_int(&ring->p_tail);

	if (consumer == producer)
		return false;

	atomic_thread_fence_acq();

	buffer = (char *)buffer + size * (consumer & mask);
	memcpy(data, buffer, size);

	atomic_thread_fence_rel();
	return atomic_cmpset_int(&ring->c_head, consumer, consumer + 1);
}

__inline static bool
ck_ring_trydequeue_spmc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_trydequeue_spmc(ring,
	    buffer, data, sizeof(void *));
}

__attribute__((noinline)) static bool
_ck_ring_dequeue_spmc(struct ck_ring *ring,
    void *buffer,
    void *data,
    unsigned int size)
{
	unsigned int consumer, producer;
	unsigned int mask = ring->mask;
	char *target;

	do {
		/*
		 * Producer counter must represent state relative to
		 * our latest consumer snapshot.
		 */
		consumer = atomic_load_acq_int(&ring->c_head);
		atomic_thread_fence_acq();
		producer = atomic_load_acq_int(&ring->p_tail);

		if (consumer == producer)
			return false;

		atomic_thread_fence_acq();

		target = (char *)buffer + size * (consumer & mask);
		memcpy(data, target, size);

		/* Serialize load with respect to head update. */
		atomic_thread_fence_rel();
	} while (atomic_cmpset_int(&ring->c_head, consumer, consumer + 1) == 0);
	return true;
}

__inline static bool
ck_ring_dequeue_spmc(struct ck_ring *ring,
    struct ck_ring_buffer *buffer,
    void *data)
{

	return _ck_ring_dequeue_spmc(ring, buffer, data,
	    sizeof(void *));
}

__inline static void
ck_ring_init(struct ck_ring *ring, unsigned int size)
{

	ring->size = size;
	ring->mask = size - 1;
	ring->p_tail = 0;
	ring->c_head = 0;
	return;
}

#define CK_RING_PROTOTYPE(name, type)			\
__inline static bool				\
ck_ring_enqueue_spsc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_spsc_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
__inline static bool				\
ck_ring_enqueue_spsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_spsc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
__inline static bool				\
ck_ring_dequeue_spsc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_spsc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
__inline static bool				\
ck_ring_enqueue_spmc_size_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c,					\
    unsigned int *d)					\
{							\
							\
	return _ck_ring_enqueue_spsc_size(a, b, c,	\
	    sizeof(struct type), d);			\
}							\
							\
							\
__inline static bool				\
ck_ring_enqueue_spmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_enqueue_spsc(a, b, c,		\
	    sizeof(struct type));			\
}							\
							\
__inline static bool				\
ck_ring_trydequeue_spmc_##name(struct ck_ring *a,	\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_trydequeue_spmc(a,		\
	    b, c, sizeof(struct type));			\
}							\
							\
__inline static bool				\
ck_ring_dequeue_spmc_##name(struct ck_ring *a,		\
    struct type *b,					\
    struct type *c)					\
{							\
							\
	return _ck_ring_dequeue_spmc(a, b, c,		\
	    sizeof(struct type));			\
}

#define CK_RING_ENQUEUE_SPSC(name, a, b, c)		\
	ck_ring_enqueue_spsc_##name(a, b, c)
#define CK_RING_ENQUEUE_SPSC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_spsc_size_##name(a, b, c, d)
#define CK_RING_DEQUEUE_SPSC(name, a, b, c)		\
	ck_ring_dequeue_spsc_##name(a, b, c)
#define CK_RING_ENQUEUE_SPMC(name, a, b, c)		\
	ck_ring_enqueue_spmc_##name(a, b, c)
#define CK_RING_ENQUEUE_SPMC_SIZE(name, a, b, c, d)	\
	ck_ring_enqueue_spmc_size_##name(a, b, c, d)
#define CK_RING_TRYDEQUEUE_SPMC(name, a, b, c)		\
	ck_ring_trydequeue_spmc_##name(a, b, c)
#define CK_RING_DEQUEUE_SPMC(name, a, b, c)		\
	ck_ring_dequeue_spmc_##name(a, b, c)

#endif /* CK_RING_H */
