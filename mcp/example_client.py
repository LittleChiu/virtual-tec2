# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2.0"]
# ///
"""
Minimal MCP client that connects to the TEC-2 MCP server, loads the custom
JNE microprogram, runs the "not equal" case and prints the result.

    uv run mcp/example_client.py
"""
import asyncio
import json
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main() -> None:
    server = str(Path(__file__).parent / "tec2_mcp_server.py")
    params = StdioServerParameters(command=sys.executable, args=[server])
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            print("tools:", ", ".join(t.name for t in tools.tools))

            await session.call_tool("load_custom_microcode", {"name": "JNE"})
            # JNE R2,R3,6 with R2=4, R3=9 (not equal) -> branch to MOV R0,2222
            await session.call_tool("write_memory", {"address": "3000", "words": [
                "2C00", "0000", "2C20", "0004", "2C30", "0009", "EC23", "0006",
                "2C00", "1111", "AC00", "0000", "2C00", "2222", "AC00"]})
            res = await session.call_tool("run", {"address": "3000"})
            state = json.loads(res.content[0].text)
            print("JNE not-equal -> R0 =", f'{state["registers"]["R0"]:04X}',
                  "| Z =", state["flags"]["Z"])


if __name__ == "__main__":
    asyncio.run(main())
