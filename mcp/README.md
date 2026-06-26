# TEC-2 MCP 服务器

通过 [Model Context Protocol](https://modelcontextprotocol.io) 把 TEC-2
微程序计算机模拟器开放给支持 MCP 的 AI 工具 / 编辑器。
客户端可以复位机器、读写内存与寄存器、汇编/反汇编 TEC-2 程序（含示例自定义指令
**JNE**）、单步与微单步、运行程序，并通过 **LDMC** 把自定义微程序动态装入控制存储器。

## 工作原理

服务器启动一个常驻的 `tec2_cli --server` 子进程（由 `tec2_cli.cpp` 静态编译，
无运行期 DLL 依赖），用一行一命令、以 `__TEC2_DONE__` 为结束哨兵的小协议通信。
机器的全部状态都保存在该进程里，跨工具调用持续存在。

```
MCP 客户端 ──stdio/JSON-RPC──> tec2_mcp_server.py ──行协议──> tec2_cli --server ──> TEC-2 仿真内核
```

## 准备

- **Python ≥ 3.10**。推荐用 [`uv`](https://docs.astral.sh/uv/)，它会按脚本头部的内联依赖自动安装 `mcp`。
- **C++17 g++** 用于编译 `tec2_cli`（Windows 上为 MSYS2 UCRT64 的 g++）。
  建议先编译一次：仓库根目录执行 `./build.ps1`，或 `cmake -S . -B build && cmake --build build`。
  若 `tec2_cli` 不存在，服务器会在首次启动时自动调用 `g++` 编译（需 g++ 在 PATH 中）。

## 运行

```bash
uv run mcp/tec2_mcp_server.py        # stdio 传输，供 MCP 客户端连接
```

冒烟测试（自带的示例客户端，装载 JNE 并运行“不相等”分支）：

```bash
uv run mcp/example_client.py
# tools: reset, read_registers, ...
# JNE not-equal -> R0 = 2222 | Z = 0
```

## 接入 MCP 客户端

把 `<REPO>` 换成本仓库的绝对路径。大多数 MCP 客户端（桌面端或命令行）都用如下 JSON 配置
（字段名 `mcpServers` 是通用约定；个别客户端的配置文件名或添加方式略有不同）：

```json
{
  "mcpServers": {
    "tec2": {
      "command": "uv",
      "args": ["run", "<REPO>/mcp/tec2_mcp_server.py"]
    }
  }
}
```

> 服务器按自身位置（`mcp/` 的上级目录）定位 `tec2_cli` 与三个 `.ROM`，
> 因此与客户端的工作目录无关。Windows 路径请用 `C:\\...\\VirtualTec2\\mcp\\tec2_mcp_server.py`。

## 工具一览

| 工具 | 说明 |
|---|---|
| `reset` | 复位机器（寄存器清零、SP=FFFF、重载标准微码）。 |
| `read_registers` | 读 16 个寄存器、SP/PC/IP、C/Z/V/S 标志、PC 处当前指令。 |
| `set_register(reg,value)` | 设寄存器（R0..R15 或 SP/PC/IP）。 |
| `write_memory(address,words)` | 向主存写入一串 16 位字。 |
| `read_memory(address,count_words)` | 读主存（含 ASCII 视图）。 |
| `assemble(start_address,instructions)` | 汇编 TEC-2 指令（含 MADD/MMOV/JNE）。 |
| `disassemble(address,count)` | 反汇编。 |
| `run(address?)` | 运行（以子程序方式，遇 RET/断点/死循环停止）。 |
| `step(count?)` | 单步若干条机器指令。 |
| `microstep()` | 单步一条微指令，返回微级状态。 |
| `view_microstate()` | 查看当前微指令状态（不执行）。 |
| `set_breakpoint(address?)` | 设/清断点。 |
| `hex_calc(value1,value2)` | 十六进制加减。 |
| `load_custom_microcode(name)` | 通过 LDMC 把内置示例指令 `JNE`（或 `ALL`）装入控存。 |
| `raw_command(command)` | 透传任意 `tec2_cli` 命令（高级逃生口）。 |

数值均为十六进制字符串。寄存器约定 SP=R4、PC=R5、IP=R6。

## 自定义指令示例：JNE

标准 ROM 不含自定义微码。仓库附带一条**最简单的示例指令 JNE**（4 条微指令），
用来演示如何通过 LDMC 在运行时把自定义微程序装入控存；先 `load_custom_microcode` 再使用。

| 指令 | 入口 | 格式 | 功能 |
|---|---|---|---|
| `JNE R2,R3,DISP` | 0x160 | 两字 | R2≠R3 则转移到 IP+DISP，否则顺序执行 |

完整例（不相等 → 转移到 `MOV R0,2222`）：

```
load_custom_microcode("JNE")
# JNE R2,R3,6 ; R2=4,R3=9 不相等 -> 转移到 MOV R0,2222
write_memory("3000", ["2C00","0000","2C20","0004","2C30","0009","EC23","0006",
                       "2C00","1111","AC00","0000","2C00","2222","AC00"])
run("3000")          # -> R0=2222, Z=0（不相等，转移成功）
```

> 想加更多自定义指令：把 `<name>.mcr`（格式 `mcr <控存地址> <B3> <B2> <B1> <B0>`）放进
> `mcp/microcode/`，并在 `tec2_mcp_server.py` 的 `CUSTOM` 字典里登记其控存入口即可。
>
> `load_custom_microcode` 使用内存 0x0900..（微码映像）与 0xFE00..（小装载程序）作暂存，
> 并保存/恢复 R1/R2/R3。`reset` 后需重新装载。
