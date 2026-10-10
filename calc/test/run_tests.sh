#!/bin/bash
# Host unit tests for calc/src/logic.c (run inside WSL/Linux or Git Bash with gcc).
set -e
cd "$(dirname "$0")/.."
gcc -std=c11 -Wall -Wextra -Isrc test/test_logic.c src/logic.c -o /tmp/test_logic
/tmp/test_logic
