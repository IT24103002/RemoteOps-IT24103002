# RemoteOps Test Results — IT24103002

## Environment
- Agent: `./agent_002` on 127.0.0.1:9410
- Controller: `./controller_002`
- OS: Linux, gcc with -Wall -Wextra -pthread

## Test Matrix

| # | Test | Input | Expected | Actual | Result |
|---|------|-------|----------|--------|--------|
| 1 | AUTH success | `AUTH OPS-3002` | `OK AUTHENTICATED SID:2003` | Same | ✅ |
| 2 | AUTH failure | `AUTH wrong` | `ERR 001 AUTH_FAILED SID:2003` | Same | ✅ |
| 3 | Pre-auth gate | `SYSINFO` before AUTH | `ERR 001 AUTH_REQUIRED SID:2003` | Same | ✅ |
| 4 | SYSINFO | `SYSINFO` | `OK SYSINFO <cpu> <mem> <up> SID:2003` | Same | ✅ |
| 5 | LISTPROC | `LISTPROC` | `OK PROCS pid-name,... SID:2003` | Same | ✅ |
| 6 | EXEC DATE | `EXEC DATE` | `OK EXEC_RESULT <date> SID:2003` | Same | ✅ |
| 7 | EXEC UPTIME | `EXEC UPTIME` | `OK EXEC_RESULT <uptime> SID:2003` | Same | ✅ |
| 8 | EXEC DISKFREE | `EXEC DISKFREE` | `OK EXEC_RESULT <df> SID:2003` | Same | ✅ |
| 9 | EXEC HOSTNAME | `EXEC HOSTNAME` | `OK EXEC_RESULT <host> SID:2003` | Same | ✅ |
| 10 | EXEC WHOAMI | `EXEC WHOAMI` | `OK EXEC_RESULT <user> SID:2003` | Same | ✅ |
| 11 | EXEC rejected | `EXEC rm -rf /` | `ERR 002 COMMAND_NOT_ALLOWED SID:2003` | Same | ✅ |
| 12 | EXEC rejected | `EXEC cat /etc/passwd` | `ERR 002 COMMAND_NOT_ALLOWED SID:2003` | Same | ✅ |
| 13 | PUT | `put /tmp/test.txt` | `OK FILE_RECEIVED test.txt SID:2003` | Same | ✅ |
| 14 | GET | `get test.txt /tmp/downloaded.txt` | File saved, identical | BYTE-IDENTICAL | ✅ |
| 15 | GET missing | `get nonexistent.txt` | `ERR 005 FILE_NOT_FOUND SID:2003` | Same | ✅ |
| 16 | Path traversal PUT | `put x ../etc/passwd` | `ERR 006 INVALID_FILENAME SID:2003` | Same | ✅ |
| 17 | MONITOR START | `monitor start 5555` | `OK MONITOR_STARTED SID:2003` + UDP datagrams | Same | ✅ |
| 18 | MONITOR STOP | `monitor stop` | `OK MONITOR_STOPPED SID:2003` + UDP stops | Same | ✅ |
| 19 | Ctrl+C on controller | Press Ctrl+C | Clean shutdown, no leaked thread | Same | ✅ |
| 20 | Ungraceful disconnect | `pkill -9 nc` | Agent survives, logs disconnect | Same | ✅ |

## Notes
- All responses include the personalised SID tag `SID:2003` as required by §2.3.
- All commands except AUTH are gated behind successful authentication.
- PUT/GET operate on binary data using exact byte-count loops.
- UDP monitoring runs in its own thread, cooperatively shut down via an `active` flag.
