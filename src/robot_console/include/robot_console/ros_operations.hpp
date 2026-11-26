#pragma once

#include <rclcpp/rclcpp.hpp>

#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "robot_console/request_tracker.hpp"
#include "robot_console_core/operation_source.hpp"

namespace robot_console {

class RosOperations final : public OperationSource {
 public:
  RosOperations();

  bool available(Operation operation) const override;
  bool submit(const OperationRequest& request) override;

  void start(const rclcpp::Node::SharedPtr& node);
  void stopAccepting();
  void stop();

 private:
  struct Client {
    rclcpp::ClientBase::SharedPtr ros;
    std::function<void(const OperationRequest&)> send;
  };

  struct Pending {
    RequestTracker tracker;
    int64_t sequence = 0;
    std::function<void(int64_t)> remove;
  };

  template<class Service, class Encode, class Decode>
  void add(const rclcpp::Node::SharedPtr& node, Operation operation,
           const char* service_name, Encode encode, Decode decode);
  void poll();

  mutable std::mutex queue_mutex_;
  bool accepting_ = false;
  std::vector<OperationRequest> queue_;
  std::map<Operation, bool> ready_;
  std::map<Operation, Client> clients_;
  std::map<quint64, Pending> pending_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace robot_console
