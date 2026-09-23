# Code Snippet 输入助手

一个 Windows Code Snippet 管理器，可按 Context 组织模板，并与系统 TSF 输入法和 IntelliJ Live Templates ZIP/XML 互通。

## 已实现

- WPF 模板管理界面：用树状结构显示 Context 与模板；Context 可新建、删除、内联改名，模板也可自定义所属 Context。
- 管理器顶部提供官方网站入口，使用 Windows 默认浏览器打开 `https://lunacraftgames.fyi/csi/`。
- 全局展开：例如输入 `fori` 后按 `Tab`，会删除触发词并注入模板；全局监听默认关闭，需在界面显式启用。
- 模板正文逐字输出：任何 `$...$` 内容都会保留两侧的 `$` 符号，不执行变量替换。
- JSON 配置自动保存至 `%APPDATA%\CodeSnippetInput\templates.json`，可导入、导出。
- 可导入 IntelliJ IDEA Live Templates ZIP 或单个 XML；`templateSet/@group` 会映射为可切换的 Context。
- 导出 IntelliJ ZIP 时，每个 Context 独立生成一个 XML 文件，再统一压缩为 ZIP。
- 保存模板时会同步生成 TSF 输入法使用的桥接配置，包括候选窗显示的模板说明。
- `--toolbar` 启动模式提供可拖动的置顶悬浮栏，可切换当前 Context 并打开完整管理器。
- 管理器支持 English、Español (México)、Français、Deutsch、Italiano、Português (Brasil)、简体中文、繁體中文、日本語、한국어和 Tiếng Việt，并会保存用户选择的界面语言。
- 首次启动会根据 Windows 显示语言匹配全部受支持语言；地区变体按语言映射，无法匹配时使用 English。手动选择的语言会在后续启动时优先使用。

## 构建和运行

```powershell
dotnet build .\CodeSnippetInput\CodeSnippetInput.csproj
dotnet run --project .\CodeSnippetInput\CodeSnippetInput.csproj
```

## 兼容性范围

IntelliJ 的 XML 属性可无损保存的部分会保留；其中 `templateSet/group`、`name`、`value`、`description`、`variable/defaultValue`、启用的 `context/option` 可被读取。顶层 `group` 是本工具中用户可切换的 Context，模板内部的 `context/option` 作为 IntelliJ 适用范围单独保留。IDE 特有的代码语义判断、重格式化/缩短限定名以及 Groovy 变量表达式不能由系统级输入工具执行。

该 MVP 通过 Windows 低级键盘钩子捕获英文字母、数字和下划线触发词。它不试图替代中文/日文 IME 的组合输入；在这些输入法的候选词状态下，先结束候选词再按 `Tab` 使用模板。

## 在 Windows 输入法栏中切换

如果需要像系统输入法一样出现在任务栏的输入指示器与 `Win + 空格` 菜单中，并在输入时显示纵向代码片段候选，请构建并注册同目录上层的 [`CodeSnippetInputTsf`](../CodeSnippetInputTsf/README.md) 原生 TSF 文本服务。管理器每次保存时都会写入该服务读取的桥接配置；安装 TSF 版本后请不要同时启用本应用的“全局展开”。
