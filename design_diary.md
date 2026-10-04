# Design Diary

## 2026-10-03
- Read the brief and calculated the personalised values from IT24102206.
- Chose threads (pthread) for concurrency: simple, and threads share the log mutex easily.
- Each connection has its own buffer so partial lines and multiple lines per recv() are handled.

## 2026-10-04
- SYSINFO reads /proc/loadavg, /proc/meminfo and /proc/uptime (no external commands).
- LISTPROC reads /proc/<pid>/comm directly, so it does not depend on 'ps'.
- EXEC maps the name to a FIXED command string with strcmp; user text is never given to the shell, so injection like "DATE; ls" is rejected.

## 2026-10-04
- PUT/GET: the Agent first consumes bytes already sitting in the line buffer, then recv()s the rest, so exactly <filesize> bytes are read however recv() splits them.
- PUT writes to a hidden .part file and renames it on success, so a dropped connection never leaves a half file.
- Filenames are restricted to letters, digits, . _ - (no ../ traversal). 10 MB upload limit.
- Optional extension: Controller reports transfer throughput (bytes/sec) for PUT and GET.
