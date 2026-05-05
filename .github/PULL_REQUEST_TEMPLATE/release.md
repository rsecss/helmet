## Release vX.Y.Z

> 本 PR 用于把 dev 上累积的 `[Unreleased]` 内容固化为正式版本。

## 发版 Checklist

### CHANGELOG.md
- [ ] 把 `[Unreleased]` 段重命名为 `[X.Y.Z] - YYYY-MM-DD`（用今天的日期）
- [ ] 在文件顶部新增空的 `[Unreleased]` 段，供下一轮累积
- [ ] 在文件底部 compare 链接区追加新版本对比链接：
      `[X.Y.Z]: https://github.com/rsecss/helmet/compare/vX.Y.Z-1...vX.Y.Z`
- [ ] 更新 `[Unreleased]` 的 compare 链接指向 `vX.Y.Z...HEAD`

### 版本号选择（SemVer）
- [ ] **MAJOR**（X）：不兼容的接口变更（罕见）
- [ ] **MINOR**（Y）：新增功能、向后兼容（多数发版）
- [ ] **PATCH**（Z）：仅 Bug 修复、向后兼容

### 提交规范
- [ ] 本 PR 仅含一个 `chore(release): vX.Y.Z` 提交（或在 squash 合并时使用此标题）
- [ ] PR 标题格式：`release: vX.Y.Z`

### 验收
- [ ] 本地预览 release note：`git cliff --unreleased --strip header`（仅看自动附录）
- [ ] CHANGELOG 主体通顺、无内部行话、面向使用者视角
- [ ] dev 分支已干净（无未提交改动）

## 合并后操作

```bash
# PR 合并到 main 后，本地同步
git checkout main && git pull origin main

# 在 main HEAD 上打 tag
git tag vX.Y.Z
git push origin vX.Y.Z

# CI 自动触发 release.yml：
#   1. 校验 tag 已合入 main
#   2. 校验 CHANGELOG.md 含 [X.Y.Z] 段
#   3. 跑质量门禁
#   4. 抽取 CHANGELOG 主体 + cliff 附录 → 创建 GitHub Release
```

## 关联

<!-- 关联本次发版的关键 PR / 任务 -->
