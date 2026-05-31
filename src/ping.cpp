#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

struct LatencySummary
{
  double min_rtt_us = 0.0;
  double p05_rtt_us = 0.0;
  double median_rtt_us = 0.0;
  double p95_rtt_us = 0.0;
  double max_rtt_us = 0.0;
  double loss_percent = 0.0;
  std::size_t received_samples = 0;
};

class PingNode : public rclcpp::Node
{
public:
  PingNode()
  : rclcpp::Node("ping_node")
  {
    run_started_at_ = std::chrono::system_clock::now();
    declare_parameters();
    load_parameters();
    initialize_message();
    start_time_ = Clock::now();
    total_expected_runtime_ = warmup_seconds_ + static_cast<double>(samples_) / publish_rate_hz_;

    qos_config_.print();
    const auto qos = qos_config_.to_rclcpp_qos();
    ping_publisher_ = create_publisher<perf::msg::U8Array>("ping", qos);
    pong_subscriber_ = create_subscription<perf::msg::U8Array>(
      "pong", qos, std::bind(&PingNode::handle_pong, this, std::placeholders::_1));
    timer_ = create_wall_timer(compute_publish_period(), std::bind(&PingNode::publish_ping, this));
  }

  bool is_running() const
  {
    if (samples_sent_ < samples_) {
      return true;
    }

    const auto elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start_time_);
    return elapsed.count() <= total_expected_runtime_ * 1000.0;
  }

  void show_results()
  {
    if (round_trip_times_us_.empty()) {
      std::cout << "[ERROR] Empty results." << std::endl;
      write_results_json(std::nullopt, "Empty results.");
      return;
    }

    std::sort(round_trip_times_us_.begin(), round_trip_times_us_.end());
    const auto summary = build_summary();

    if (summary.received_samples > samples_) {
      std::cout << "[ERROR] Received " << summary.received_samples
                << " messages which should not be greater than the total samples " << samples_
                << std::endl;
    }

    std::cout << std::fixed
              << "[RTT(us)] min: " << summary.min_rtt_us
              << ", p05: " << summary.p05_rtt_us
              << ", p50: " << summary.median_rtt_us
              << ", p95: " << summary.p95_rtt_us
              << ", max: " << summary.max_rtt_us << std::endl
              << "[Loss(%)] " << summary.loss_percent << std::endl;
    write_results_json(summary, "");
  }

private:
  void declare_parameters()
  {
    declare_parameter("warmup", 5.0);
    declare_parameter("samples", 100);
    declare_parameter("size", 32);
    declare_parameter("rate", 10);
    declare_parameter("output_json", std::string{""});
    QoSConfig::declare_parameters(*this);
  }

  void load_parameters()
  {
    warmup_seconds_ = get_parameter("warmup").as_double();
    samples_ = static_cast<std::size_t>(get_parameter("samples").as_int());
    payload_size_ = static_cast<std::size_t>(get_parameter("size").as_int());
    publish_rate_hz_ = static_cast<int>(get_parameter("rate").as_int());
    output_json_path_ = get_parameter("output_json").as_string();
    qos_config_ = QoSConfig::from_node(*this);
    validate_parameters();

    std::cout << "Warm up time (sec): " << warmup_seconds_ << std::endl;
    std::cout << "Samples number: " << samples_ << std::endl;
    std::cout << "Payload size (bytes): " << payload_size_ << std::endl;
    std::cout << "Publish rate (Hz): " << publish_rate_hz_ << std::endl;
  }

  void initialize_message()
  {
    message_ = std::make_shared<perf::msg::U8Array>();
    message_->data.assign(payload_size_, 0);
  }

  void validate_parameters() const
  {
    if (publish_rate_hz_ <= 0) {
      throw std::runtime_error("rate must be greater than 0");
    }

    if (samples_ == 0) {
      throw std::runtime_error("samples must be greater than 0");
    }

    if (payload_size_ < sizeof(std::uint64_t)) {
      throw std::runtime_error("size must be at least 8 bytes to store the ping timestamp");
    }
  }

  std::chrono::milliseconds compute_publish_period() const
  {
    const auto period_ms = std::clamp(1000 / publish_rate_hz_, 1, 1000);
    return std::chrono::milliseconds(period_ms);
  }

  bool warmup_complete()
  {
    if (!warmup_complete_) {
      const auto elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start_time_);
      warmup_complete_ = elapsed.count() > warmup_seconds_ * 1000.0;
    }
    return warmup_complete_;
  }

  void publish_ping()
  {
    if (samples_sent_ >= samples_) {
      return;
    }

    const auto timestamp = now_microseconds();
    std::memmove(message_->data.data(), &timestamp, sizeof(timestamp));
    ping_publisher_->publish(*message_);

    if (warmup_complete()) {
      ++samples_sent_;
    }
  }

  void handle_pong(const perf::msg::U8Array::SharedPtr msg)
  {
    std::uint64_t sent_timestamp_us = 0;
    std::memmove(&sent_timestamp_us, msg->data.data(), sizeof(sent_timestamp_us));

    const auto rtt_us = now_microseconds() - sent_timestamp_us;
    if (warmup_complete()) {
      RCLCPP_INFO(get_logger(), "RTT: %lu(us), RTT/2: %lu(us)", rtt_us, rtt_us / 2);
      round_trip_times_us_.push_back(static_cast<double>(rtt_us));
    }
  }

  std::uint64_t now_microseconds() const
  {
    return std::chrono::duration_cast<Microseconds>(Clock::now().time_since_epoch()).count();
  }

  double percentile(double fraction) const
  {
    const auto last_index = round_trip_times_us_.size() - 1;
    const auto index = static_cast<std::size_t>(last_index * fraction);
    return round_trip_times_us_[index];
  }

  LatencySummary build_summary() const
  {
    LatencySummary summary;
    summary.received_samples = round_trip_times_us_.size();
    summary.min_rtt_us = percentile(0.00);
    summary.p05_rtt_us = percentile(0.05);
    summary.median_rtt_us = percentile(0.50);
    summary.p95_rtt_us = percentile(0.95);
    summary.max_rtt_us = percentile(1.00);
    const auto lost_samples = summary.received_samples >= samples_ ?
      0U :
      samples_ - summary.received_samples;
    summary.loss_percent =
      static_cast<double>(lost_samples) * 100.0 / static_cast<double>(samples_);
    return summary;
  }

  void write_results_json(
    const std::optional<LatencySummary> & summary,
    const std::string & error_message) const
  {
    if (output_json_path_.empty()) {
      return;
    }

    auto output = create_output_file(output_json_path_);
    output << std::fixed << std::setprecision(2);
    output << "{\n";
    output << "  \"benchmark_type\": \"latency\",\n";
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
    write_json_string(output, summary.has_value() ? "ok" : "error");
    output << ",\n";
    output << "  \"parameters\": {\n";
    output << "    \"warmup_seconds\": " << warmup_seconds_ << ",\n";
    output << "    \"samples\": " << samples_ << ",\n";
    output << "    \"payload_size_bytes\": " << payload_size_ << ",\n";
    output << "    \"publish_rate_hz\": " << publish_rate_hz_ << "\n";
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
    output << "  \"summary\": ";
    if (summary.has_value()) {
      output << "{\n";
      output << "    \"received_samples\": " << summary->received_samples << ",\n";
      output << "    \"loss_percent\": " << summary->loss_percent << ",\n";
      output << "    \"min_rtt_us\": " << summary->min_rtt_us << ",\n";
      output << "    \"p05_rtt_us\": " << summary->p05_rtt_us << ",\n";
      output << "    \"median_rtt_us\": " << summary->median_rtt_us << ",\n";
      output << "    \"p95_rtt_us\": " << summary->p95_rtt_us << ",\n";
      output << "    \"max_rtt_us\": " << summary->max_rtt_us << "\n";
      output << "  }";
    } else {
      output << "null";
    }
    output << ",\n";
    output << "  \"samples_us\": [";
    for (std::size_t index = 0; index < round_trip_times_us_.size(); ++index) {
      if (index != 0U) {
        output << ", ";
      }
      output << round_trip_times_us_[index];
    }
    output << "]";

    if (!error_message.empty()) {
      output << ",\n  \"error\": ";
      write_json_string(output, error_message);
    }

    output << "\n}\n";
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<perf::msg::U8Array>::SharedPtr ping_publisher_;
  rclcpp::Subscription<perf::msg::U8Array>::SharedPtr pong_subscriber_;

  QoSConfig qos_config_;
  std::size_t samples_ = 0;
  std::size_t payload_size_ = 0;
  double warmup_seconds_ = 0.0;
  bool warmup_complete_ = false;
  int publish_rate_hz_ = 1;
  std::string output_json_path_;

  double total_expected_runtime_ = 0.0;
  std::atomic<std::size_t> samples_sent_{0};
  perf::msg::U8Array::SharedPtr message_;
  std::vector<double> round_trip_times_us_;
  Clock::time_point start_time_;
  std::chrono::system_clock::time_point run_started_at_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto ping_node = std::make_shared<PingNode>();
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(ping_node);
  while (ping_node->is_running()) {
    executor.spin_some();
  }
  executor.remove_node(ping_node);
  ping_node->show_results();
  rclcpp::shutdown();
  return 0;
}
