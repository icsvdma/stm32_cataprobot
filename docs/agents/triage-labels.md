# Triage labels

スキルが使う役割名を、GitHub Issuesのラベルへ対応付ける。

| 役割 | ラベル | 意味 |
|---|---|---|
| needs-triage | `needs-triage` | 管理者による評価待ち |
| needs-info | `needs-info` | 追加情報待ち |
| ready-for-agent | `ready-for-agent` | 仕様がそろい、エージェントが対応可能 |
| ready-for-human | `ready-for-human` | 人による対応が必要 |
| wontfix | `wontfix` | 対応しない |

振り分け時はカテゴリラベル（`bug` または `enhancement`）と、上表の状態ラベルをそれぞれ1つ付ける。既存の状態ラベルが競合する場合は管理者へ確認する。
