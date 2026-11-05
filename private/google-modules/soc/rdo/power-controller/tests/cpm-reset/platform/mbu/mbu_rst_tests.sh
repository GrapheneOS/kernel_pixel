# SPDX-License-Identifier: GPL-2.0-only
# Copyright 2026 Google LLC
# Malibu CPM reset controller tests

run_rst_test() {
  local domain="$1"
  if ! ./rst_test_runner "${domain}"; then
    fail "Error verifying reset domain: ${domain}. See test.log for details."
  fi
}

test_rst_codec_3p() { run_rst_test "codec-3p-resets"; }
test_rst_aoss_pg_amb() { run_rst_test "aoss-pg-amb-resets"; }
test_rst_lsio_s() { run_rst_test "lsio-s-resets"; }
test_rst_lsio_e() { run_rst_test "lsio-e-resets"; }
# These domain names use underscores to match the naming in /d/cpm_rst.
test_rst_hsion() { run_rst_test "hsion_resets"; }
test_rst_hsios() { run_rst_test "hsios_resets"; }
test_rst_pcie() { run_rst_test "pcie-resets"; }
