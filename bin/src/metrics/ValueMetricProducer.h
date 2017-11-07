/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <utils/threads.h>
#include <list>
#include "../condition/ConditionTracker.h"
#include "../external/PullDataReceiver.h"
#include "../external/StatsPullerManager.h"
#include "MetricProducer.h"
#include "frameworks/base/cmds/statsd/src/statsd_config.pb.h"

namespace android {
namespace os {
namespace statsd {

struct ValueBucket {
    int64_t mBucketStartNs;
    int64_t mBucketEndNs;
    int64_t mValue;
};

class ValueMetricProducer : public virtual MetricProducer, public virtual PullDataReceiver {
public:
    ValueMetricProducer(const ValueMetric& valueMetric, const int conditionIndex,
                        const sp<ConditionWizard>& wizard, const int pullTagId,
                        const uint64_t startTimeNs);

    virtual ~ValueMetricProducer();

    void onConditionChanged(const bool condition, const uint64_t eventTime) override;

    void finish() override;

    // TODO: Pass a timestamp as a parameter in onDumpReport.
    std::unique_ptr<std::vector<uint8_t>> onDumpReport() override;

    void onSlicedConditionMayChange(const uint64_t eventTime);

    void onDataPulled(const std::vector<std::shared_ptr<LogEvent>>& data) override;

    size_t byteSize() override;

    // TODO: Implement this later.
    virtual void notifyAppUpgrade(const string& apk, const int uid, const int version) override{};
    // TODO: Implement this later.
    virtual void notifyAppRemoved(const string& apk, const int uid) override{};

protected:
    void onMatchedLogEventInternal(const size_t matcherIndex, const HashableDimensionKey& eventKey,
                                   const std::map<std::string, HashableDimensionKey>& conditionKey,
                                   bool condition, const LogEvent& event,
                                   bool scheduledPull) override;

    void startNewProtoOutputStream(long long timestamp) override;

private:
    const ValueMetric mMetric;

    StatsPullerManager& mStatsPullerManager = StatsPullerManager::GetInstance();

    Mutex mLock;

    // tagId for pulled data. -1 if this is not pulled
    const int mPullTagId;

    // internal state of a bucket.
    typedef struct {
        std::vector<std::pair<long, long>> raw;
    } Interval;

    std::unordered_map<HashableDimensionKey, Interval> mCurrentSlicedBucket;
    // If condition is true and pulling on schedule, the previous bucket value needs to be carried
    // over to the next bucket.
    std::unordered_map<HashableDimensionKey, Interval> mNextSlicedBucket;

    // Save the past buckets and we can clear when the StatsLogReport is dumped.
    // TODO: Add a lock to mPastBuckets.
    std::unordered_map<HashableDimensionKey, std::vector<ValueBucket>> mPastBuckets;

    long get_value(const LogEvent& event);

    void flush_if_needed(const uint64_t eventTimeNs);

    size_t mByteSize;
};

}  // namespace statsd
}  // namespace os
}  // namespace android