#!/bin/sh
# PreToolUse hook (Glob|Grep): remind the agent that the code graph exists. Silent when there is no graph.
[ -f graphify-out/graph.json ] || exit 0
printf '%s\n' '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"graphify-out/graph.json exists. For structure questions (who calls X, how modules connect, impact of a change) run `graphify query \"...\"`, `graphify explain \"X\"`, `graphify path \"A\" \"B\"` or `graphify affected \"X\"` first; use Grep/Glob for exact strings. Read graphify-out/GRAPH_REPORT.md for the hubs."}}'
