#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "prometheus/client_metric.h"
#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/family.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/metric_family.h"
#include "prometheus/registry.h"
#include "prometheus/summary.h"

using namespace prometheus;

int main() {
  // create an http server running on port 8080
  Exposer exposer{"127.0.0.1:8080"};

  // create a metrics registry
  // @note it's the users responsibility to keep the object alive
  auto registry = std::make_shared<Registry>();

  // add a new counter family to the registry (families combine values with the
  // same name, but distinct label dimensions)
  //
  // @note please follow the metric-naming best-practices:
  // https://prometheus.io/docs/practices/naming/
  auto& packet_counter = BuildCounter()
                             .Name("observed_packets_total")
                             .Help("Number of observed packets")
                             .Register(*registry);

  // add and remember dimensional data, incrementing those is very cheap
  auto& tcp_rx_counter =
      packet_counter.Add({{"protocol", "tcp"}, {"direction", "rx"}});
  auto& tcp_tx_counter =
      packet_counter.Add({{"protocol", "tcp"}, {"direction", "tx"}});
  auto& udp_rx_counter =
      packet_counter.Add({{"protocol", "udp"}, {"direction", "rx"}});
  auto& udp_tx_counter =
      packet_counter.Add({{"protocol", "udp"}, {"direction", "tx"}});

  // add a counter whose dimensional data is not known at compile time
  // nevertheless dimensional values should only occur in low cardinality:
  // https://prometheus.io/docs/practices/naming/#labels
  auto& http_requests_counter = BuildCounter()
                                    .Name("http_requests_total")
                                    .Help("Number of HTTP requests")
                                    .Register(*registry);

  auto& gauge_requests_counter = BuildGauge()
                                     .Name("gauge_requests_total")
                                     .Help("Number of gauge requests")
                                     .Register(*registry);

  auto& guage_tx_counter =
      gauge_requests_counter.Add({{"type", "guage"}, {"direction", "tx"}});

  auto& guage_rx_counter =
      gauge_requests_counter.Add({{"type", "guage"}, {"direction", "rx"}});

  const int number_of_buckets = 10;
  auto bucket_boundaries = Histogram::BucketBoundaries{};
  for (auto i = 0; i < number_of_buckets; i += 1) {
    bucket_boundaries.push_back(i);
  }

  auto& histogram_family = BuildHistogram()
                               .Name("name_histogram")
                               .Help("help histogram")
                               .Register(*registry);

  auto& histogram_single = histogram_family.Add({}, bucket_boundaries);

  auto& histogram_multiple = BuildHistogram()
                                 .Name("name_histogram_multiple")
                                 .Help("help histogram multiple")
                                 .Register(*registry);

  auto& histogram_multi = histogram_multiple.Add({}, bucket_boundaries);

  double sum_of_bucket_values = 0.0;
  std::vector<double> bucket_increments;
  for (auto i = 0; i < number_of_buckets + 1; i += 1) {
    bucket_increments.push_back(i);
    sum_of_bucket_values += i;
  }

  auto& summary_family = BuildSummary()
                             .Name("name_summary_family")
                             .Help("help summary_family")
                             .Register(*registry);

  auto& summary = summary_family.Add(
      {}, Summary::Quantiles{
              {0.5, 0.05}, {0.9, 0.01}, {0.95, 0.005}, {0.99, 0.001}});

  // ask the exposer to scrape the registry on incoming HTTP requests
  exposer.RegisterCollectable(registry);

  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto random_value = std::rand();
    std::cout << "random_value: " << random_value
              << ", random_value%10: " << random_value % 10 << std::endl;

    if (random_value & 1) tcp_rx_counter.Increment();
    if (random_value & 2) tcp_tx_counter.Increment();
    if (random_value & 4) udp_rx_counter.Increment();
    if (random_value & 8) udp_tx_counter.Increment();

    const std::array<std::string, 4> methods = {"GET", "PUT", "POST", "HEAD"};
    auto method = methods.at(random_value % methods.size());
    // dynamically calling Family<T>.Add() works but is slow and should be
    // avoided
    http_requests_counter.Add({{"method", method}}).Increment();

    guage_tx_counter.SetToCurrentTime();
    guage_rx_counter.Increment();

    // value를 하나씩 추가하면, 해당 bucket을 찾아서 Counter 증가
    histogram_single.Observe(random_value % 10);

    std::vector<double> bucket_increments_new;
    for (auto i = 0; i < number_of_buckets + 1; i += 1) {
      bucket_increments_new.push_back(i);
      sum_of_bucket_values += i;
    }
    // each bucket 증가분에 대해 전체를 한번에 업데이트
    histogram_multi.ObserveMultiple(bucket_increments, sum_of_bucket_values);

    summary.Observe(random_value % 1000);

    auto counter_packet_families = packet_counter.Collect();
    auto counter_http_families = http_requests_counter.Collect();

    auto gauge_rerquests_families = gauge_requests_counter.Collect();

    auto histogram_single_families = histogram_family.Collect();
    auto histogram_multiple_families = histogram_multiple.Collect();

    auto summary_families = summary_family.Collect();

    for (auto& family : gauge_rerquests_families) {
      std::cout << "name: " << family.name << std::endl;
      // std::cout << "help: " << family.help << std::endl;
      // std::cout << "type: " << int(family.type) << std::endl;
      for (auto& metric : family.metric) {
        std::cout << "label(";
        for (auto& label : metric.label) {
          std::cout << label.name << ", " << label.value;
        }
        std::cout << ")\t";
        // std::cout << "counter: " << metric.counter.value << std::endl;
        std::cout << "guage: " << metric.gauge.value << std::endl;
      }
    }
  }
  return 0;
}