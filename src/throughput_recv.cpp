#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

#include "perf/msg/u8_array.hpp"
#include "qos.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

namespace
{

using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

constexpr char kThroughputTopic[] = "throughput";

class ThroughputRecvNode : public rclcpp::Node
{
public:
  ThroughputRecvNode()
  : rclcpp::Node("throughput_recv_node")
  {
    declare_parameter("warmup", 5.0);
    declare_parameter("running_time", 10.0);
    QoSConfig::declare_parameters(*this);

    warmup_seconds_ = get_parameter("warmup").as_double();
    runtime_seconds_ = get_parameter("running_time").as_double();
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

  void show_final_summary() const
  {
    if (measured_messages_ == 0U) {
      std::cout << "[ERROR] No throughput samples were collected." << std::endl;
      return;
    }

    print_summary("[Final]", measurement_elapsed_seconds());
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
    print_summary("[Progress]", measurement_elapsed_seconds(now));
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

    const auto measured = std::chrono::duration_cast<Seconds>(now - measurement_start_time_).count();
    return std::min(measured, runtime_seconds_);
  }

  void print_summary(const char * prefix, double elapsed) const
  {
    const auto message_rate = measured_messages_ / elapsed;
    const auto mib = static_cast<double>(measured_bytes_) / (1024.0 * 1024.0);
    const auto mib_rate = mib / elapsed;
    const auto expected_messages = measured_messages_ + dropped_messages_;
    const auto drop_percent =
      expected_messages == 0U ? 0.0 : (static_cast<double>(dropped_messages_) * 100.0) / expected_messages;

    std::cout << std::fixed << std::setprecision(2)
              << prefix
              << " elapsed(s): " << elapsed
              << ", recv(msg): " << measured_messages_
              << ", recv(MiB): " << mib
              << ", rate(msg/s): " << message_rate
              << ", rate(MiB/s): " << mib_rate
              << ", dropped: " << dropped_messages_
              << ", drop(%): " << drop_percent
              << std::endl;
  }

  QoSConfig qos_config_;
  double warmup_seconds_ = 0.0;
  double runtime_seconds_ = 0.0;
  bool measurement_started_ = false;
  Clock::time_point start_time_;
  Clock::time_point measurement_start_time_;
  Clock::time_point last_report_time_;
  std::optional<std::uint64_t> previous_sequence_;
  std::uint64_t measured_messages_ = 0;
  std::uint64_t measured_bytes_ = 0;
  std::uint64_t dropped_messages_ = 0;
  rclcpp::Subscription<perf::msg::U8Array>::SharedPtr subscriber_;
  rclcpp::TimerBase::SharedPtr report_timer_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ThroughputRecvNode>();
  while (rclcpp::ok() && node->is_running()) {
    rclcpp::spin_some(node);
  }
  node->show_final_summary();
  rclcpp::shutdown();
  return 0;
}
