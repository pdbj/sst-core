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

#include "sst/core/sync/threadSyncSimpleSkip.h"

#include "sst/core/event.h"
#include "sst/core/exit.h"
#include "sst/core/link.h"
#include "sst/core/simulation_impl.h"
#include "sst/core/timeConverter.h"

namespace SST {

SimTime_t ThreadSyncSimpleSkip::localMinimumNextActivityTime = 0;

/** Create a new ThreadSyncSimpleSkip object */
ThreadSyncSimpleSkip::ThreadSyncSimpleSkip(int num_threads, int thread, Simulation_impl* sim) :
    ThreadSync(),
    num_threads(num_threads),
    thread(thread),
    sim(sim),
    totalWaitTime(0.0)
{
    for ( int i = 0; i < num_threads; i++ ) {
        queues.push_back(new ThreadSyncQueue());
    }

    if ( sim->getRank().thread == 0 ) {
        barrier[0].resize(num_threads);
        barrier[1].resize(num_threads);
        barrier[2].resize(num_threads);
    }

    if ( sim->getNumRanks().rank > 1 )
        single_rank = false;
    else
        single_rank = true;

    my_max_period = sim->getInterThreadMinLatency();
    nextSyncTime  = my_max_period;
}

ThreadSyncSimpleSkip::~ThreadSyncSimpleSkip()
{
    if ( totalWaitTime > 0.0 )
        Output::getDefaultObject().verbose(
            CALL_INFO, 1, 0, "ThreadSyncSimpleSkip total wait time: %lg seconds.\n", totalWaitTime);
    for ( int i = 0; i < num_threads; i++ ) {
        delete queues[i];
    }
    queues.clear();
}

void
ThreadSyncSimpleSkip::registerLink(const std::string& name, Link* link)
{
    auto iter = link_map.find(name);
    if ( iter == link_map.end() ) {
        // I have initialized first, so just put the name and link in
        // the map
        link_map[name] = link;
    }
    else {
        // I already have the remote info, so initialize the link data
        Link* remote_link = iter->second;
        setLinkDeliveryInfo(link, reinterpret_cast<uintptr_t>(remote_link));
        link_map.erase(iter);
    }
}

ActivityQueue*
ThreadSyncSimpleSkip::registerRemoteLink(int tid, const std::string& name, Link* link)
{
    auto iter = link_map.find(name);
    if ( iter == link_map.end() ) {
        // I have initialized first, so just put the name and link in
        // the map
        link_map[name] = link;
    }
    else {
        // I already have the local info, so initialize the link data
        Link* local_link = iter->second;
        setLinkDeliveryInfo(local_link, reinterpret_cast<uintptr_t>(link));
        link_map.erase(iter);
    }
    return queues[tid];
}

void
ThreadSyncSimpleSkip::before()
{
    SimTime_t current_cycle = sim->getCurrentSimCycle();
    // Empty all the queues and send events on the links
    for ( size_t i = 0; i < queues.size(); i++ ) {
        ThreadSyncQueue*        queue = queues[i];
        std::vector<Activity*>& vec   = queue->getVector();
        for ( size_t j = 0; j < vec.size(); j++ ) {
            Event*    ev    = static_cast<Event*>(vec[j]);
            SimTime_t delay = ev->getDeliveryTime() - current_cycle;
            getDeliveryLink(ev)->send(delay, ev);
        }
        queue->clear();
    }
}

void
ThreadSyncSimpleSkip::after()
{
    // Use this nextSyncTime computation for no skip
    // nextSyncTime = sim->getCurrentSimCycle() + max_period;

    // Use this nextSyncTime computation for skipping

    auto nextmin     = sim->getLocalMinimumNextActivityTime();
    auto nextminPlus = nextmin + my_max_period;
    nextSyncTime     = nextmin > nextminPlus ? nextmin : nextminPlus;
}

void
ThreadSyncSimpleSkip::execute()
{
    totalWaitTime = barrier[0].wait();
    before();
    totalWaitTime = barrier[1].wait();
    after();
    totalWaitTime += barrier[2].wait();
}

void
ThreadSyncSimpleSkip::processLinkUntimedData()
{
    // Need to walk through all the queues and send the data to the
    // correct links
    for ( int i = 0; i < num_threads; i++ ) {
        ThreadSyncQueue*        queue = queues[i];
        std::vector<Activity*>& vec   = queue->getVector();
        for ( size_t j = 0; j < vec.size(); j++ ) {
            Event* ev = static_cast<Event*>(vec[j]);
            sendUntimedData_sync(getDeliveryLink(ev), ev);
        }
        queue->clear();
    }
}

void
ThreadSyncSimpleSkip::finalizeLinkConfigurations()
{
    for ( auto i = link_map.begin(); i != link_map.end(); ++i ) {
        finalizeConfiguration(i->second);
    }
}

void
ThreadSyncSimpleSkip::prepareForComplete()
{
    for ( auto i = link_map.begin(); i != link_map.end(); ++i ) {
        prepareForCompleteInt(i->second);
    }
}

uint64_t
ThreadSyncSimpleSkip::getDataSize() const
{
    size_t count = 0;
    return count;
}

Core::ThreadSafe::Barrier ThreadSyncSimpleSkip::barrier[3];

} // namespace SST
