# M4 Ops 首次完整共同验收基线

2026-11-26，北京时间，`merged-control-current-full`，源码 `58b9000-merged-59a37fbf`。八阶段通过，主验收 110/110。实际窗口 Ops 10/10。五分钟无人操作的实际发送计数为快照 300 次、站点及路段各 61 次、业务发布为零。外部封路到地图更新实测 169 毫秒，满足 5 秒判据。Alarm 同轮 3/3。

后续逐项比较使用原始 [Ops 基线 TSV](m4_ops_results.tsv)，SHA-256 `e3d210e54467c8bb3287a237d49de50634cd72b818a1b14823a485bad51778b3`。该文件与 [阶段 7 原始结果](nightly/2026-11-26-current-candidate-full-evidence/stage7_console_smoke/console_ops_results.tsv) 一致。[阶段 7 汇总](nightly/2026-11-26-current-candidate-full-evidence/stage7_console_smoke/summary.md) · [共同报告](nightly/2026-11-26-current-candidate-full-evidence/report.md) · [后验](nightly/2026-11-26-current-candidate-full-evidence/postcheck.json)

随交付保存的是共同冻结快照的 Ops、Alarm 原始结果、服务边界和终态报告。报告中其余专项的原始日志保存在候选工作树。交付提交使用 GitHub CI 验证。 [冻结清单](nightly/2026-11-26-current-candidate-full-evidence/source.manifest)
