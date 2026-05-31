#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

#include "perf/msg/u8_array.hpp"
#include "qos.hpp"
#include "result_utils.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

namespace
{

using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

constexpr char kThroughputTopic[] = "throughput";

struct ThroughputSample
{
  double elapsed_seconds = 0.0;
  std::uint64_t received_messages = 0U;
  double received_mib = 0.0;
  double message_rate = 0.0;
  double mib_rate = 0.0;
  std::uint64_t dropped_messages = 0U;
  double drop_percent = 0.0;
};

class ThroughputRecvNode : public rclcpp::Node
{
public:
  ThroughputRecvNode()
  : rclcpp::Node("throughput_recv_node")
  {
    run_started_at_ = std::chrono::system_clock::now();
    declare_parameter("warmup", 5.0);
    declare_parameter("running_time", 10.0);
    declare_parameter("output_json", std::string{""});
    QoSConfig::declare_parameters(*this);

    warmup_seconds_ = get_parameter("warmup").as_double();
    runtime_seconds_ = get_parameter("running_time").as_double();
    output_json_path_ = get_parameter("output_json").as_string();
    qos_config_ = QoSConfig::from_node(*this);
    validate_parameters();

    start_time_ = Clock::now();
    last_report_time_ = start_time_;

    std::cout << "Warm up time (sec): " << warmup_seconds_ << std::endl;
    std::cout << "Measurement time (sec): " << runtime_seconds_ << std::endl;
    std::cout << "Total run time (sec): " << warmup_seconds_ + runtime_seconds_ << std::endl;
    qos_config_.print();

    subscriber_ = create_subscription<perf::msg::U8Array>(
      kThroughputTopic, qos_config_.to_rclcpp_qos(),
      std::bind(&ThroughputRecvNode::handle_message, this, std::placeholders::_1));
    report_timer_ = create_wall_timer(1s, std::bind(&ThroughputRecvNode::report_progress, this));
  }

  bool is_running() const
  {
    return elapsed_seconds() < warmup_seconds_ + runtime_seconds_;
  }

  void show_final_summary()
  {
    if (measured_messages_ == 0U) {
      std::cout << "[ERROR] No throughput samples were collected." << std::endl;
      write_results_json(std::nullopt, "No throughput samples were collected.");
      return;
    }

    const auto final_sample = build_sample(measurement_elapsed_seconds());
    print_summary("[Final]", final_sample);
    progress_samples_.push_back(final_sample);
    write_results_json(final_sample, "");
  }

private:
  void validate_parameters() const
  {
    if (warmup_seconds_ < 0.0) {
      throw std::runtime_error("warmup must be greater than or equal to 0");
    }

    if (runtime_seconds_ <= 0.0) {
      throw std::runtime_error("running_time must be greater than 0");
    }
  }

  void handle_message(const perf::msg::U8Array::SharedPtr msg)
  {
    const auto sequence = decode_sequence(*msg);
    const auto now = Clock::now();
    payload_size_bytes_ = msg->data.size();

    if (!measurement_started_ && elapsed_seconds(now) >= warmup_seconds_) {
      measurement_started_ = true;
      measurement_start_time_ = now;
      last_report_time_ = now;
      previous_sequence_ = sequence;
      return;
    }

    if (!measurement_started_) {
      previous_sequence_ = sequence;
      return;
    }

    if (previous_sequence_.has_value() && sequence > *previous_sequence_ + 1U) {
      dropped_messages_ += sequence - *previous_sequence_ - 1U;
    }

    previous_sequence_ = sequence;
    ++measured_messages_;
    measured_bytes_ += msg->data.size();
  }

  void report_progress()
  {
    if (!measurement_started_ || measured_messages_ == 0U || !is_running()) {
      return;
    }

    const auto now = Clock::now();
    if (std::chrono::duration_cast<Seconds>(now - last_report_time_).count() < 1.0) {
      return;
    }

    last_report_time_ = now;
    const auto progress_sample = build_sample(measurement_elapsed_seconds(now));
    print_summary("[Progress]", progress_sample);
    progress_samples_.push_back(progress_sample);
  }

  std::uint64_t decode_sequence(const perf::msg::U8Array & msg) const
  {
    std::uint64_t sequence = 0;
    std::memmove(&sequence, msg.data.data(), sizeof(sequence));
    return sequence;
  }

  double elapsed_seconds() const
  {
    return elapsed_seconds(Clock::now());
  }

  double elapsed_seconds(const Clock::time_point & now) const
  {
    return std::chrono::duration_cast<Seconds>(now - start_time_).count();
  }

  double measurement_elapsed_seconds() const
  {
    return measurement_elapsed_seconds(Clock::now());
  }

  double measurement_elapsed_seconds(const Clock::time_point & now) const
  {
    if (!measurement_started_) {
      return 0.0;
    }

    const auto measured = std::chrono::duration_cast<Seconds>(
      now - measurement_start_time_).count();
    return std::min(measured, runtime_seconds_);
  }

  ThroughputSample build_sample(double elapsed) const
  {
    ThroughputSample sample;
    const auto safe_elapsed = std::max(elapsed, 1e-9);
    sample.elapsed_seconds = elapsed;
    sample.received_messages = measured_messages_;
    sample.received_mib = static_cast<double>(measured_bytes_) / (1024.0 * 1024.0);
    sample.message_rate = measured_messages_ / safe_elapsed;
    sample.mib_rate = sample.received_mib / safe_elapsed;
    const auto expected_messages = measured_messages_ + dropped_messages_;
    sample.dropped_messages = dropped_messages_;
    sample.drop_percent = expected_messages == 0U ?
      0.0 :
      (static_cast<double>(dropped_messages_) * 100.0) / expected_messages;
    return sample;
  }

  void print_summary(const char * prefix, const ThroughputSample & sample) const
  {
    std::cout << std::fixed << std::setprecision(2)
              << prefix
              << " elapsed(s): " << sample.elapsed_seconds
              << ", recv(msg): " << sample.received_messages
              << ", recv(MiB): " << sample.received_mib
              << ", rate(msg/s): " << sample.message_rate
              << ", rate(MiB/s): " << sample.mib_rate
              << ", dropped: " << sample.dropped_messages
              << ", drop(%): " << sample.drop_percent
              << std::endl;
  }

  void write_results_json(
    const std::optional<ThroughputSample> & final_sample,
    const std::string & error_message) const
  {
    if (output_json_path_.empty()) {
      return;
    }

    auto output = create_output_file(output_json_path_);
    output << std::fixed << std::setprecision(2);
    output << "{\n";
    output << "  \"benchmark_type\": \"throughput\",\n";
    output << "  \"timestamp\": ";
    write_json_string(output, to_iso8601_utc(run_started_at_));
    output << ",\n";
    output << "  \"rmw_implementation\": ";
    write_json_string(
      output, std::getenv("RMW_IMPLEMENTATION") == nullptr ?
      "UNSET" :
      std::getenv("RMW_IMPLEMENTATION"));
    output << ",\n";
    output << "  \"ros_distro\": ";
    write_json_string(
      output, std::getenv("ROS_DISTRO") == nullptr ?
      "UNSET" :
      std::getenv("ROS_DISTRO"));
    output << ",\n";
    output << "  \"status\": ";
    write_json_string(output, final_sample.has_value() ? "ok" : "error");
    output << ",\n";
    output << "  \"parameters\": {\n";
    output << "    \"warmup_seconds\": " << warmup_seconds_ << ",\n";
    output << "    \"running_time_seconds\": " << runtime_seconds_ << ",\n";
    output << "    \"payload_size_bytes\": " << payload_size_bytes_ << "\n";
    output << "  },\n";
    output << "  \"qos\": {\n";
    output << "    \"reliability\": ";
    write_json_string(output, qos_config_.reliability);
    output << ",\n";
    output << "    \"durability\": ";
    write_json_string(output, qos_config_.durability);
    output << ",\n";
    output << "    \"history\": ";
    write_json_string(output, qos_config_.history);
    output << ",\n";
    output << "    \"history_depth\": " << qos_config_.history_depth << "\n";
    output << "  },\n";

    output << "  \"progress_samples\": [\n";
    for (std::size_t index = 0; index < progress_samples_.size(); ++index) {
      const auto & sample = progress_samples_[index];
      output << "    {\n";
      output << "      \"elapsed_seconds\": " << sample.elapsed_seconds << ",\n";
      output << "      \"received_messages\": " << sample.received_messages << ",\n";
      output << "      \"received_mib\": " << sample.received_mib << ",\n";
      output << "      \"message_rate\": " << sample.message_rate << ",\n";
      output << "      \"mib_rate\": " << sample.mib_rate << ",\n";
      output << "      \"dropped_messages\": " << sample.dropped_messages << ",\n";
      output << "      \"drop_percent\": " << sample.drop_percent << "\n";
      output << "    }";
      if (index + 1U != progress_samples_.size()) {
        output << ",";
      }
      output << "\n";
    }
    output << "  ],\n";

    output << "  \"summary\": ";
    if (final_sample.has_value()) {
      std::vector<double> mib_rates;
      mib_rates.reserve(progress_samples_.size());
      for (const auto & sample : progress_samples_) {
        mib_rates.push_back(sample.mib_rate);
      }
      std::sort(mib_rates.begin(), mib_rates.end());
      const auto median_index = (mib_rates.size() - 1U) / 2U;

      output << "{\n";
      output << "    \"received_messages\": " << final_sample->received_messages << ",\n";
      output << "    \"received_mib\": " << final_sample->received_mib << ",\n";
      output << "    \"message_rate\": " << final_sample->message_rate << ",\n";
      output << "    \"mib_rate\": " << final_sample->mib_rate << ",\n";
      output << "    \"dropped_messages\": " << final_sample->dropped_messages << ",\n";
      output << "    \"drop_percent\": " << final_sample->drop_percent << ",\n";
      output << "    \"min_mib_per_sec\": " << mib_rates.front() << ",\n";
      output << "    \"median_mib_per_sec\": " << mib_rates[median_index] << ",\n";
      output << "    \"max_mib_per_sec\": " << mib_rates.back() << "\n";
      output << "  }";
    } else {
      output << "null";
    }

    if (!error_message.empty()) {
      output << ",\n  \"error\": ";
      write_json_string(output, error_message);
    }

    output << "\n}\n";
  }

  QoSConfig qos_config_;
  double warmup_seconds_ = 0.0;
  double runtime_seconds_ = 0.0;
  std::string output_json_path_;
  bool measurement_started_ = false;
  Clock::time_point start_time_;
  Clock::time_point measurement_start_time_;
  Clock::time_point last_report_time_;
  std::chrono::system_clock::time_point run_started_at_;
  std::optional<std::uint64_t> previous_sequence_;
  std::size_t payload_size_bytes_ = 0U;
  std::uint64_t measured_messages_ = 0;
  std::uint64_t measured_bytes_ = 0;
  std::uint64_t dropped_messages_ = 0;
  std::vector<ThroughputSample> progress_samples_;
  rclcpp::Subscription<perf::msg::U8Array>::SharedPtr subscriber_;
  rclcpp::TimerBase::SharedPtr report_timer_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ThroughputRecvNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  while (rclcpp::ok() && node->is_running()) {
    executor.spin_some();
  }
  executor.remove_node(node);
  node->show_final_summary();
  rclcpp::shutdown();
  return 0;
}
