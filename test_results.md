# Test Results (IT24102206, port 9410, SID:6022)

| # | Feature | What I did | Expected | Actual |
|---|---|---|---|---|
| 1 | Auth required | SYSINFO before AUTH | ERR 003 NOT_AUTHENTICATED SID:6022 | PASS - got ERR 003 NOT_AUTHENTICATED SID:6022 |
| 2 | Auth failure | AUTH wrongtoken | ERR 001 AUTH_FAILED SID:6022 | PASS - got ERR 001 AUTH_FAILED SID:6022 |
| 3 | Auth success | AUTH OPS-2206 | OK AUTHENTICATED SID:6022 | PASS - got OK AUTHENTICATED SID:6022 |
| 4 | SYSINFO | SYSINFO | OK SYSINFO cpu mem uptime SID:6022 | PASS - got OK SYSINFO 0.01 1375 765 SID:6022 (cpu load, memory used MB, uptime sec) |
| 5 | LISTPROC | LISTPROC | OK PROCS pid/name,... SID:6022 | PASS - got OK PROCS 1/systemd,2/kthreadd,... SID:6022 (note: list is cut at the 1900-char reply limit, so only the first processes appear) |
| 6 | EXEC allowed | EXEC DATE / UPTIME / DISKFREE / HOSTNAME / WHOAMI | OK EXEC_RESULT ... | PASS - EXEC DATE returned OK EXEC_RESULT Mon Oct 5 02:41:20 PM +0530 2026 SID:6022; UPTIME, DISKFREE, HOSTNAME and WHOAMI also returned OK EXEC_RESULT with SID:6022 |
| 7 | EXEC rejected | EXEC rm -rf / and EXEC DATE; ls | ERR 002 COMMAND_NOT_ALLOWED | PASS - EXEC rm -rf / and EXEC DATE; ls both returned ERR 002 COMMAND_NOT_ALLOWED SID:6022; rejection logged |
| 8 | PUT | PUT hello.txt, PUT random.bin | OK FILE_RECEIVED, files in agentfiles/IT24102206 | PASS - OK FILE_RECEIVED for hello.txt (32 bytes) and random.bin (200000 bytes) SID:6022; both listed in agentfiles/IT24102206 |
| 9 | GET byte-exact | GET random.bin, cmp + sha256sum | identical hashes | PASS - cmp printed IDENTICAL; sha256 c7f5ba7c... matched for original, downloaded and Agent copy |
| 10 | GET missing | GET nothere.txt | ERR 005 FILE_NOT_FOUND | PASS - got ERR 005 FILE_NOT_FOUND SID:6022 |
| 11 | PUT too large | PUT big.bin (11 MB) | ERR 004 FILE_TOO_LARGE | PASS - got ERR 004 FILE_TOO_LARGE SID:6022 (11000000 byte file rejected, not stored) |
| 12 | UDP monitor start | MONITOR START 9500 | datagram every 2 s with SID:6022 | PASS - got OK MONITOR_STARTED, then [UDP] SYSINFO ... SID:6022 every 2 s (uptime 853, 855, 857...) |
| 13 | UDP monitor stop | MONITOR STOP | datagrams stop | PASS - got OK MONITOR_STOPPED SID:6022; no new datagrams after the one already in flight |
| 14 | Partial line | AUTH OP ... S-2206 | one OK reply | PASS - got one reply OK AUTHENTICATED SID:6022 |
| 15 | Multiple lines in one packet | 3 commands at once | 3 replies | PASS - got 3 replies: OK AUTHENTICATED, OK SYSINFO, OK BYE, all with SID:6022 |
| 16 | 5 simultaneous clients | 5 parallel nc sessions | all served | PASS - 5 parallel clients each got 3 correct replies; log shows 5 overlapping connections |
| 17 | Ungraceful disconnect in PUT | connection dropped after 3 of 1000 bytes | Agent survives, no partial file | PASS - Agent kept running; log shows PUT FAILED cut.bin (connection lost); no cut.bin or .part file left in storage |
| 18 | Logging | cat log file | timestamped connections, commands, transfers | PASS - log has timestamped CONNECT, CMD, AUTH OK/FAILED, PUT OK, GET OK, MONITOR and DISCONNECT lines; token hidden |
