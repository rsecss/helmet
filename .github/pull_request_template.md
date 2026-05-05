## 变更说明

<!-- 简述这个 PR 做了什么、为什么 -->

## 变更类型

- [ ] feat: 新增功能
- [ ] fix: 修复 Bug
- [ ] refactor: 重构（不改变外部行为）
- [ ] perf: 性能优化
- [ ] docs: 文档更新
- [ ] test: 测试相关
- [ ] chore: 杂项 / 配置 / 构建
- [ ] ci: CI 相关

## 自检 Checklist

- [ ] PR 标题符合 [Conventional Commits](https://www.conventionalcommits.org/)（如 `feat(asrpro): xxx`）
- [ ] 涉及 APP 代码：本地 Keil 编译通过、SWD 烧录验证
- [ ] 新增 / 修改模块：源文件放 `APP/`、头文件 include 进 `APP/bsp_system.h`、Keil 工程已添加路径
- [ ] 模块边界守住：调用方不直接操作其他模块的 GPIO 或私有状态
- [ ] **CHANGELOG.md 已更新 `[Unreleased]` 段**（feat / fix / perf / refactor 类必须）
- [ ] 涉及外设 / 引脚 / 架构变化：已同步 `README.md` 与 `CLAUDE.md`
- [ ] 编码规范：UTF-8 无 BOM、LF 行尾、头文件守卫齐全

## 关联

<!-- 关联 issue、设计文档、Trellis 任务等。无则填 N/A -->

---

> **发版 PR？** 请改用模板：[在 PR URL 末尾追加 `?template=release.md`](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/creating-a-pull-request-template-for-your-repository#creating-multiple-pull-request-templates)
