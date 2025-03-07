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

#ifndef SST_CORE_SST_TYPES_H
#define SST_CORE_SST_TYPES_H

#include <cstdint>
#include <limits>

namespace SST {

typedef uint64_t ComponentId_t;
typedef uint64_t StatisticId_t;
typedef uint32_t LinkId_t;
typedef uint64_t HandlerId_t;
typedef uint64_t ProfileToolId_t;
typedef uint64_t Cycle_t;
typedef uint64_t SimTime_t;
typedef double   Time_t;

static constexpr StatisticId_t STATALL_ID = std::numeric_limits<StatisticId_t>::max();

#define MAX_SIMTIME_T 0xFFFFFFFFFFFFFFFFl

/* Subcomponent IDs are in the high-16 bits of the Component ID */
#define UNSET_COMPONENT_ID                      0xFFFFFFFFFFFFFFFFULL
#define UNSET_STATISTIC_ID                      0xFFFFFFFFFFFFFFFFULL
#define COMPONENT_ID_BITS                       32
#define COMPONENT_ID_MASK(x)                    ((x)&0xFFFFFFFFULL)
#define SUBCOMPONENT_ID_BITS                    16
#define SUBCOMPONENT_ID_MASK(x)                 ((x) >> COMPONENT_ID_BITS)
#define SUBCOMPONENT_ID_CREATE(compId, sCompId) ((((uint64_t)sCompId) << COMPONENT_ID_BITS) | compId)
#define CONFIG_COMPONENT_ID_BITS                (COMPONENT_ID_BITS + SUBCOMPONENT_ID_BITS)
#define CONFIG_COMPONENT_ID_MASK(x)             ((x)&0xFFFFFFFFFFFFULL)
#define STATISTIC_ID_CREATE(compId, statId)     ((((uint64_t)statId) << CONFIG_COMPONENT_ID_BITS) | compId)
#define COMPDEFINED_SUBCOMPONENT_ID_MASK(x)     ((x) >> 63)
#define COMPDEFINED_SUBCOMPONENT_ID_CREATE(compId, sCompId) \
    ((((uint64_t)sCompId) << COMPONENT_ID_BITS) | compId | 0x8000000000000000ULL)

typedef double watts;
typedef double joules;
typedef double farads;
typedef double volts;

#ifndef LIKELY
#define LIKELY(x)   __builtin_expect((int)(x), 1)
#define UNLIKELY(x) __builtin_expect((int)(x), 0)
#endif

} // namespace SST

#endif // SST_CORE_SST_TYPES_H
