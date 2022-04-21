// Copyright 2009-2022 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2022, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"

#include "sst/core/clock.h"

#include "sst/core/factory.h"
#include "sst/core/simulation_impl.h"
#include "sst/core/timeConverter.h"

#include <sys/time.h>

namespace SST {

#if SST_CLOCK_PROFILING

#if SST_HIGH_RESOLUTION_CLOCK

#define SST_CLOCK_PROFILE_START \
    auto sst_clock_profile_start = std::chrono::high_resolution_clock::now()

#define SST_CLOCK_PROFILE_STOP                                                                                   \
    auto sst_clock_profile_finish = std::chrono::high_resolution_clock::now();                                   \
    auto sst_clock_profile_count = \
      std::chrono::duration_cast<std::chrono::nanoseconds>(sst_clock_profile_finish - sst_clock_profile_start).count(); \
    sim->incrementClockCounters(handler, sst_clock_profile_count)

#else

#define SST_CLOCK_PROFILE_START                     \
    struct timeval clockStart, clockEnd, clockDiff; \
    gettimeofday(&clockStart, NULL)

#define SST_CLOCK_PROFILE_STOP                                             \
    gettimeofday(&clockEnd, NULL);                                         \
    timersub(&clockEnd, &clockStart, &clockDiff);                          \
    auto sst_clock_profile_count = clockDiff.tv_usec + clockDiff.tv_sec * 1e6; \
    sim->incrementClockCounters(handler, sst_clock_profile_count)
#endif
#else
#define SST_CLOCK_PROFILE_START
#define SST_CLOCK_PROFILE_STOP
#endif

Clock::Clock(TimeConverter* period, int priority) : Action(), currentCycle(0), period(period), scheduled(false)
{
    setPriority(priority);
}

Clock::~Clock()
{
    // Delete all the handlers
    for ( StaticHandlerMap_t::iterator it = staticHandlerMap.begin(); it != staticHandlerMap.end(); ++it ) {
        delete *it;
    }
    staticHandlerMap.clear();
}

bool
Clock::registerHandler(Clock::HandlerBase* handler)
{
    staticHandlerMap.push_back(handler);
    if ( !scheduled ) { schedule(); }
    return 0;
}

bool
Clock::unregisterHandler(Clock::HandlerBase* handler, bool& empty)
{

    StaticHandlerMap_t::iterator iter = staticHandlerMap.begin();

    for ( ; iter != staticHandlerMap.end(); iter++ ) {
        if ( *iter == handler ) {
            staticHandlerMap.erase(iter);
            break;
        }
    }

    empty = staticHandlerMap.empty();

    return 0;
}

Cycle_t
Clock::getNextCycle()
{
    return currentCycle + 1;
    // return period->convertFromCoreTime(next);
}

void
Clock::execute(void)
{
    Simulation_impl* sim = Simulation_impl::getSimulation();

    if ( staticHandlerMap.empty() ) {
        scheduled = false;
        return;
    }

    // Derive the current cycle from the core time
    // currentCycle = period->convertFromCoreTime(sim->getCurrentSimCycle());
    currentCycle++;

    StaticHandlerMap_t::iterator sop_iter;
    for ( sop_iter = staticHandlerMap.begin(); sop_iter != staticHandlerMap.end(); ) {
        Clock::HandlerBase* handler = *sop_iter;

        SST_CLOCK_PROFILE_START;

	bool done = (*handler)(currentCycle);

        SST_CLOCK_PROFILE_STOP;

        if ( done )
            sop_iter = staticHandlerMap.erase(sop_iter);
        else
            ++sop_iter;
    }

    next = sim->getCurrentSimCycle() + period->getFactor();
    sim->insertActivity(next, this);

    return;
}

void
Clock::schedule()
{
    Simulation_impl* sim = Simulation_impl::getSimulation();
    currentCycle         = sim->getCurrentSimCycle() / period->getFactor();
    SimTime_t next       = (currentCycle * period->getFactor()) + period->getFactor();

    // Check to see if we need to insert clock into queue at current
    // simtime.  This happens if the clock would have fired at this
    // tick and if the current priority is less than my priority.
    // However, if we are at time = 0, then we always go out to the
    // next cycle;
    if ( sim->getCurrentPriority() < getPriority() && sim->getCurrentSimCycle() != 0 ) {
        if ( sim->getCurrentSimCycle() % period->getFactor() == 0 ) { next = sim->getCurrentSimCycle(); }
    }

    // std::cout << "Scheduling clock " << period->getFactor() << " at cycle " << next << " current cycle is " <<
    // sim->getCurrentSimCycle() << std::endl;
    sim->insertActivity(next, this);
    scheduled = true;
}

std::string
Clock::toString() const
{
    std::stringstream buf;
    buf << "Clock Activity with period " << period->getFactor() << " to be delivered at " << getDeliveryTime()
        << " with priority " << getPriority() << " with " << staticHandlerMap.size() << " items on clock list";
    return buf.str();
}

} // namespace SST
