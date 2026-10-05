# IE3090 RemoteOps (Network Programming Assignment)

**Registration number:** IT24102206

## Personalised values
| Item | Value |
|---|---|
| Agent port | 9410 (7000 + 2410) |
| Source files | agent_206.c, controller_206.c, Makefile_206 |
| SID tag | SID:6022 (2206 reversed) |
| Auth token | OPS-2206 |
| Log file | remoteops_IT24102206.log |
| Storage path | ./agentfiles/IT24102206/<filename> |
| Submission ZIP | IE3090_IT24102206.zip |

## Build
    make -f Makefile_206

## Run
    ./agent_206
    ./controller_206 [host] [port]

## Features
AUTH, SYSINFO, LISTPROC, EXEC (whitelist: DATE, UPTIME, DISKFREE, HOSTNAME, WHOAMI), PUT, GET, MONITOR START/STOP (UDP), QUIT.
Optional extension implemented: throughput reporting (bytes/sec) for PUT and GET in the Controller.

## Design summary
- Concurrency: one pthread per Controller connection (detached); one extra thread per monitored session for UDP.
- Framing: per-connection buffer; one line = one command; PUT/GET move exactly <filesize> raw bytes.
- Every TCP reply and UDP datagram ends with SID:6022.
- Logging: timestamped, mutex-protected, written to remoteops_IT24102206.log.
- EXEC never passes user text to a shell; the name is mapped to a fixed command.

## Quick test
    ./agent_206                      # terminal 1
    ./controller_206 127.0.0.1 9410  # terminal 2
    AUTH OPS-2206
    SYSINFO
    PUT hello.txt
    GET hello.txt
    MONITOR START 9500
    MONITOR STOP
    QUIT

## Error codes
001 AUTH_FAILED, 002 COMMAND_NOT_ALLOWED, 003 NOT_AUTHENTICATED, 004 FILE_TOO_LARGE, 005 FILE_NOT_FOUND,
006 UNKNOWN_COMMAND, 007 PROC_UNAVAILABLE, 008 EXEC_FAILED, 009 BAD_ARGUMENTS, 010 BAD_FILENAME,
011 STORAGE_ERROR, 012 ALREADY_MONITORING, 013 NOT_MONITORING, 014 MONITOR_FAILED

## Files
agent_206.c, controller_206.c, Makefile_206, design_diary.md, prompt_log.md, test_results.md
