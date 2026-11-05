# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC
# Malibu CPM clock state and rate tests

run_clk_state_test() {
  local group="$1"
  # Note: Requires fd 3 to be open (e.g. via bste_test_runner.sh).
  if ! ./clk_test_runner state "${group}" 3>&3; then
    fail "ERROR: Command failed: ./clk_test_runner state ${group}"
  fi
}

run_clk_rate_test() {
  local group="$1"
  # Note: Requires fd 3 to be open (e.g. via bste_test_runner.sh).
  if ! ./clk_test_runner rate "${group}" 3>&3; then
    fail "ERROR: Command failed: ./clk_test_runner rate ${group}"
  fi
}

test_clk_state_aoss_pg() { run_clk_state_test "aoss_pg"; }
test_clk_state_cpu() { run_clk_state_test "cpu"; }
test_clk_state_cpuacc() { run_clk_state_test "cpuacc"; }
test_clk_state_dpu() { run_clk_state_test "dpu"; }
test_clk_state_gpcm_amb() { run_clk_state_test "gpcm_amb"; }
test_clk_state_gpcm_infra() { run_clk_state_test "gpcm_infra"; }
test_clk_state_hsio_n() { run_clk_state_test "hsio_n"; }
test_clk_state_hsio_s() { run_clk_state_test "hsio_s"; }
test_clk_state_lsio_e() { run_clk_state_test "lsio_e"; }
test_clk_state_lsio_s() { run_clk_state_test "lsio_s"; }
test_clk_state_pcie() { run_clk_state_test "pcie"; }

test_clk_rate_aoss_pg() { run_clk_rate_test "aoss_pg"; }
test_clk_rate_cpu() { run_clk_rate_test "cpu"; }
test_clk_rate_cpuacc() { run_clk_rate_test "cpuacc"; }
test_clk_rate_dpu() { run_clk_rate_test "dpu"; }
test_clk_rate_gpcm_amb() { run_clk_rate_test "gpcm_amb"; }
test_clk_rate_gpcm_infra() { run_clk_rate_test "gpcm_infra"; }
test_clk_rate_hsio_n() { run_clk_rate_test "hsio_n"; }
test_clk_rate_hsio_s() { run_clk_rate_test "hsio_s"; }
test_clk_rate_lsio_e() { run_clk_rate_test "lsio_e"; }
test_clk_rate_lsio_s() { run_clk_rate_test "lsio_s"; }
test_clk_rate_pcie() { run_clk_rate_test "pcie"; }
