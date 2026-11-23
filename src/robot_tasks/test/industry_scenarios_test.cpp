// 校验随包发布的 industry_scenarios.yaml：每个工作流的每一步都能在站点文件上解析出有效路径。
// 原来由两个 industry 验收用例把 11 个模板逐个完整跑一遍（约 13 分钟）来间接检查；模板之间只有站点
// 不同、执行路径相同，所以把"模板数据有效"挪到这里，验收里只按执行形态各跑一个
// （acceptance-suite-rework design D2）。
// 站点文件两份都查：任务中枢默认的 warehouse_stations.yaml，以及演示栈用的 amr_sim_stations.yaml。
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "robot_tasks/scenario_catalog.hpp"
#include "robot_tasks/station_catalog.hpp"

namespace robot_tasks {
namespace {

const std::filesystem::path kSourceDir{ROBOT_TASKS_SOURCE_DIR};

ScenarioCatalog LoadIndustry() {
  const auto catalog = LoadScenarioCatalog(kSourceDir / "config/scenarios/industry_scenarios.yaml");
  EXPECT_TRUE(catalog.has_value());
  return catalog.value_or(ScenarioCatalog{});
}

// 返回工作流里无法解析的步骤说明，空表示全部有效。
std::vector<std::string> UnresolvedSteps(
    const ScenarioCatalog& scenarios, const StationCatalog& stations) {
  std::vector<std::string> problems;
  auto check_leg = [&](const std::string& where, const std::string& from, const std::string& to) {
    if (!FindRoutePath(stations, from, to).has_value()) {
      problems.push_back(where + ": no route " + from + " -> " + to);
    }
  };
  for (const auto& workflow : scenarios.workflows) {
    for (const auto& task_id : workflow.task_ids) {
      const std::string where = workflow.scenario_id + "/" + workflow.workflow_id + "/" + task_id;
      const auto* task = FindScenarioTask(scenarios, workflow.scenario_id, task_id);
      if (task == nullptr) {
        problems.push_back(where + ": task not found");
        continue;
      }
      if (task->task_type == "station_transport") {
        check_leg(where, task->pickup_station_id, task->dropoff_station_id);
      } else if (task->task_type == "station_sequence") {
        if (task->station_sequence_ids.size() < 2) {
          problems.push_back(where + ": station sequence shorter than 2");
        }
        for (size_t i = 1; i < task->station_sequence_ids.size(); ++i) {
          check_leg(where, task->station_sequence_ids[i - 1], task->station_sequence_ids[i]);
        }
      } else if (task->task_type != "facility_action") {
        problems.push_back(where + ": unknown task_type " + task->task_type);
      }
    }
  }
  return problems;
}

TEST(IndustryScenariosTest, CatalogIsValidAndHasAllTemplates) {
  const auto scenarios = LoadIndustry();
  std::string message;
  EXPECT_TRUE(ValidateScenarioCatalog(scenarios, &message)) << message;
  // warehouse 的 inbound_crossdock 加两个 industry 用例覆盖的 11 个行业模板。
  EXPECT_GE(scenarios.workflows.size(), 12U);
  for (const auto& workflow : scenarios.workflows) {
    EXPECT_FALSE(workflow.task_ids.empty()) << workflow.scenario_id << "/" << workflow.workflow_id;
  }
}

TEST(IndustryScenariosTest, EveryWorkflowStepRoutesOnWarehouseStations) {
  const auto stations =
      LoadStationCatalog(kSourceDir / "config/stations/warehouse_stations.yaml");
  ASSERT_TRUE(stations.has_value());
  const auto problems = UnresolvedSteps(LoadIndustry(), *stations);
  EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problems);
}

TEST(IndustryScenariosTest, EveryWorkflowStepRoutesOnAmrSimStations) {
  const auto stations = LoadStationCatalog(
      kSourceDir / "../robot_simulation/config/amr_sim_stations.yaml");
  ASSERT_TRUE(stations.has_value());
  const auto problems = UnresolvedSteps(LoadIndustry(), *stations);
  EXPECT_TRUE(problems.empty()) << ::testing::PrintToString(problems);
}

}  // namespace
}  // namespace robot_tasks
