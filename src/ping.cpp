#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "perf/msg/ping_pong.hpp"
#include "qos.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

namespace
{

using Clock = std::chrono::high_resolution_clock;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

class PingNode : public rclcpp::Node
{
public:
  PingNode()
  : rclcpp::Node("ping_node")
  {
    declare_parameters();
    load_parameters();
    initialize_message();
    start_time_ = Clock::now();
    total_expected_runtime_ = warmup_seconds_ + static_cast<double>(samples_) / publish_rate_hz_;

    qos_config_.print();
    const auto qos = qos_config_.to_rclcpp_qos();
    ping_publisher_ = create_publisher<perf::msg::PingPong>("ping", qos);
    pong_subscriber_ = create_subscription<perf::msg::PingPong>(
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
      return;
    }

    std::sort(round_trip_times_us_.begin(), round_trip_times_us_.end());
    const auto received_samples = round_trip_times_us_.size();

    if (received_samples > samples_) {
      std::cout << "[ERROR] Received " << received_samples
                << " messages which should not be greater than the total samples " << samples_
                << std::endl;
    }

    std::cout << std::fixed
              << "[RTT(us)] min: " << percentile(0.00)
              << ", p05: " << percentile(0.05)
              << ", p50: " << percentile(0.50)
              << ", p95: " << percentile(0.95)
              << ", max: " << percentile(1.00) << std::endl
              << "[Loss(%)] " << (samples_ - received_samples) * 100 / samples_ << std::endl;
  }

private:
  void declare_parameters()
  {
    declare_parameter("warmup", 5.0);
    declare_parameter("samples", 100);
    declare_parameter("size", 32);
    declare_parameter("rate", 10);
    QoSConfig::declare_parameters(*this);
  }

  void load_parameters()
  {
    warmup_seconds_ = get_parameter("warmup").as_double();
    samples_ = static_cast<std::size_t>(get_parameter("samples").as_int());
    payload_size_ = static_cast<std::size_t>(get_parameter("size").as_int());
    publish_rate_hz_ = static_cast<int>(get_parameter("rate").as_int());
    qos_config_ = QoSConfig::from_node(*this);
    validate_parameters();

    std::cout << "Warm up time (sec): " << warmup_seconds_ << std::endl;
    std::cout << "Samples number: " << samples_ << std::endl;
    std::cout << "Payload size (bytes): " << payload_size_ << std::endl;
    std::cout << "Publish rate (Hz): " << publish_rate_hz_ << std::endl;
  }

  void initialize_message()
  {
    message_ = std::make_shared<perf::msg::PingPong>();
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

  void handle_pong(const perf::msg::PingPong::SharedPtr msg)
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

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<perf::msg::PingPong>::SharedPtr ping_publisher_;
  rclcpp::Subscription<perf::msg::PingPong>::SharedPtr pong_subscriber_;

  QoSConfig qos_config_;
  std::size_t samples_ = 0;
  std::size_t payload_size_ = 0;
  double warmup_seconds_ = 0.0;
  bool warmup_complete_ = false;
  int publish_rate_hz_ = 1;

  double total_expected_runtime_ = 0.0;
  std::atomic<std::size_t> samples_sent_{0};
  perf::msg::PingPong::SharedPtr message_;
  std::vector<double> round_trip_times_us_;
  Clock::time_point start_time_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto ping_node = std::make_shared<PingNode>();
  while (ping_node->is_running()) {
    rclcpp::spin_some(ping_node);
  }
  ping_node->show_results();
  rclcpp::shutdown();
  return 0;
}
