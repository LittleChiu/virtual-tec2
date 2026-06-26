# Virtual TEC-2

TEC-2 微程序计算机（AM2901/AM2910 位片式 + 微程序控制器）的软件模拟器：
一个贴近原版的命令行监控程序，一条可经 LDMC 动态装入的示例自定义指令，以及一个
让 AI 工具直接驱动模拟器的 **MCP 服务器**。

> **来源与致谢**
> 本仓库基于 [ri-char/VirtualTec2](https://github.com/ri-char/VirtualTec2)
> （把原版改造为可在 Linux/macOS 运行），而原版来自中山大学
> [zhouguiheng/VirtualTEC2](https://github.com/zhouguiheng/VirtualTEC2)。
> 仿真内核（`bit/chip/tec2`、`monitor.cpp`）版权归原作者所有，按 MIT 许可证使用。
> 本仓库在其之上新增：脚本化 CLI `tec2_cli`、示例自定义指令 JNE、headless 对照工具、MCP 服务器。

## 包含什么

| 组件 | 说明 |
|---|---|
| `bit/chip/tec2.{cpp,h}` | 仿真内核：位运算、AM2901/AM2910、整机（来自上游）。 |
| `monitor.cpp` | 原版风格的全屏 curses 监控程序（来自上游）。 |
| `tec2_cli.cpp` | 不依赖 curses 的脚本化 CLI，命令面贴近原版，并内置示例自定义指令 JNE 与一个 `--server` 行协议。 |
| `mcp/` | **MCP 服务器**——让 AI 工具接入并驱动模拟器。见 [`mcp/README.md`](mcp/README.md)。 |
| `headless_curses/` | 用桩 `curses.h` 把原版 `monitor.cpp` 编成 headless 版，用于与 `tec2_cli` 输出逐字对照。 |
| `*.ROM` | 标准控存 `MCR.ROM`、地址映射 `MAPROM.ROM`、指令助记表 `INST.ROM`。 |

## 编译

跨平台（CMake）：

```bash
cmake -S . -B build
cmake --build build
# 生成 tec2_cli；若装了 ncurses 还会生成 monitor
```

Windows / MSYS2 UCRT64（一键脚本）：

```powershell
.\build.ps1            # 编译 tec2_cli.exe（静态链接，无 DLL 依赖）
.\build_monitor.ps1   # 编译原版 curses monitor.exe（需 ncurses 包）
```

运行时确保三个 `.ROM` 文件在工作目录中。Windows 上 curses 版需要 MSYS2 的 ncurses：
`pacman -S mingw-w64-ucrt-x86_64-ncurses`。

## 三种用法

### 1) 脚本化 CLI `tec2_cli`

```powershell
.\tec2_cli.exe                # 交互模式（命令面贴近原版 monitor）
.\tec2_cli.exe demo.txt       # 脚本模式（启用扩展命令）
.\tec2_cli.exe --server       # 行协议常驻模式（供 MCP 服务器驱动）
```

核心命令：

| 命令 | 作用 |
|---|---|
| `A <addr> <inst>` | 汇编一条指令到指定地址 |
| `U [addr] [count]` | 反汇编 |
| `D [addr] [count]` | 查看内存（16 进制 + ASCII） |
| `R` / `R <reg>` / `R <reg> <val>` | 查看 / 修改寄存器 |
| `G [addr]` | 运行（以子程序方式，遇 RET/断点停止） |
| `T` / `P` / `TT` | 单步机器指令 / 步过 / 单步微指令 |
| `V` / `B [addr]` | 查看微指令状态 / 设断点 |
| `E [addr]` / `M <addr> <w...>` | 编辑内存 / 直接写内存（脚本模式） |
| `H v1 v2` | 十六进制加减 |
| `LOAD/SAVE/RESET/SAVEMCR` | 文件读写 / 复位 / 写回控存（脚本模式） |
| `S` / `Q` / `?` | 保存控存 / 退出 / 帮助 |

扩展命令（`M`、`R <reg> <val>`、`D/U <addr> <count>`、`LOAD/SAVE/RESET`、自定义指令助记符）
只在脚本模式与 `--server` 模式启用；交互模式刻意贴合原版只看首字符的行为。

### 2) 原版 curses 监控程序

```powershell
.\build_monitor.ps1
.\run_monitor.ps1
```

`monitor.exe` 由 `monitor.cpp` 原样编译，命令界面、帮助文本与交互流程以原版为准。
注意一个行尾差异：原版读 `INST.ROM` 后会无条件删掉每行最后一个字符，UCRT 文本模式把
`CRLF` 转成 `LF` 会导致 `NOP`→`NO`、`RET`→`RE`；`build_monitor.ps1` 会在 `monitor_runtime/`
里准备好适配该读取逻辑的 `INST.ROM`，`run_monitor.ps1` 从该目录启动。

### 3) MCP 服务器（让 AI 工具接入）

```bash
uv run mcp/tec2_mcp_server.py
```

把模拟器开放给支持 MCP 的 AI 工具 / 编辑器：复位、读写内存与寄存器、
汇编/反汇编、单步与微单步、运行、以及通过 LDMC 动态装入自定义微码。配置与工具清单见
[`mcp/README.md`](mcp/README.md)。

## 示例自定义指令：JNE

标准 ROM 不含自定义微码。仓库附带一条**最简单的示例指令 JNE**（4 条微指令），
用来演示如何在运行时通过 **LDMC** 把自定义微程序从内存装入控存。

| 指令 | 操作码 | 控存入口 | 格式 | 功能 |
|---|---|---|---|---|
| `JNE R2,R3,DISP` | 59 | 0x160 | 两字 | R2≠R3 则转移到 IP+DISP，否则顺序执行 |

微码映像见 [`mcp/microcode/jne.mcr`](mcp/microcode/jne.mcr)（格式 `mcr <控存地址> <B3> <B2> <B1> <B0>`）。
`tec2_cli` 内置该指令的汇编与反汇编；MCP 工具 `load_custom_microcode("JNE")` 可一键装入。
想新增自定义指令：把对应 `.mcr` 放进 `mcp/microcode/` 并在 MCP 服务器的 `CUSTOM` 字典登记其入口即可。

## 目录结构

```
bit/chip/tec2.{cpp,h}   仿真内核
monitor.cpp             原版 curses 监控程序
tec2_cli.cpp            脚本化 CLI（含 --server 模式 + 示例自定义指令 JNE）
headless_curses/        headless 对照工具
mcp/                    MCP 服务器、示例客户端、微码映像
*.ROM                   标准 MCR / MAPROM / INST
build*.ps1 run_monitor.ps1   Windows 一键脚本
CMakeLists.txt          跨平台构建
```

## 许可证

MIT（见 [LICENSE](LICENSE)）。沿用上游版权声明，新增部分同样以 MIT 发布。
