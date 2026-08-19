# faad2最新版更新検証メモ

## 日付

2026-08-19

## 結論

faad2の最新版（2.11.2）への更新は今回は見送る。

faad2自体の更新は可能だが、現在のautotools前提のビルドをCMake方式へ変更する必要がある。さらに、AACGainがfaad2内部の構造体とビット処理関数に依存しているため、AACGain側にも互換修正が必要になる。

## 検証内容

- 検証ブランチ: `codex/faad2-2.11.2-test`
- faad2: `2.11.2`（コミット `673a22a`）
- 対象環境: macOS
- mp4v2は従来のautotools方式のまま検証
- faad2はCMakeで静的ライブラリをビルド

検証用に以下の互換修正を行うと、AACGain本体のビルドまで成功した。

- `NeAACDecStruct.alloced_channels`を`sample_buffer_size`へ置換
- `faad_rewindbits()`を`faad_resetbits(ld, 0)`へ置換
- faad2のCMakeビルド・インストール処理をCMakeのExternalProjectへ追加
- ビルドスクリプトのfaad2取得判定を`CMakeLists.txt`基準へ変更

ビルド後、テスト用M4Aに対する`-r`書き換えと、書き換え後の`mp4info`解析も成功した。

## 見送る理由

- faad2更新はI/O書き換え速度を改善しない
- faad2のビルド方式変更が必要
- AACGainがfaad2の内部APIに依存しており、将来の更新でも追従が必要
- mp4v2は引き続きautotoolsを必要とするため、依存関係を減らせない
- FreeBSD実機での回帰検証が未実施

## 再検討時の条件

- AAC/M4Aの代表サンプルで旧版とゲイン値を比較する
- macOSとFreeBSDの両方でビルドする
- 解析、ゲイン適用、タグ削除、ファイル再読込を確認する
- faad2のCMake移行を正式な変更としてレビューする

参照: https://github.com/knik0/faad2/releases/tag/2.11.2
