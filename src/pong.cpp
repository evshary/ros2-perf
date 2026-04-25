#include <functional>
#include <memory>

#include "perf/msg/u8_array.hpp"
#include "qos.hpp"
#include "rclcpp/rclcpp.hpp"

namespace
{

class PongNode : public rclcpp::Node
{
public:
  PongNode()
  : rclcpp::Node("pong_node")
  {
    QoSConfig::declare_parameters(*this);
    qos_config_ = QoSConfig::from_node(*this);
    qos_config_.print();

    const auto qos = qos_config_.to_rclcpp_qos();
    ping_subscriber_ = create_subscription<perf::msg::U8Array>(
      "ping", qos, std::bind(&PongNode::handle_ping, this, std::placeholders::_1));
    pong_publisher_ = create_publisher<perf::msg::U8Array>("pong", qos);
  }

private:
  void handle_ping(const perf::msg::U8Array::SharedPtr msg) const
  {
    pong_publisher_->publish(*msg);
    RCLCPP_INFO(get_logger(), "Receiving data size: %zu", msg->data.size());
  }

  QoSConfig qos_config_;
  rclcpp::Subscription<perf::msg::U8Array>::SharedPtr ping_subscriber_;
  rclcpp::Publisher<perf::msg::U8Array>::SharedPtr pong_publisher_;
};

}  // namespace

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PongNode>());
  rclcpp::shutdown();
  return 0;
}
