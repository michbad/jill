/*
 * JILL - C++ framework for JACK
 *
 * Copyright (C) 2010 C Daniel Meliza <dmeliza@uchicago.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef _MULTICHANNEL_WRITER_HH
#define _MULTICHANNEL_WRITER_HH

#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include "../data_thread.hh"

namespace jill {

namespace dsp { class period_ringbuffer; }

namespace file {
/**
 * A trivial implementation of the data thread that just stores data in a
 * ringbuffer and then throws it away. A useful base for more complex tasks.
 */
class multichannel_writer : public data_thread {

public:
        multichannel_writer(nframes_t buffer_size=4096);
        virtual ~multichannel_writer();

        nframes_t push(void const * arg, period_info_t const & info);
        void xrun();
        void stop();
        void start();
        void join();

	/// @return the number of complete periods that can be stored. wait-free
        nframes_t write_space(nframes_t nframes) const;

        /**
         * Resize the ringbuffer. Determines the best size based on period size
         * and number of channels in the data. Only takes effect if the new size
         * is larger than the current size.  The actual size may be larger due
         * to constraints on the underlying storage mechanism.
         *
         * Deriving classes may override this method to provide different
         * estimates of the appropriate buffer size.
         *
         * Blocks until the write thread has emptied the buffer. If data is
         * being added to the buffer by a realtime thread this may take an
         * extremely long time.
         *
         * @param period_size   the period size
         * @param period_rate   expected rate of periods/sec
         * @return the new capacity of the buffer (in samples)
         */
        virtual nframes_t resize_buffer(nframes_t period_size, nframes_t period_rate);

protected:
        /**
         * Entry point for deriving classes to actually so something with the
         * data pulled off the ringbuffer. Deriving classes *must* release data
         * using _buffer->release() when they are done with the data or the buffer will
         * overrun.
         *
         * @param info   the header and data for the period
         */
        virtual void write(period_info_t const * info);

        static void * thread(void * arg);           // the thread entry point

        pthread_t _thread_id;                      // thread id
        pthread_mutex_t _lock;                     // mutex for disk operations
        pthread_cond_t  _ready;                    // indicates data ready
        int _stop;                                 // stop flag
        int _xruns;                                // xrun counter

        boost::shared_ptr<jill::dsp::period_ringbuffer> _buffer;     // ringbuffer
};

}} // jill::file

#endif
