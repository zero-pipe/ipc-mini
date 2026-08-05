#ifndef check_interval_include_h
#define check_interval_include_h

namespace zero_ipc{namespace util{

    class check_interval
    {
        public:
            check_interval()
            {
                beg_t = get_t();
            }

            ~check_interval()
            {
            }

            void set_beg()
            {
                beg_t = get_t();
            }

            int64_t interval()
            {
               return get_t() - beg_t;
            }

        private:
            uint64_t get_t()
            {
                struct timespec spec_now;
                clock_gettime(CLOCK_MONOTONIC,&spec_now);
                
                return (uint64_t)spec_now.tv_sec * 1000 + (uint64_t)spec_now.tv_nsec / 1000000;
            }

            int64_t beg_t;
    };

}}//namespace

#endif

