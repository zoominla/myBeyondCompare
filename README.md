# mbc                       
[English](README.en.md) | 中文


**mbc** 是一个**对 [Beyond Compare](https://www.scootersoftware.com/bcomp.php) 的比较功能进行净室（clean-room）重写的项目，采用C++20 实现**。它以单个原生可执行文件发布，提供命令行界面、本地 Web 图形界面（`mbc gui`），主要功能：文件夹 / 文本 / 十六进制比较、三向合并、报告生成，以及 `BCSessions.xml` 会话兼容性 —— 底层不依赖任何第三方比较引擎。

### ✨ 特性亮点

- 🖥️ **单个二进制同时提供 CLI 与 GUI** —— 可通过 `make`（g++/clang++，C++20）、`build_win.bat`（Visual Studio 2022 / MSVC）或 `build_wsl.sh` 构建。
- 📂 **Beyond Compare 同级功能集** —— 文件夹比较树（过滤器、显示/选项、复制/删除、同步、报告）、并排文本比较并支持就地编辑、十六进制比较，以及带可编辑输出面板的三向合并。
- 🌐 **双语界面** —— 英文与中文，可在标签栏处运行时切换，并记忆在浏览器中。
- 📇 **会话兼容** —— 读写 `BCSessions.xml`；每次 GUI 比较都会自动记录到 `History` 会话中。
- ✅ **模糊测试** —— CLI 在系统存在 GNU `diff` / `patch` / `diff3` 时与其进行模糊比对；GUI 通过 CDP 驱动无头浏览器进行测试。
- 📄 **兼容 `Beyond Compare` 的退出码**。

## 目录

- [🚀 快速开始](#-快速开始)
  - [构建](#构建)
  - [测试](#测试)
  - [命令行](#命令行)
  - [图形界面](#图形界面)
- [💡 使用提示](#-使用提示)
- [🏛 架构](#-架构)
- [🛡️ 兼容性与测试](#️-兼容性与测试)
- [⚠️ 风险与限制](#️-风险与限制)
- [📄 许可证](#-许可证)
- [🙏 致谢](#-致谢)

## 🚀 快速开始

### 构建

```
make            # g++/clang++，C++20；生成 GUI 页面需要 python3
build_win.bat   # Visual Studio 2022 / MSVC
build_wsl.sh    # WSL
```

### 测试

```
MBC=./mbc python3 tests/test_mbc.py   # CLI + GUI API；系统存在 GNU diff/patch/diff3 时与其模糊比对
node tests/ui_cdp.js                  # GUI 界面测试（Windows，需要 Node + Chrome/Edge）；通过 CDP 驱动无头浏览器
```

### 命令行

运行 `mbc help` 查看完整选项列表。退出码遵循 `BComp.com`。

### 图形界面

```
mbc gui [LEFT [RIGHT]] [LEFT BASE RIGHT] [--port N] [--no-open] [--view dir|text|hex|merge] [--sessions FILE]
```

启动一个监听 127.0.0.1 的本地 Web 服务（随机端口、随机令牌、校验 Host 头），并打开默认浏览器。

| 区域 | 提供能力 |
|---|---|
| 标签页 | 多标签工作区；可通过"退出"标签按钮或 Ctrl+C 退出 |
| 首页 | 快速比较；会话管理器（读写 `BCSessions.xml`） |
| 文件夹比较 | 树状视图；过滤器、显示/选项、复制/删除、同步、报告 |
| 文本比较 | 并排显示、行内高亮、就地编辑、复制区段、撤销/重做、查找、编码/换行符、规则、保存、报告 |
| 十六进制比较 | 字节级比较 |
| 三向合并 | 带可编辑输出面板的合并视图 |

在 Windows 上，双击 `mbc.exe`（不带参数、从资源管理器启动）会直接打开 GUI；在终端中不带参数运行则打印帮助信息。`MBC_NO_OPEN=1` 可阻止打开浏览器。

会话：历史记录从进程当前目录下的 `BCSessions.xml` 读取（文件不存在时创建空文件）；从 GUI（或 `mbc session save`）保存的会话会写回该文件。在 GUI 中发起的每一次比较（文件夹 / 文本 / 十六进制 / 合并）也会自动记录到 `History` 会话文件夹中（最新的在前，最多 100 条，自动去重；从文件夹比较中打开的文件，或已按名称保存过的比较，不会被记录）。`--sessions FILE` 或环境变量 `MBC_SESSIONS` 可指定其他文件。

布局：在文本、文件夹、十六进制和合并视图中，每个面板默认等宽，带有可拖动分隔条（双击恢复等宽）和各自的横向滚动条。文件夹行带有可点击的 ▼/▶ 折叠标记，用于折叠 / 展开该文件夹。

语言：英文与中文。标签栏右上角的按钮可切换语言，选择结果保存在浏览器中（默认跟随浏览器语言）。

## 💡 使用提示

- **把会话文件放在你期望的位置。** 历史记录从进程当前目录下的 `BCSessions.xml` 读取并写回 —— 可用 `--sessions FILE` 或环境变量 `MBC_SESSIONS` 覆盖。
- **每次 GUI 比较都会记入 `History`** —— 最新的在前，最多 100 条，自动去重。从文件夹比较中打开的比较，或已按名称保存过的比较，不会被记录。
- **双击面板之间的分隔条** 可将各面板重置为等宽。
- **使用文件夹行上的 ▼/▶ 折叠标记** 来折叠 / 展开文件夹树。
- **从资源管理器启动即可进入 GUI** —— 在 Windows 上，双击不带参数的 `mbc.exe` 会打开 GUI；在终端中不带参数运行则打印帮助信息。
- **跳过浏览器** —— 设置 `MBC_NO_OPEN=1` 可只启动服务而不打开默认浏览器。
- **新增界面文案请同时提供两种语言** —— 界面文案以英文编写；`web/i18n.js` 中保存中文词典，并翻译 DOM（文本节点、提示气泡、占位符），切回英文时还原，且不影响文件名与文件内容。新增字符串请添加到该文件的 `I18N_ZH` / `I18N_PAT` —— 若存在未翻译的字符串，`tests/ui_scenarios5.js` 会失败。

## 🏛 架构

单个 C++20 可执行文件：包含 CLI 入口，以及一个提供 Web GUI 与其 JSON API 的本地 HTTP 服务。

| 模块（src/） | 职责 |
|---|---|
| `main` | 命令行 |
| `common` | 通用工具 |
| `textfile` | 编码 / 换行符 |
| `diff` | LCS/patience 行差异、行内差异、规则 |
| `merge` | 三向合并 |
| `hexcmp` | 十六进制比较 |
| `dircmp` | 文件夹比较、过滤器、同步 |
| `report` | 文本 / 文件夹报告 |
| `session` | `BCSessions.xml` 读写 |
| `script` | BC 风格脚本子集 |
| `json` | JSON |
| `httpd` | 本地 HTTP 服务 |
| `gui` / `gui_api` | Web GUI 及其 JSON API |

GUI 页面由 `embed.py` 从 `web/*` 生成到 `src/gui_page.cpp`（该文件已签入仓库；Makefile 会重新生成它）。实现方式为净室重写；脚本语法依据对 BC 脚本的通用了解编写，而非来自已安装的二进制程序。

## 🛡️ 兼容性与测试

| 检查项 | 验证方式 |
|---|---|
| CLI | 系统存在 GNU `diff` / `patch` / `diff3` 时与其模糊比对 —— `tests/test_mbc.py` 覆盖 CLI + GUI API |
| GUI | 通过 CDP 驱动无头浏览器 —— `tests/ui_cdp.js`（Windows，需要 Node + Chrome/Edge） |
| 退出码 | 遵循 `BComp.com` |

## ⚠️ 风险与限制

- **未实现：** FTP/云端、压缩包/注册表/图片/音频/表格比较，以及原生（非 Web）GUI。
- **尽力而为的脚本兼容性：** 脚本语法依据对 BC 脚本的分析编写，而非来自已安装的二进制程序。
- **构建前置条件：** 生成 GUI 页面需要 python3；GUI 界面测试在 Windows 上还需要 Node 和 Chrome/Edge。

## 📄 许可证

基于 [MIT 许可证](LICENSE.txt) 授权。

## 🙏 致谢

- [Beyond Compare](https://www.scootersoftware.com/bcomp.php)（Scooter Software）—— 本项目净室重写的参照产品。
- GNU `diff` / `patch` / `diff3` —— 用于模糊测试的参照基准。
