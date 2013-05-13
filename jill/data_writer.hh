#ifndef _DATA_WRITER_HH
#define _DATA_WRITER_HH

#include <boost/weak_ptr.hpp>
#include "event_logger.hh"
#include "types.hh"


namespace jill {

class data_source;

/**
 * ABC for classes that write multichannel sampled and event data to disk. Data
 * are assumed to be organized in one or more entries, each containing zero or
 * more channels which share a common start time.
 */
class data_writer : public event_logger {

public:
        ~data_writer() {}

        /**
         * Create a new entry, closing the previous one if necessary.
         *
         * @param frame   the frame index at the start of the entry
         */
        virtual void new_entry(nframes_t frame) = 0;

        /** Close the current entry */
        virtual void close_entry() = 0;

        /** true if an entry is open for recording */
        virtual bool ready() const = 0;

        /**
         * true if the same amount of data has been written to all channels
         * and at least one full period has been written.
         */
        virtual bool aligned() const = 0;

        /** Store a record that an xrun occurred in the file */
        virtual void xrun() = 0;

        /**
         * Provide the writer with a pointer to an object that can give
         * samplerate and time information.  This is optional.
         */
        virtual void set_data_source(boost::weak_ptr<data_source> data_source) {}

        /**
         * Write a period to disk. Looks up the appropriate channel.
         *
         * @pre ready() is true
         *
         * @param info  pointer to header and data for period
         * @param start if nonzero, only write frames >= start
         * @param stop  if nonzero, only write frames < stop. okay if stop > info->nframes
         *
         * @return the number of frames written
         */
        virtual nframes_t write(period_info_t const * info, nframes_t start=0, nframes_t stop=0) = 0;

        /**
         * Request data to be flushed disk. Implementing classes must flush data
         * to disk on cleanup or at appropriate intervals, but this function is
         * provided so callers can request a flush when the system load is light.
         */
        virtual void flush() {}

};

}
#endif