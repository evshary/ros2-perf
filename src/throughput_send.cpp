#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "perf/msg/u8_array.hpp"
#include "qos.hpp"
#include "rclcpp/rclcpp.hpp"

namespace
{

constexpr char kThroughputTopic[] = "throughput";

class ThroughputSendNode : public rclcpp::Node
{
public:
  ThroughputSendNode()
  : rclcpp::Node("throughput_send_node")
  {
    declare_parameter("size", 32);
    QoSConfig::declare_parameters(*this);

    payload_size_ = static_cast<std::size_t>(get_parameter("size").as_int());
    qos_config_ = QoSConfig::from_node(*this);
    validate_parameters();

    message_ = std::make_unique<perf::msg::U8Array>();
    message_->data.assign(payload_size_, 0);

    std::cout << "Payload size (bytes): " << payload_size_ << std::endl;
    qos_config_.print();

    publisher_ = create_publisher<perf::msg::U8Array>(kThroughputTopic, qos_config_.to_rclcpp_qos());
  }

  void run()
  {
    while (rclcpp::ok()) {
      stamp_sequence();
      publisher_->publish(*message_);
      ++sequence_;
      rclcpp::spin_some(get_node_base_interface());
    }
  }

private:
  void validate_parameters() const
  {
    if (payload_size_ < sizeof(std::uint64_t)) {
      throw std::runtime_error("size must be at least 8 bytes to store the throughput sequence");
    }
  }

  void stamp_sequence()
  {
    std::memmove(message_->data.data(), &sequence_, sizeof(sequence_));
  }

  QoSConfig qos_config_;
  std::size_t payload_size_ = 0;
  std::uint64_t sequence_ = 0;
  std::unique_ptr<perf::msg::U8Array> message_;
  rclcpp::Publisher<perf::msg::U8Array>::SharedPtr publisher_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ThroughputSendNode>();
  node->run();
  rclcpp::shutdown();
  return 0;
}
