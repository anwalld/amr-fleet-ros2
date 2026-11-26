#include <chrono>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace robot_simulation {

class FleetSimStopNode final : public rclcpp::Node {
 public:
  FleetSimStopNode() : Node("fleet_sim_stop_node") {
    state_ = create_publisher<std_msgs::msg::Bool>(
        "/fleet/sim_stop/state", rclcpp::QoS(1).reliable().transient_local());
    service_ = create_service<std_srvs::srv::SetBool>(
        "/v2/set_fleet_sim_stop",
        [this](std_srvs::srv::SetBool::Request::SharedPtr request,
               std_srvs::srv::SetBool::Response::SharedPtr response) {
          stopped_ = request->data;
          publish();
          response->success = true;
          response->message = stopped_ ? "simulation fleet stop set" : "simulation fleet stop released";
        });
    timer_ = create_wall_timer(std::chrono::milliseconds(500), [this] { publish(); });
    publish();
  }

 private:
  void publish() {
    std_msgs::msg::Bool msg;
    msg.data = stopped_;
    state_->publish(msg);
  }

  bool stopped_ = true;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr state_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr service_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace robot_simulation

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<robot_simulation::FleetSimStopNode>());
  rclcpp::shutdown();
}
