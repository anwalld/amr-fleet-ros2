#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace robot_simulation {

struct LeaseToken {
  std::uint64_t service_epoch = 0;
  std::uint64_t revision = 0;

  bool operator==(const LeaseToken& other) const {
    return service_epoch == other.service_epoch && revision == other.revision;
  }
  bool operator!=(const LeaseToken& other) const { return !(*this == other); }
};

// 客户端二元租约状态机（与 ROS 解耦，便于单测）：
// 1. 仅由 RequestRouteSegment 应答建立或切换活跃 service_epoch（并淘汰旧纪元令牌与已见最大代次）；
// 2. /traffic/grants 通知不切换活跃纪元，仅在 service_epoch == active_service_epoch_ 时才接纳
//    replan 触发或大于当前纪元已见最大代次的 granted 令牌；
// 3. pending_releases_ 按 (service_epoch, revision) 精确核销，防止旧释放在重占后误核销新待释放项。
class AgentLeaseTracker {
 public:
  std::uint64_t active_service_epoch() const { return active_service_epoch_; }
  const std::set<std::string>& held() const { return held_; }
  const std::map<std::string, LeaseToken>& held_leases() const { return held_leases_; }
  const std::map<std::string, LeaseToken>& pending_releases() const { return pending_releases_; }

  void ClearHeld() {
    held_.clear();
    held_leases_.clear();
  }

  void RestoreReconciledResources(const std::set<std::string>& resources) {
    held_ = resources;
    held_leases_.clear();
  }

  bool IsCurrentEpoch(const std::uint64_t epoch) const {
    return epoch != 0U && epoch == active_service_epoch_;
  }

  // 等待中的授予只触发一次权威重查，纪元尚未知的通知也只可唤起这次查询。
  bool CanHintGrant(const std::uint64_t epoch) const {
    return epoch != 0U && retired_service_epochs_.count(epoch) == 0;
  }

  bool AcceptRequestResponse(
      const std::uint64_t epoch, const bool granted,
      const std::vector<std::string>& ids, const std::vector<std::uint64_t>& revisions) {
    if (!CanHintGrant(epoch)) {
      return false;
    }
    if (epoch != active_service_epoch_) {
      if (active_service_epoch_ != 0U) {
        retired_service_epochs_.insert(active_service_epoch_);
        ClearHeld();
      }
      active_service_epoch_ = epoch;
      max_revision_in_epoch_.clear();
    }
    if (!granted) {
      return true;
    }
    if (ids.empty() || ids.size() != revisions.size()) {
      return false;
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
      const auto& id = ids[i];
      const auto revision = revisions[i];
      const auto known = MaxSeenRevision(id);
      if (revision == 0U || revision < known ||
          (revision == known && (held_.count(id) == 0 || pending_releases_.count(id) != 0))) {
        return false;
      }
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
      RecordLease(ids[i], revisions[i]);
    }
    return true;
  }

  // 离开等待后收到补授予时，只记录严格新代次，供调用方归还多余资源。
  bool AcceptUnneededGrant(
      const std::uint64_t epoch, const std::string& id, const std::uint64_t revision) {
    if (!IsCurrentEpoch(epoch) || revision <= MaxSeenRevision(id)) {
      return false;
    }
    RecordLease(id, revision);
    return true;
  }

  void QueueRelease(const std::vector<std::string>& ids) {
    for (const auto& id : ids) {
      if (held_.erase(id) == 0) {
        continue;
      }
      const auto lease = held_leases_.find(id);
      pending_releases_[id] = lease != held_leases_.end() ? lease->second : LeaseToken{};
      if (lease != held_leases_.end()) {
        held_leases_.erase(lease);
      }
    }
  }

  void AcknowledgeReleaseBatch(const std::vector<std::pair<std::string, LeaseToken>>& sent_batch) {
    for (const auto& [id, sent_token] : sent_batch) {
      const auto it = pending_releases_.find(id);
      if (it != pending_releases_.end() && it->second == sent_token) {
        pending_releases_.erase(it);
      }
    }
  }

 private:
  std::uint64_t MaxSeenRevision(const std::string& id) const {
    const auto found = max_revision_in_epoch_.find(id);
    return found == max_revision_in_epoch_.end() ? 0U : found->second;
  }

  void RecordLease(const std::string& id, const std::uint64_t revision) {
    max_revision_in_epoch_[id] = revision;
    held_.insert(id);
    held_leases_[id] = LeaseToken{active_service_epoch_, revision};
  }

  std::uint64_t active_service_epoch_ = 0;
  std::set<std::uint64_t> retired_service_epochs_;
  std::map<std::string, std::uint64_t> max_revision_in_epoch_;
  std::set<std::string> held_;
  std::map<std::string, LeaseToken> held_leases_;
  std::map<std::string, LeaseToken> pending_releases_;
};

}  // namespace robot_simulation
