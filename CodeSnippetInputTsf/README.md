# Windows TSF 输入法组件

这个项目是真正的 Windows Text Services Framework (TSF) 输入法 DLL：注册后会以 **Code Snippet 输入法** 显示在任务栏的输入指示器和 `Win + 空格` 切换菜单中。它在被选中时根据用户正在输入的前缀实时显示纵向代码片段候选，不会像早期的全局键盘钩子那样在其他输入法启用期间也捕获按键。

## 候选窗操作

- 输入触发词的任意前缀后，候选窗会在文本光标附近显示最多 8 个匹配项。
- 按 `↑` / `↓` 移动选中项；按 `1`–`8` 直接选择对应候选。
- 按 `Space`、`Enter` 或 `Tab` 展开当前候选，也可直接用鼠标点击。
- 按 `Esc` 关闭候选窗。每项纵向显示触发词及模板说明；旧配置没有说明时显示代码正文预览。

候选项会按悬浮栏中选择的 Context 过滤；选择“全部 Context”时，候选说明前会标出模板所属 Context。

## 输入法悬浮栏

输入法激活后会尝试启动单实例悬浮栏。悬浮栏可拖动并保持置顶，能够：

- 从下拉列表切换当前 Context；
- 点击“管理器”打开完整模板管理界面；
- 点击关闭按钮退出悬浮栏及由它打开的管理器。

当前 Context 保存在 `%APPDATA%\CodeSnippetInput\active-context.txt`，每个宿主进程中的 TSF DLL 会在匹配候选时读取它，因此切换后无需重新注册输入法。

## 工作方式

```text
Windows 输入法栏 / Win + 空格
        │ 选择 Code Snippet 输入法
        ▼
TSF COM Text Service DLL ──读取──> %APPDATA%\CodeSnippetInput\templates.tsf
        ▲                                  ▲
        └─ TSF 编辑会话替换文本 ────────────┘ WPF 模板管理器点击“保存”时同步生成
```

TSF 把这个服务注册为简体中文（`zh-CN`）键盘文本服务；配置注册时默认启用，因此应在中文输入法列表和切换菜单中出现。

## 构建前提

开发或重新构建时需要以下组件：

- Visual Studio 2022 Build Tools 或 Visual Studio 2022，勾选“使用 C++ 的桌面开发”
- Windows 10/11 SDK
- CMake（可由 Visual Studio 安装）

构建 64 位版本：

```powershell
cmake -S .\CodeSnippetInputTsf -B .\build\tsf-x64 -A x64
cmake --build .\build\tsf-x64 --config Release
```

生成的 DLL 位于 `build\tsf-x64\Release\CodeSnippetInputTsf.dll`。将它复制到不会被移动或删除的安装目录，再注册：

```powershell
.\CodeSnippetInputTsf\install-input-method.ps1 -DllPath 'C:\你的安装目录\CodeSnippetInputTsf.dll'
```

本仓库已经提供使用相对路径的根目录脚本，也可以直接双击：

```text
install-input-method.bat
uninstall-input-method.bat
```

两个批处理文件通过 `%~dp0` 定位项目根目录，因此不要求先切换 PowerShell 或 CMD 的工作目录。
当前脚本安装 `publish\tsf-x64-literal\CodeSnippetInputTsf.dll`，悬浮栏与管理器位于 `publish\manager-context\CodeSnippetInput.exe`。

注册脚本会自动请求管理员权限，将 COM/TSF 配置写入 Windows 的标准系统输入法注册位置。若列表没有立刻刷新，注销并重新登录后再按 `Win + 空格`。

卸载前须先切换到其他输入法，然后运行：

```powershell
.\CodeSnippetInputTsf\uninstall-input-method.ps1 -DllPath 'C:\你的安装目录\CodeSnippetInputTsf.dll'
```

## 模板配置

运行 WPF 管理器并点击“保存”后，会生成 `%APPDATA%\CodeSnippetInput\templates.tsf`。这是 DLL 使用的 UTF-8 桥接文件，包含触发词、正文、启用状态、变量、说明及 Context；JSON 配置和 XML/ZIP 的导入导出仍由管理器负责。模板正文会逐字输出，任何 `$...$` 内容（包括两侧的 `$` 符号）都不会被替换或移除。

## 位数要求

一个进程只能加载同位数 DLL。上面的步骤为 64 位应用（大部分现代 Windows 应用）安装 x64 输入法。若需要支持 32 位旧程序，需以 `-A Win32` 再构建一份，并从 32 位 `C:\Windows\SysWOW64\regsvr32.exe` 注册该 DLL。

## 与旧的全局展开开关

安装并使用 TSF 版本后，请保持管理器中的“全局展开”关闭。TSF 输入法被选中时会自行展开；旧开关仅保留给尚未安装 DLL 时的兼容模式，若同时开启会使展开不再遵循输入法切换状态。
