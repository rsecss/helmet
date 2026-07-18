#!/usr/bin/env bash
# SmartHelmet 发版脚本 — 固化「单 main 主干 + tag 发版」流程，消除人为漂移。
#
# 用法: scripts/release.sh vX.Y.Z
#
# 自动完成:
#   1. 前置校验: main 分支、工作区干净、与远端同步、tag 未存在、[Unreleased] 非空
#   2. CHANGELOG.md 滚动: [Unreleased] → [X.Y.Z] - 今日日期，重建空 [Unreleased]，更新 compare 链接
#   3. 提交 chore(release): vX.Y.Z
#   4. 在该提交上打 annotated tag（保证 tag 永远落在 release 提交上）
#   5. 确认后推送 main 与 tag，触发 release.yml 自动发 GitHub Release
set -euo pipefail

die() { echo "错误: $*" >&2; exit 1; }

TAG="${1:-}"
[[ "$TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "用法: scripts/release.sh vX.Y.Z（收到: '${TAG}'）"
VERSION="${TAG#v}"
DATE="$(date +%F)"

cd "$(git rev-parse --show-toplevel)"

# ---- 前置校验 ----
[[ "$(git rev-parse --abbrev-ref HEAD)" == "main" ]] || die "必须在 main 分支上发版"
[[ -z "$(git status --porcelain)" ]] || die "工作区不干净，先提交或暂存所有改动"
git fetch origin main --tags
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || die "本地 main 与 origin/main 不一致，先 pull/push 同步"
git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null && die "tag ${TAG} 已存在"

grep -qF '## [Unreleased]' CHANGELOG.md || die "CHANGELOG.md 缺少 [Unreleased] 段"
UNRELEASED_BODY=$(awk '/^## \[Unreleased\]/{f=1;next} f&&/^## \[/{exit} f{print}' CHANGELOG.md | tr -d '[:space:]')
[[ -n "$UNRELEASED_BODY" ]] || die "[Unreleased] 段为空，没有可发版的内容"

PREV_TAG=$(sed -n 's#^\[Unreleased\]: .*/compare/\(v[0-9.]*\)\.\.\.HEAD$#\1#p' CHANGELOG.md)
[[ -n "$PREV_TAG" ]] || die "无法从 CHANGELOG.md 底部解析上一版本 compare 链接"
REPO_URL=$(sed -n 's#^\[Unreleased\]: \(.*\)/compare/.*#\1#p' CHANGELOG.md)

# ---- 滚动 CHANGELOG ----
sed -i "s|^## \[Unreleased\]\$|## [Unreleased]\n\n## [${VERSION}] - ${DATE}|" CHANGELOG.md
sed -i "s#^\[Unreleased\]: .*\$#[Unreleased]: ${REPO_URL}/compare/${TAG}...HEAD\n[${VERSION}]: ${REPO_URL}/compare/${PREV_TAG}...${TAG}#" CHANGELOG.md

echo "===== CHANGELOG.md 变更预览 ====="
git --no-pager diff CHANGELOG.md
echo "================================="
read -rp "确认发版 ${TAG}（commit + tag + push）? [y/N] " ANSWER
if [[ "$ANSWER" != "y" && "$ANSWER" != "Y" ]]; then
    git checkout -- CHANGELOG.md
    die "已取消，CHANGELOG.md 已还原"
fi

# ---- 提交、打 tag、推送 ----
git add CHANGELOG.md
git commit -m "chore(release): ${TAG}"
git tag -a "${TAG}" -m "Release ${TAG}"
git push origin main "refs/tags/${TAG}"

echo "✓ ${TAG} 已推送，release.yml 将自动校验并创建 GitHub Release"
echo "  进度: ${REPO_URL}/actions"
