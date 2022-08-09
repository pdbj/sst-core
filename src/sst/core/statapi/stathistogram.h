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

#ifndef SST_CORE_STATAPI_STATHISTOGRAM_H
#define SST_CORE_STATAPI_STATHISTOGRAM_H

#include "sst/core/sst_types.h"
#include "sst/core/statapi/statbase.h"
#include "sst/core/statapi/statoutput.h"
#include "sst/core/warnmacros.h"

#include <algorithm>   // minmax
#include <cmath>      // log10
#include <utility>    // pair


namespace SST {
namespace Statistics {

// NOTE: When calling base class members in classes derived from
//       a templated base class.  The user must use "this->" in
//       order to call base class members (to avoid a compiler
//       error) because they are "nondependant named" and the
//       templated base class is a "dependant named".  The
//       compiler will not look in dependant named base classes
//       when looking up independent names.
// See: http://www.parashift.com/c++-faq-lite/nondependent-name-lookup-members.html

/**
    \class HistogramStatistic
    Holder of data grouped into pre-determined width bins.
    \tparam BinDataType is the type of the data held in each bin (i.e. what data type described the width of the bin)
*/
template <class BinDataType>
class HistogramStatistic : public Statistic<BinDataType>
{
    using CountType = uint64_t;
    using NumBinsType = uint32_t;

public:
    SST_ELI_DECLARE_STATISTIC_TEMPLATE(
        HistogramStatistic,
        "sst",
        "HistogramStatistic",
        SST_ELI_ELEMENT_VERSION(1, 0, 0),
        "Track distribution of statistic across bins",
        "SST::Statistic<T>")

    HistogramStatistic(
        BaseComponent* comp, const std::string& statName, const std::string& statSubId, Params& statParams) :
        Statistic<BinDataType>(comp, statName, statSubId, statParams)
    {
        // Identify what keys are Allowed in the parameters
        Params::KeySet_t allowedKeySet;
        allowedKeySet.insert("minvalue");
        allowedKeySet.insert("binwidth");
        allowedKeySet.insert("numbins");
        allowedKeySet.insert("dumpbinsonoutput");
        allowedKeySet.insert("includeoutofbounds");
        allowedKeySet.insert("autoscale");
        statParams.pushAllowedKeys(allowedKeySet);

        // Process the Parameters
        m_minValue           = statParams.find<BinDataType>("minvalue", 0);
        m_binWidth           = statParams.find<BinDataType>("binwidth", 5000);
        m_numBins            = statParams.find<NumBinsType>("numbins", 100);
        m_caching            = statParams.find<bool>("autoscale", false);
        m_dumpBinsOnOutput   = statParams.find<bool>("dumpbinsonoutput", true);
        m_includeOutOfBounds = statParams.find<bool>("includeoutofbounds", true);

        // Sanity fixes
        if (m_numBins == 0) m_numBins = 1;
        if (m_binWidth == 0) m_binWidth = 1;
        if (m_binWidth < 0) m_binWidth = -m_binWidth;

        // Initialize other properties
        m_totalSummed      = 0;
        m_totalSummedSqr   = 0;
        m_OOBMinCount      = 0;
        m_OOBMaxCount      = 0;
        m_itemsBinnedCount = 0;
        this->setCollectionCount(0);
        m_cache.reserve(CACHE_SIZE);

        // Set the Name of this Statistic
        this->setStatisticTypeName("Histogram");
    }

    ~HistogramStatistic() {}

protected:
    /**
        Cache a a new value if we're still caching,
        otherwise configure the histogram.
    */
    void Cache (uint64_t N, BinDataType value)
    {
        if (!m_caching) return;

        m_cache.push_back(std::make_pair(N, value));
        this->incrementCollectionCount(N);
        const uint64_t counts = this->getCollectionCount();

        if (counts < CACHE_SIZE) return;

        // We've got enough counts to configure and populate the real histo
        // So determine histo parameters

        auto cache_compare = [](const CacheEntryType a,
                                const CacheEntryType b)
        {
            return a.second < b.second;
        };

        auto minmaxit = std::minmax_element(m_cache.begin(), m_cache.end(),
                                            cache_compare);
        BinDataType vmin = minmaxit.first->second;
        BinDataType vmax = minmaxit.second->second;
        BinDataType width{1};

        if (vmin == vmax) {
                --vmin;
                ++vmax;
                width = (vmax - vmin) / m_numBins;
            }
        else {

            // Call 2.5% of the range for overflow on each side
            BinDataType dv = (vmax - vmin) * 0.025;
            vmin += dv;
            vmax -= dv;

            // Function to push min/max to zero
            auto push_to_zero = [&vmin, &vmax](const BinDataType delta)
              {
                if (vmin >= 0 && vmin - delta < 0) vmin = 0;
                if (vmax <= 0 && vmax + delta > 0) vmax = 0;
              };
            // If we're within 5% of zero use zero
            push_to_zero (dv);

            width = (vmax - vmin) / m_numBins;

            // Round width to 1, 1.5, 2, or 5
            auto round_to_125 = [](BinDataType & value)
              {
                uint64_t jlog = std::log10 (value);  // truncate to int
                BinDataType sigfig = value * std::pow(10, -jlog);
                BinDataType siground = 0;
                if (sigfig <= 1)      siground = 1;
                else if (sigfig <= 2) siground = 2;
                else if (sigfig <= 5) siground = 5;
                else 
                  {
                    siground = 1;
                    ++ jlog;
                  }
                value = siground * std::pow(10, jlog);
              };
            round_to_125(width);
            push_to_zero(width);

            round_to_125(vmin);
            push_to_zero(width);
        }

        // Set up the histogram
        m_minValue = vmin;
        m_binWidth = width;
        // printf("DEBUG: min: %f, wid: %f\n", (double)m_minValue, (double)m_binWidth);

        // Insert cached values
        m_caching = false;
        clearStatisticData();
        for (const auto & v : m_cache) {
                addData_impl_Ntimes(v.first, v.second);
        }
        m_cache.clear();
    }

    /**
        Adds a new value to the histogram. The correct bin is identified and then incremented. If no bin can be found
        to hold the value then a new bin is created.
    */
     void addData_impl_Ntimes(uint64_t N, BinDataType value) override
    {
        if (m_caching) {
                Cache(N, value);
                return;
            }

        // Check to see if the value is above or below the min/max values
        if ( value < getBinsMinValue() ) {
            m_OOBMinCount += N;
            return;
        }
        if ( value > getBinsMaxValue() ) {
            m_OOBMaxCount += N;
            return;
        }

        // This value is to be binned...
        // Add the "in limits" value to the total summation's
        m_totalSummed += N * value;
        m_totalSummedSqr += N * (value * value);

        // Increment the Binned count (note this <= to the Statistics added Item Count)
        m_itemsBinnedCount++;
        this->incrementCollectionCount(N);

        // Figure out what the starting bin is and find it in the map
        // To support signed and unsigned values along with floating point types,
        // the calculation to find the bin_start value must be done in floating point
        // then converted to BinDataType
        double      delta     = (double)value - m_minValue;
        double      ind       = delta / m_binWidth;
        NumBinsType index     = floor(ind);
        BinDataType bin_start = m_minValue + index * m_binWidth;

        // printf("DEBUG: value = %f, delta = %f, ind = %f, index = %d, bin_start = %f, item count = %ld, \n",
        // value, delta, ind, index, (double)bin_start, getStatCollectionCount());

        HistoMapItr_t bin_itr = m_binsMap.find(bin_start);

        // Was the bin found?
        if ( bin_itr == m_binsMap.end() ) {
            // No, Create the bin and set a value of 1 to it
            m_binsMap.insert(std::pair<BinDataType, CountType>(bin_start, (CountType)N));
        }
        else {
            // Yes, Increment the specific bin's count
            bin_itr->second += N;
        }
    }

    void addData_impl(BinDataType value) override { addData_impl_Ntimes(1, value); }

private:
    /** Count how many bins are active in this histogram */
    NumBinsType getActiveBinCount() { return m_binsMap.size(); }

    /** Count how many bins are available */
    NumBinsType getNumBins() { return m_numBins; }

    /** Get the width of a bin in this histogram */
    BinDataType getBinWidth() { return m_binWidth; }

    /**
        Get the count of items in the bin by the start value (e.g. give me the count of items in the bin which begins at
       value X). \return The count of items in the bin else 0.
    */
    CountType getBinCountByBinStart(BinDataType binStartValue)
    {
        // Find the Bin Start Value in the Bin Map
        HistoMapItr_t bin_itr = m_binsMap.find(binStartValue);

        // Check to see if the Start Value was found
        if ( bin_itr == m_binsMap.end() ) {
            // No, return no count for this bin
            return (CountType)0;
        }
        else {
            // Yes, return the bin count
            return m_binsMap[binStartValue];
        }
    }

    /**
        Get the smallest start value of a bin in this histogram (i.e. the minimum value possibly represented by this
       histogram)
    */
    BinDataType getBinsMinValue() { return m_minValue; }

    /**
        Get the largest possible value represented by this histogram (i.e. the highest value in any of items bins
       rounded above to the size of the bin)
    */
    BinDataType getBinsMaxValue()
    {
        // Compute the max value based on the width * num bins offset by minvalue
        return (m_binWidth * m_numBins) + m_minValue;
    }

    /**
        Get the total number of items collected by the statistic
        \return The number of items that have been added to the statistic
    */
    CountType getStatCollectionCount()
    {
        // Get the number of items added (but not necessarily binned) to this statistic
        return this->getCollectionCount();
    }

    /**
        Get the total number of items contained in all bins
        \return The number of items contained in all bins
    */
    CountType getItemsBinnedCount()
    {
        // Get the number of items added to this statistic that were binned.
        return m_itemsBinnedCount;
    }

    /**
        Sum up every item presented for storage in the histogram
        \return The sum of all values added into the histogram
    */
    BinDataType getValuesSummed() { return m_totalSummed; }

    /**
    Sum up every squared value entered into the Histogram.
    \return The sum of all values added after squaring into the Histogram
    */
    BinDataType getValuesSquaredSummed() { return m_totalSummedSqr; }

    void clearStatisticData() override
    {
        m_totalSummed      = 0;
        m_totalSummedSqr   = 0;
        m_OOBMinCount      = 0;
        m_OOBMaxCount      = 0;
        m_itemsBinnedCount = 0;
        m_binsMap.clear();
        this->setCollectionCount(0);
    }

    void registerOutputFields(StatisticFieldsOutput* statOutput) override
    {
        // Check to see if we have registered the Startup Fields
        m_Fields.push_back(statOutput->registerField<BinDataType>("BinsMinValue"));
        m_Fields.push_back(statOutput->registerField<BinDataType>("BinsMaxValue"));
        m_Fields.push_back(statOutput->registerField<BinDataType>("BinWidth"));
        m_Fields.push_back(statOutput->registerField<NumBinsType>("TotalNumBins"));
        m_Fields.push_back(statOutput->registerField<BinDataType>("Sum"));
        m_Fields.push_back(statOutput->registerField<BinDataType>("SumSQ"));
        m_Fields.push_back(statOutput->registerField<NumBinsType>("NumActiveBins"));
        m_Fields.push_back(statOutput->registerField<CountType>("NumItemsCollected"));
        m_Fields.push_back(statOutput->registerField<CountType>("NumItemsBinned"));

        if ( true == m_includeOutOfBounds ) {
            m_Fields.push_back(statOutput->registerField<CountType>("NumOutOfBounds-MinValue"));
            m_Fields.push_back(statOutput->registerField<CountType>("NumOutOfBounds-MaxValue"));
        }

        // Do we also need to dump the bin counts on output
        if ( true == m_dumpBinsOnOutput ) {
            BinDataType binLL = getBinsMinValue();
            BinDataType binUL = binLL;

            for ( uint32_t y = 0; y < getNumBins(); y++ ) {
                // Figure out the upper and lower values for this bin
                binLL = binUL;
                binUL += getBinWidth();
                // Build the string name for this bin and add it as a field
                std::stringstream ss;
                ss << "Bin" << y << ":" << binLL << "-" << binUL;
                m_Fields.push_back(statOutput->registerField<CountType>(ss.str().c_str()));
            }
        }
    }

    void outputStatisticFields(StatisticFieldsOutput* statOutput, bool UNUSED(EndOfSimFlag)) override
    {
        uint32_t x = 0;
        statOutput->outputField(m_Fields[x++], getBinsMinValue());
        statOutput->outputField(m_Fields[x++], getBinsMaxValue());
        statOutput->outputField(m_Fields[x++], getBinWidth());
        statOutput->outputField(m_Fields[x++], getNumBins());
        statOutput->outputField(m_Fields[x++], getValuesSummed());
        statOutput->outputField(m_Fields[x++], getValuesSquaredSummed());
        statOutput->outputField(m_Fields[x++], getActiveBinCount());
        statOutput->outputField(m_Fields[x++], getStatCollectionCount());
        statOutput->outputField(m_Fields[x++], getItemsBinnedCount());

        if ( true == m_includeOutOfBounds ) {
            statOutput->outputField(m_Fields[x++], m_OOBMinCount);
            statOutput->outputField(m_Fields[x++], m_OOBMaxCount);
        }

        // Do we also need to dump the bin counts on output
        if ( true == m_dumpBinsOnOutput ) {
            BinDataType currentBinValue = getBinsMinValue();
            for ( uint32_t y = 0; y < getNumBins(); y++ ) {
                statOutput->outputField(m_Fields[x++], getBinCountByBinStart(currentBinValue));
                // Increment the currentBinValue to get the next bin
                currentBinValue += getBinWidth();
            }
        }
    }

    bool isStatModeSupported(StatisticBase::StatMode_t mode) const override
    {
        switch ( mode ) {
        case StatisticBase::STAT_MODE_COUNT:
        case StatisticBase::STAT_MODE_PERIODIC:
        case StatisticBase::STAT_MODE_DUMP_AT_END:
            return true;
        default:
            return false;
        }
        return false;
    }

private:
    // Bin Map Definition
    typedef std::map<BinDataType, CountType> HistoMap_t;

    // Iterator over the histogram bins
    typedef typename HistoMap_t::iterator HistoMapItr_t;

    // The minimum value in the Histogram
    BinDataType m_minValue;

    // The width of each Histogram bin
    BinDataType m_binWidth;

    // The number of bins to be supported
    NumBinsType m_numBins;

    // Out of bounds bins
    CountType m_OOBMinCount;
    CountType m_OOBMaxCount;

    // Count of Items that have binned, (Different than item count as some
    // items may be out of bounds and not binned)
    CountType m_itemsBinnedCount;

    // The sum of all values added into the Histogram, this is calculated and the sum of all values presented
    // to be entered into the Histogram not with bin-width multiplied by the (max-min)/2 of the bin.
    BinDataType m_totalSummed;

    // The sum of values added to the Histogram squared. Allows calculation of derivative statistic
    // values such as variance.
    BinDataType m_totalSummedSqr;

    // A map of the the bin starts to the bin counts
    HistoMap_t m_binsMap;

    // Autoscaling cache

    // Number of values to cache before histogramming
    static constexpr std::size_t CACHE_SIZE {2000};

    // Cache entry type
    typedef std::pair<uint64_t, BinDataType> CacheEntryType;

    // Autoscaling cache
    std::vector<CacheEntryType> m_cache;

    // If we're still caching
    bool m_caching;

    // Support
    std::vector<uint32_t> m_Fields;
    bool                  m_dumpBinsOnOutput;
    bool                  m_includeOutOfBounds;
};

} // namespace Statistics
} // namespace SST

#endif // SST_CORE_STATAPI_STATHISTOGRAM_H
