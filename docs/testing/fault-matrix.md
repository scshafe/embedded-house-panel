# Roadmap Step 8 fault matrix

The canonical record is [`fault-matrix.json`](fault-matrix.json). It maps each
fault row of roadmap Step 8 to the exact tests that prove it. Rows keep the
roadmap's order, and every citation is a file, a function and a needle that must
appear in that function's body. The table below is a summary of that record.
Where they differ, the JSON wins.

`python3 scripts/verification/check_fault_matrix.py` fails closed if:

- a row is missing or out of order;
- a cited file or function is missing, or its needle is not in the function body;
- a cited test is not wired into CMake, the Makefile, the verification scripts,
  a `unittest discover` directory, or the golden manifest;
- a real-Lexinet scenario did not pass in
  [`lexinet-live-contract-validation.json`](lexinet-live-contract-validation.json).

`make verify-docs` and `make verify` run the checker.
`scripts/verification/tests/test_panel_fault_matrix.py` holds its negative
cases.

| Row | Status | Host tests | Real Lexinet scenarios | What is still open |
|---|---|---|---|---|
| cold boot | covered | 5 | `create_resume` | — |
| untrusted clock | covered | 6 | — | — |
| sunrise/sunset rollover | covered | 3 | — | — |
| absence during work | covered | 4 | `create_resume`, `resume_after_server_restart` | — |
| suspend preparation | partial | 8 | — | `snd-aloop` guest run and s2idle attempt (Mac-local); microphone rail-off (FRDM) |
| crash/restart | covered | 5 | `crash_after_send`, `resume_after_server_restart` | — |
| offline queues | covered | 2 | `offline_queue`, `recover` | — |
| stale data | covered | 5 | `study_and_sync` | — |
| corrupted state | covered | 3 | `atomic_rejection` | — |
| disk full | covered | 1 | — | — |
| TLS failure | covered | 1 | — | real Tailnet certificate evidence (plan Step 7) |
| credential/capability denial | partial | 5 | `auth_failure` | real Tailnet grant, tag and revocation denial (plan Step 7) |

Every row also names a `vm_repeat` procedure for the Mac-local guest run (plan
Step 13, Verify item 2). The companion boundary is not implemented: agenda
feasibility Step 1 is a pending human safety review. All results are
`simulation_not_hardware_evidence`.
