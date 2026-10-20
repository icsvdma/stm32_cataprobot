# Issue tracker: GitHub

課題・仕様は `icsvdma/stm32_cataprobot` のGitHub Issuesで管理する。操作にはGitHub CLI (`gh`) を使う。

## 基本操作

- 取得: `gh issue view <number> --json number,title,body,labels,comments`
- 一覧: `gh issue list --state open --json number,title,body,labels`。必要な状態・ラベルで絞り込む。
- 作成: `gh issue create --title "..." --body-file <path>`
- コメント: `gh issue comment <number> --body-file <path>`
- ラベル変更: `gh issue edit <number> --add-label "..." --remove-label "..."`
- 完了: `gh issue close <number>`

リポジトリ内で実行するか、`--repo icsvdma/stm32_cataprobot` を指定する。複数行の本文は一時ファイルに保存し、`--body-file` で渡す。

スキルの「issue trackerへ公開」はIssue作成、「チケット取得」はIssue取得として扱う。

## 関連付け

親子関係はGitHubのsub-issueを使う。利用できない場合は子Issue本文に `Part of #<parent>` を記載し、親本文のタスクリストに子を追加する。

依存関係はGitHubのissue dependencyを使う。利用できない場合は本文冒頭に `Blocked by: #<number>` を記載する。

## PRの扱い

**PRs as a request surface: no.**

外部PRは自動的な課題振り分けの対象に含めない。番号がIssueかPRか曖昧な場合は `gh pr view <number>` と `gh issue view <number>` で解決する。
