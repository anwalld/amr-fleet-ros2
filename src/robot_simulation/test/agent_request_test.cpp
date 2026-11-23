#include "robot_simulation/agent_request.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>

namespace robot_simulation {
namespace {

// 只有 RetryingRequest 用到的那部分 rclcpp::Client 接口。
class FakeClient {
 public:
  using SharedRequest = std::shared_ptr<int>;
  using SharedResponse = std::shared_ptr<int>;
  using SharedFuture = std::shared_future<SharedResponse>;
  struct Sent {
    std::int64_t request_id;
  };

  template <typename CallbackT>
  Sent async_send_request(const SharedRequest& request, CallbackT&& callback) {
    requests.push_back(request);
    const auto id = static_cast<std::int64_t>(requests.size());
    waiting[id] = std::forward<CallbackT>(callback);
    return {id};
  }

  bool remove_pending_request(const std::int64_t id) { return waiting.erase(id) != 0U; }
  bool service_is_ready() const { return ready; }

  // 与 rclcpp 一致：已被 remove 的请求，应答到了也不回调。
  bool Answer(const std::int64_t id, const int value) {
    const auto it = waiting.find(id);
    if (it == waiting.end()) {
      return false;
    }
    const auto callback = it->second;
    waiting.erase(it);
    std::promise<SharedResponse> promise;
    promise.set_value(std::make_shared<int>(value));
    callback(promise.get_future().share());
    return true;
  }

  std::vector<SharedRequest> requests;
  std::map<std::int64_t, std::function<void(SharedFuture)>> waiting;
  bool ready = true;
};

class AgentRequestTest : public ::testing::Test {
 protected:
  void Send(const double now_s) {
    call.Send(client, request, [this](const auto& r) { answers.push_back(*r); }, now_s);
  }

  std::shared_ptr<FakeClient> client = std::make_shared<FakeClient>();
  std::shared_ptr<int> request = std::make_shared<int>(7);
  RetryingRequest<FakeClient> call;
  std::vector<int> answers;
};

TEST_F(AgentRequestTest, LostResponseIsResentUnchanged) {
  Send(100.0);
  ASSERT_TRUE(call.ResendIfOverdue(105.0));
  ASSERT_EQ(client->requests.size(), 2U);
  EXPECT_EQ(client->requests[1], request);
  ASSERT_TRUE(client->Answer(2, 42));
  EXPECT_EQ(answers, std::vector<int>{42});
  EXPECT_FALSE(call.pending());
}

TEST_F(AgentRequestTest, NotResentBeforeTimeout) {
  Send(100.0);
  EXPECT_FALSE(call.ResendIfOverdue(104.9));
  EXPECT_EQ(client->requests.size(), 1U);
  EXPECT_TRUE(call.pending());
}

TEST_F(AgentRequestTest, AnsweredRequestIsNeverResent) {
  Send(100.0);
  ASSERT_TRUE(client->Answer(1, 1));
  EXPECT_FALSE(call.ResendIfOverdue(200.0));
  EXPECT_EQ(client->requests.size(), 1U);
}

TEST_F(AgentRequestTest, LateAnswerOfAbandonedAttemptIsIgnored) {
  Send(100.0);
  ASSERT_TRUE(call.ResendIfOverdue(105.0));
  EXPECT_FALSE(client->Answer(1, 1));
  EXPECT_TRUE(answers.empty());
  EXPECT_TRUE(call.pending());
}

TEST_F(AgentRequestTest, KeepsResendingWhileResponsesAreLost) {
  Send(100.0);
  ASSERT_TRUE(call.ResendIfOverdue(105.0));
  EXPECT_FALSE(call.ResendIfOverdue(109.9));
  ASSERT_TRUE(call.ResendIfOverdue(110.0));
  EXPECT_EQ(client->waiting.size(), 1U);
}

TEST_F(AgentRequestTest, WaitsForServiceBeforeResending) {
  Send(100.0);
  client->ready = false;
  EXPECT_FALSE(call.ResendIfOverdue(105.0));
  client->ready = true;
  EXPECT_TRUE(call.ResendIfOverdue(106.0));
}

TEST_F(AgentRequestTest, CallbackMaySendTheNextRequest) {
  const auto next = std::make_shared<int>(8);
  call.Send(
      client, request,
      [this, next](const auto&) {
        call.Send(client, next, [this](const auto& r) { answers.push_back(*r); }, 101.0);
      },
      100.0);
  ASSERT_TRUE(client->Answer(1, 1));
  ASSERT_TRUE(call.pending());
  EXPECT_EQ(client->requests[1], next);
  ASSERT_TRUE(client->Answer(2, 9));
  EXPECT_EQ(answers, std::vector<int>{9});
  EXPECT_FALSE(call.pending());
}

}  // namespace
}  // namespace robot_simulation
