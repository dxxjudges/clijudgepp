# langs — CliJudge++ 界面语言包

本分支只存放界面语言包（`.cjl`），不放源码，新增/修订语言不需要重新发版。
应用内「显示语言 → 获取在线语言」会读取本分支：

- 列表：`https://api.github.com/repos/dxxjudges/clijudgepp/contents?ref=langs`
- 下载：`https://raw.githubusercontent.com/dxxjudges/clijudgepp/langs/<code>.cjl`

## 添加一种语言

1. 在分支根目录新建 `<code>.cjl`，文件名即语言代码（如 `ja.cjl`）。
2. 内容：

```json
{
  "meta": { "name": "ja", "display": "日本語", "version": "1.0.0" },
  "strings": {
    "gui": {
      "nav": { "overview": "概要" },
      "common": { "save": "保存", "cancel": "キャンセル" }
    }
  }
}
```

- `meta.display` 是下拉框里显示的名字，缺省用文件名。
- `strings.gui` 下的键为点路径（`nav.overview`），也可以直接写带点的扁平键。
- 未翻译的键按「语言包 → 内置英文 → 中文原文」回退，可以只翻译一部分。
- 完整键表见主分支 `src/i18n_en.inc`。

CLI 的 `strings`（`help` / `judge` 等）与界面无关，可一并保留，互不影响。

## 当前语言包

| 文件 | 名称 |
| --- | --- |
| `en.cjl` | English |
| `zh-CN.cjl` | 简体中文 |
