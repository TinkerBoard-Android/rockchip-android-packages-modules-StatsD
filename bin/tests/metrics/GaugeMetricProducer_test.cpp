// Copyright (C) 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/metrics/GaugeMetricProducer.h"
#include "src/stats_log_util.h"
#include "logd/LogEvent.h"
#include "metrics_test_helper.h"
#include "tests/statsd_test_util.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <vector>

using namespace testing;
using android::sp;
using std::set;
using std::unordered_map;
using std::vector;
using std::make_shared;

#ifdef __ANDROID__

namespace android {
namespace os {
namespace statsd {

const ConfigKey kConfigKey(0, 12345);
const int tagId = 1;
const int64_t metricId = 123;
const int64_t bucketStartTimeNs = 10000000000;
const int64_t bucketSizeNs = TimeUnitToBucketSizeInMillis(ONE_MINUTE) * 1000000LL;
const int64_t bucket2StartTimeNs = bucketStartTimeNs + bucketSizeNs;
const int64_t bucket3StartTimeNs = bucketStartTimeNs + 2 * bucketSizeNs;
const int64_t bucket4StartTimeNs = bucketStartTimeNs + 3 * bucketSizeNs;

TEST(GaugeMetricProducerTest, TestNoCondition) {
    GaugeMetric metric;
    metric.set_id(metricId);
    metric.set_bucket(ONE_MINUTE);
    metric.mutable_gauge_fields_filter()->set_include_all(false);
    auto gaugeFieldMatcher = metric.mutable_gauge_fields_filter()->mutable_fields();
    gaugeFieldMatcher->set_field(tagId);
    gaugeFieldMatcher->add_child()->set_field(1);
    gaugeFieldMatcher->add_child()->set_field(3);

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    // TODO: pending refactor of StatsPullerManager
    // For now we still need this so that it doesn't do real pulling.
    shared_ptr<MockStatsPullerManager> pullerManager =
            make_shared<StrictMock<MockStatsPullerManager>>();
    EXPECT_CALL(*pullerManager, RegisterReceiver(tagId, _, _)).WillOnce(Return());
    EXPECT_CALL(*pullerManager, UnRegisterReceiver(tagId, _)).WillOnce(Return());

    GaugeMetricProducer gaugeProducer(kConfigKey, metric, -1 /*-1 meaning no condition*/, wizard,
                                      tagId, bucketStartTimeNs, pullerManager);

    vector<shared_ptr<LogEvent>> allData;
    allData.clear();
    shared_ptr<LogEvent> event = make_shared<LogEvent>(tagId, bucket2StartTimeNs + 1);
    event->write(10);
    event->write("some value");
    event->write(11);
    event->init();
    allData.push_back(event);

    gaugeProducer.onDataPulled(allData);
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    auto it = gaugeProducer.mCurrentSlicedBucket->begin()->second->begin();
    EXPECT_EQ(10, it->second.value_int());
    it++;
    EXPECT_EQ(11, it->second.value_int());
    EXPECT_EQ(0UL, gaugeProducer.mPastBuckets.size());

    allData.clear();
    std::shared_ptr<LogEvent> event2 =
            std::make_shared<LogEvent>(tagId, bucket3StartTimeNs + 10);
    event2->write(24);
    event2->write("some value");
    event2->write(25);
    event2->init();
    allData.push_back(event2);
    gaugeProducer.onDataPulled(allData);
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    it = gaugeProducer.mCurrentSlicedBucket->begin()->second->begin();
    EXPECT_EQ(24, it->second.value_int());
    it++;
    EXPECT_EQ(25, it->second.value_int());
    // One dimension.
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.size());
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.begin()->second.size());
    it = gaugeProducer.mPastBuckets.begin()->second.back().mGaugeFields->begin();
    EXPECT_EQ(10L, it->second.value_int());
    it++;
    EXPECT_EQ(11L, it->second.value_int());
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.begin()->second.back().mBucketNum);

    gaugeProducer.flushIfNeededLocked(bucket4StartTimeNs);
    EXPECT_EQ(0UL, gaugeProducer.mCurrentSlicedBucket->size());
    // One dimension.
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.size());
    EXPECT_EQ(2UL, gaugeProducer.mPastBuckets.begin()->second.size());
    it = gaugeProducer.mPastBuckets.begin()->second.back().mGaugeFields->begin();
    EXPECT_EQ(24L, it->second.value_int());
    it++;
    EXPECT_EQ(25L, it->second.value_int());
    EXPECT_EQ(2UL, gaugeProducer.mPastBuckets.begin()->second.back().mBucketNum);
}

TEST(GaugeMetricProducerTest, TestWithCondition) {
    GaugeMetric metric;
    metric.set_id(metricId);
    metric.set_bucket(ONE_MINUTE);
    auto gaugeFieldMatcher = metric.mutable_gauge_fields_filter()->mutable_fields();
    gaugeFieldMatcher->set_field(tagId);
    gaugeFieldMatcher->add_child()->set_field(2);
    metric.set_condition(StringToId("SCREEN_ON"));

    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    shared_ptr<MockStatsPullerManager> pullerManager =
            make_shared<StrictMock<MockStatsPullerManager>>();
    EXPECT_CALL(*pullerManager, RegisterReceiver(tagId, _, _)).WillOnce(Return());
    EXPECT_CALL(*pullerManager, UnRegisterReceiver(tagId, _)).WillOnce(Return());
    EXPECT_CALL(*pullerManager, Pull(tagId, _))
            .WillOnce(Invoke([](int tagId, vector<std::shared_ptr<LogEvent>>* data) {
                data->clear();
                shared_ptr<LogEvent> event = make_shared<LogEvent>(tagId, bucketStartTimeNs + 10);
                event->write("some value");
                event->write(100);
                event->init();
                data->push_back(event);
                return true;
            }));

    GaugeMetricProducer gaugeProducer(kConfigKey, metric, 1, wizard, tagId,
                                      bucketStartTimeNs, pullerManager);

    gaugeProducer.onConditionChanged(true, bucketStartTimeNs + 8);
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_EQ(100,
        gaugeProducer.mCurrentSlicedBucket->begin()->second->begin()->second.value_int());
    EXPECT_EQ(0UL, gaugeProducer.mPastBuckets.size());

    vector<shared_ptr<LogEvent>> allData;
    allData.clear();
    shared_ptr<LogEvent> event = make_shared<LogEvent>(tagId, bucket2StartTimeNs + 1);
    event->write("some value");
    event->write(110);
    event->init();
    allData.push_back(event);
    gaugeProducer.onDataPulled(allData);

    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_EQ(110,
        gaugeProducer.mCurrentSlicedBucket->begin()->second->begin()->second.value_int());
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.size());
    EXPECT_EQ(100, gaugeProducer.mPastBuckets.begin()->second.back()
        .mGaugeFields->begin()->second.value_int());

    gaugeProducer.onConditionChanged(false, bucket2StartTimeNs + 10);
    gaugeProducer.flushIfNeededLocked(bucket3StartTimeNs + 10);
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.size());
    EXPECT_EQ(2UL, gaugeProducer.mPastBuckets.begin()->second.size());
    EXPECT_EQ(110L, gaugeProducer.mPastBuckets.begin()->second.back()
        .mGaugeFields->begin()->second.value_int());
    EXPECT_EQ(1UL, gaugeProducer.mPastBuckets.begin()->second.back().mBucketNum);
}

TEST(GaugeMetricProducerTest, TestAnomalyDetection) {
    sp<MockConditionWizard> wizard = new NaggyMock<MockConditionWizard>();

    shared_ptr<MockStatsPullerManager> pullerManager =
            make_shared<StrictMock<MockStatsPullerManager>>();
    EXPECT_CALL(*pullerManager, RegisterReceiver(tagId, _, _)).WillOnce(Return());
    EXPECT_CALL(*pullerManager, UnRegisterReceiver(tagId, _)).WillOnce(Return());

    GaugeMetric metric;
    metric.set_id(metricId);
    metric.set_bucket(ONE_MINUTE);
    auto gaugeFieldMatcher = metric.mutable_gauge_fields_filter()->mutable_fields();
    gaugeFieldMatcher->set_field(tagId);
    gaugeFieldMatcher->add_child()->set_field(2);
    GaugeMetricProducer gaugeProducer(kConfigKey, metric, -1 /*-1 meaning no condition*/, wizard,
                                      tagId, bucketStartTimeNs, pullerManager);

    Alert alert;
    alert.set_id(101);
    alert.set_metric_id(metricId);
    alert.set_trigger_if_sum_gt(25);
    alert.set_num_buckets(2);
    const int32_t refPeriodSec = 60;
    alert.set_refractory_period_secs(refPeriodSec);
    sp<AnomalyTracker> anomalyTracker = gaugeProducer.addAnomalyTracker(alert);

    int tagId = 1;
    std::shared_ptr<LogEvent> event1 = std::make_shared<LogEvent>(tagId, bucketStartTimeNs + 1);
    event1->write("some value");
    event1->write(13);
    event1->init();

    gaugeProducer.onDataPulled({event1});
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_EQ(13L,
        gaugeProducer.mCurrentSlicedBucket->begin()->second->begin()->second.value_int());
    EXPECT_EQ(anomalyTracker->getRefractoryPeriodEndsSec(DEFAULT_DIMENSION_KEY), 0U);

    std::shared_ptr<LogEvent> event2 =
            std::make_shared<LogEvent>(tagId, bucketStartTimeNs + bucketSizeNs + 20);
    event2->write("some value");
    event2->write(15);
    event2->init();

    gaugeProducer.onDataPulled({event2});
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_EQ(15L,
        gaugeProducer.mCurrentSlicedBucket->begin()->second->begin()->second.value_int());
    EXPECT_EQ(anomalyTracker->getRefractoryPeriodEndsSec(DEFAULT_DIMENSION_KEY),
            event2->GetTimestampNs() / NS_PER_SEC + refPeriodSec);

    std::shared_ptr<LogEvent> event3 =
            std::make_shared<LogEvent>(tagId, bucketStartTimeNs + 2 * bucketSizeNs + 10);
    event3->write("some value");
    event3->write(26);
    event3->init();

    gaugeProducer.onDataPulled({event3});
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_EQ(26L,
        gaugeProducer.mCurrentSlicedBucket->begin()->second->begin()->second.value_int());
    EXPECT_EQ(anomalyTracker->getRefractoryPeriodEndsSec(DEFAULT_DIMENSION_KEY),
            event2->GetTimestampNs() / NS_PER_SEC + refPeriodSec);

    // The event4 does not have the gauge field. Thus the current bucket value is 0.
    std::shared_ptr<LogEvent> event4 =
            std::make_shared<LogEvent>(tagId, bucketStartTimeNs + 3 * bucketSizeNs + 10);
    event4->write("some value");
    event4->init();
    gaugeProducer.onDataPulled({event4});
    EXPECT_EQ(1UL, gaugeProducer.mCurrentSlicedBucket->size());
    EXPECT_TRUE(gaugeProducer.mCurrentSlicedBucket->begin()->second->empty());
}

}  // namespace statsd
}  // namespace os
}  // namespace android
#else
GTEST_LOG_(INFO) << "This test does nothing.\n";
#endif