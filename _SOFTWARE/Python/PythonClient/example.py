"""
Example: TON 5s → FOR(10) → x = x + iter → cmp = (x > 15)
"""
from Code import Code
from Enums import mem_types_t

code = Code()

# ── Variables ──
code.var(mem_types_t.MEM_B, "enable", data=True)
code.var(mem_types_t.MEM_F, "x",      data=1.0)
code.var(mem_types_t.MEM_B, "cmp",    data=False)
code.var(mem_types_t.MEM_F, "table",    data=[1,1,1,1,1,2,2,2,2,2,11], dims=[100])
code.var(mem_types_t.MEM_F, "zero",      data=0)  # for iterator and intermediate results

# ── Blocks ──
ton  = code.add_clock(period_ms=10000, width_ms=2000, en="enable", alias="timer")
chk  = code.add_logic('"x" > 9', en="enable", alias="chk") 
code.add_set(target="x", value=0, en="chk[1]") 

calc = code.add_math('"x" + "table["table[x]"]"', en="timer[0]", alias="calc")
code.add_set(target="x",   value=4, en="timer[0]")
from BlockCounter import CounterMode

code.add_counter(start_val=0, step=1, limit_max=999, cu="enable", alias="iter", reset="chk[1]", mode=CounterMode.WHEN_ACTIVE)


# ── Generate ──
sub = code.subscribe("timer", "x", "cmp", "iter")
code.generate("example_dump.txt", subscriptions=sub)
code.print_blocks()

