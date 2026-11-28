#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace robot_simulation {

// 服务应答的等待上限：过了就按原样重发。FastDDS 会丢应答（夜跑 #3：路网查询丢了一次，
// robot_3 就一直没就绪）。
inline constexpr double kServiceResponseTimeoutS = 5.0;

// 一次只有一个在途请求、超时按原样重发的服务调用。只用于幂等的请求：只读查询，或重复提交
// 结果相同的路权申请（已持有照样授予，重复排队保留原位次）。
// 在应答之前不换请求内容：服务端可能已经按原请求授予，换了内容这份授予就没人释放。
// ClientT 是 rclcpp::Client<ServiceT>；做成模板是为了不起 ROS 也能单测。
template <typename ClientT>
class RetryingRequest {
 public:
  using Request = typename ClientT::SharedRequest;
  using Response = typename ClientT::SharedResponse;
  using Callback = std::function<void(const Response&)>;

  bool pending() const { return static_cast<bool>(request_); }

  void Cancel() {
    ++generation_;
    if (request_ && client_) {
      client_->remove_pending_request(request_id_);
    }
    request_.reset();
    on_done_ = nullptr;
  }

  void Send(std::shared_ptr<ClientT> client, Request request, Callback on_done, const double now_s) {
    client_ = std::move(client);
    request_ = std::move(request);
    on_done_ = std::move(on_done);
    Dispatch(now_s);
  }

  // Periodic read-only polling. Keep one request outstanding even when callbacks are delayed.
  void Poll(std::shared_ptr<ClientT> client, Request request, Callback on_done) {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const double now_s = std::chrono::duration<double>(elapsed).count();
    if (pending()) {
      ResendIfOverdue(now_s);
    } else if (client->service_is_ready()) {
      Send(std::move(client), std::move(request), std::move(on_done), now_s);
    }
  }

  // 返回是否重发了。被放弃的那次请求即使应答迟到也不会再回调。
  bool ResendIfOverdue(const double now_s, const double timeout_s = kServiceResponseTimeoutS) {
    if (!pending() || now_s - sent_s_ < timeout_s || !client_->service_is_ready()) {
      return false;
    }
    client_->remove_pending_request(request_id_);
    Dispatch(now_s);
    return true;
  }

 private:
  void Dispatch(const double now_s) {
    sent_s_ = now_s;
    const std::uint64_t generation = ++generation_;
    request_id_ = client_
                      ->async_send_request(
                          request_,
                          [this, generation](typename ClientT::SharedFuture future) {
                            if (generation != generation_ || !request_) {
                              return;
                            }
                            ++generation_;
                            // 先清状态再回调：回调里可能紧接着发下一个请求。
                            const Callback on_done = std::move(on_done_);
                            on_done_ = nullptr;
                            request_.reset();
                            if (on_done) {
                              on_done(future.get());
                            }
                          })
                      .request_id;
  }

  std::shared_ptr<ClientT> client_;
  Request request_;
  Callback on_done_;
  double sent_s_ = 0.0;
  std::int64_t request_id_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace robot_simulation
