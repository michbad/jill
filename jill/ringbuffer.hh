/*
 * JILL - C++ framework for JACK
 *
 * includes code from klick, Copyright (C) 2007-2009  Dominic Sacre  <dominic.sacre@gmx.de>
 * based on JACK ringbuffer.h/c  Copyright (C) 2000 Paul Davis Copyright (C) 2003 Rohan Drape
 * additions Copyright (C) 2010-2012 C Daniel Meliza <dmeliza@uchicago.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef _RINGBUFFER_HH
#define _RINGBUFFER_HH

#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <sys/mman.h>
#include <stdexcept>

/**
 * @defgroup buffergroup Buffer data in a thread-safe manner
 *
 * In a running JACK application there will be at least two threads
 * running.  The main thread of the application is responsible for
 * setting up the client, connecting it to the right ports, and any
 * other setup and shutdown tasks.  Once the main thread registers a
 * process callback (SimpleClient::set_process_callback), this
 * callback will run independently, typically at a much higher
 * priority.  Often it's necessary to move data between these threads,
 * and the classic data structure for doing this is a ringbuffer.
 * JACK provides a thread-safe, lock-free ringbuffer.  Classes in this
 * group provide interfaces to the ringbuffer, and adapters for using
 * the ringbuffer with various output formats.
 *
 * A note on resource management. For native and aggregate/POD datatypes this is
 * not relevant. However, if the objects contain references to other memory,
 * that memory needs to be allocated by the object on construction (which will
 * occurw hen the ringbuffer is initialized). Then, when objects are pushed to
 * the ringbuffer, the referenced data needs to be copied into the preallocated
 * memory. The push() method in Ringbuffer facilitates this by using std::copy,
 * which will use the object's assignment operator. Similarly, the pull() method
 * uses std::copy, to avoid allocation, to avoid having the memory
 * overwritten by the writer thread after the objects are released by the reader
 * thread, and to avoid double freeing memory when the ringbuffer is destroyed.
 */


namespace jill {

/**
 * @ingroup buffergroup
 * @brief a lockfree ringbuffer
 *
 *  Many JILL applications will access an audio stream in both the
 *  real-time thread and a lower-priority main thread.  This class
 *  is based on JACK's ringbuffer, with the following changes:
 *
 *  1) memory is always mlocked to avoid paging
 *  2) data type is set as a template argument to simplify pointer arithmetic
 *     and avoid byteshift corruptions
 *  3) instead of cumbersome peek functions, uses a visitor paradigm
 *
 *  @param T the type of object to store in the ringbuffer. Should be POD.
 */
template<typename T>
class Ringbuffer : boost::noncopyable
{
public:
	typedef T data_type;
	typedef std::size_t size_type;
	typedef typename boost::function<size_type (data_type const * src, size_type cnt)> visitor_type;

	/**
	 * Construct a ringbuffer with enough room to hold @a size
	 * objects of type T.
	 *
	 * @param size The size of the ringbuffer (in objects)
	 */
	explicit Ringbuffer(size_type size);

	virtual ~Ringbuffer();

	/// @return the number of items that can be written to the ringbuffer
	size_type write_space() const;

	/// @return the number of items that can be read from the ringbuffer
	size_type read_space() const;

	/** @return the total size of the ringbuffer. may be larger than the space requested */
	size_type size() const { return _size;}

	/**
	 * Write data to the ringbuffer.  Uses std::copy, so object assignment
	 * operator semantics matter. Specifically, make sure objects in the
	 * ringbuffer own their resources.
	 *
	 * @param src Pointer to source buffer
	 * @param cnt The number of elements in the source buffer
	 *
	 * @return The number of elements actually written
	 */
	size_type push(data_type const * src, size_type cnt);
	size_type push(data_type const & src) { return push(&src, 1); }

	/**
	 * Read data from the ringbuffer. This version of the function
	 * copies data to a destination buffer.  In contrast to push, this
	 * performs a shallow copy (using memcpy), so the
	 *
	 * @param dest the destination buffer, which needs to be pre-allocated
	 * @param cnt the number of elements to read (0 for all)
	 *
	 * @return the number of elements actually read
	 */
	size_type pop(data_type *dest, size_type cnt=0);

	/**
	 * Read data from the ringbuffer using a visitor function.
	 *
	 * @param data_fun The visitor function. NB: to avoid copying
	 *                 the underlying object use boost::ref
	 * @param cnt      The number of elements to process, or 0 for all
	 * (@see visitor_type). Note that the function is passed by
	 * @return the number of elements actually read
	 */
	size_type pop(visitor_type data_fun, size_type cnt=0);

	/**
	 * Advance the read pointer by @a cnt, or up to the write
	 * pointer, whichever is less.
	 *
	 * @param cnt   The number of elements to advance, or 0 to advance
	 *                  up to the write pointer
	 * @return the number of elements actually advanced
	 */
	size_type advance(size_type cnt=0);

	/**
	 * Advance the read pointer until at least @a cnt elements
	 * remain in the buffer. Useful for maintaining a
	 * prebuffer. If read_size() < @a cnt, does nothing.  Note
	 * that additional samples may be added to the buffer by the
	 * writer thread during the flush, in which case read_size() >
	 * @a cnt.
	 *
	 * @param cnt The number of elements to keep
	 * @return The number of elements flushed
	 */
	size_type flush(size_type cnt);

private:
	data_type *_buf;
	volatile size_type _write_ptr;
	volatile size_type _read_ptr;
	size_type	  _size;
	size_type	  _size_mask;
};


/* IMPLEMENTATIONS */

template <typename T>
Ringbuffer<T>::Ringbuffer(size_type size)
   : _write_ptr(0), _read_ptr(0)
{
	unsigned int power_of_two;
	for (power_of_two = 1; 1U << power_of_two < size; power_of_two++);
	_size = 1U << power_of_two;
	_size_mask = _size - 1;
	_buf = new data_type[_size];

	if (mlock (_buf, _size))
		throw std::runtime_error("Error mlocking ringbuffer");
}

template <typename T>
Ringbuffer<T>::~Ringbuffer()
{
	munlock(_buf, _size);
	delete[] _buf;
}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::read_space() const
{
	size_type w = _write_ptr;
	size_type r = _read_ptr;

	if (w > r)
		return w - r;
	else if (w < r)
		return (w - r + _size) & _size_mask;
	else
		return 0;
}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::write_space() const
{
	size_type w = _write_ptr;
	size_type r = _read_ptr;

	if (w > r)
		return ((r - w + _size) & _size_mask) - 1;
	else if (w < r)
		return (r - w) - 1;
	else
		return _size - 1;
}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::advance(size_type cnt)
{
	cnt = (cnt==0) ? read_space() : std::min(read_space(), cnt);
	_read_ptr = (_read_ptr + cnt) & _size_mask;
	return cnt;
}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::flush(size_type cnt)
{
	size_type to_flush = read_space() - cnt;
	if (to_flush <= 0)
		return 0;
	_read_ptr = (_read_ptr + to_flush) & _size_mask;
	return to_flush;
}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::push(data_type const * src, size_type cnt)
{
	size_type free_cnt = write_space();
	if (free_cnt == 0) return 0;

	size_type to_write = cnt > free_cnt ? free_cnt : cnt;
	size_type cnt2 = _write_ptr + to_write;

	size_type n1, n2;
	if (cnt2 > _size) {
		n1 = _size - _write_ptr;
		n2 = cnt2 & _size_mask;
	} else {
		n1 = to_write;
		n2 = 0;
	}

        std::copy (src, src + n1, _buf + _write_ptr);
	_write_ptr = (_write_ptr + n1) & _size_mask; // FIXME: IS THIS SAFE?

	if (n2) {
                std::copy (src + n1, src + n1 + n2, _buf + _write_ptr);
		_write_ptr = (_write_ptr + n2) & _size_mask;
	}

	return to_write;
}

namespace detail {

template <typename T>
struct memcopier {
	typedef T data_type;
	typedef std::size_t size_type;

	data_type* _dest;
	memcopier(data_type * dest) : _dest(dest) {}
	size_type operator() (data_type const * src, size_type cnt) {
                std::copy(src, src + cnt, _dest); // assume uses memcpy for POD
                return cnt;
	}
};

}

template<typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::pop(data_type * dst, size_type cnt)
{
	detail::memcopier<data_type> copier(dst);
	return pop(copier, cnt);
}

template <typename T>
typename Ringbuffer<T>::size_type
Ringbuffer<T>::pop(visitor_type data_fun, size_type cnt)
{
	size_type to_read = (cnt==0) ? read_space() : std::min(read_space(), cnt);
	if (to_read==0) return 0;

	size_type cnt2 = _read_ptr + to_read;
	size_type n1, n2;
	if (cnt2 > _size) {
		n1 = _size - _read_ptr;
		n2 = cnt2 & _size_mask;
	} else {
		n1 = to_read;
		n2 = 0;
	}

	data_fun (_buf + _read_ptr, n1);
	_read_ptr = (_read_ptr + n1) & _size_mask;

	if (n2) {
		data_fun (_buf + _read_ptr, n2);
		_read_ptr = (_read_ptr + n2) & _size_mask;
	}

	return to_read;
}


} // namespace jill


#endif