# AI Prompt Log

| # | Date | Tool | Prompt (summary) | How output was used |
|---|---|---|---|---|
| 1 | 2026-10-03 | Claude | Asked for a step-by-step plan and my personalised values | Checked the values by hand against section 2.4 |
| 2 | 2026-10-03 | Claude | Asked for Agent skeleton (AUTH, QUIT, logging, framing) and a basic Controller | Typed and tested it, studied each function |
| 3 | 2026-10-04 | Claude | Asked for SYSINFO, LISTPROC and EXEC handlers | Applied via patch script, tested each command, read through the code |
| 4 | 2026-10-04 | Claude | Asked for PUT/GET with exact byte counting and a new Controller | Applied, tested with cmp and sha256sum, studied recv_bytes() |
| 5 | 2026-10-04 | Claude | Asked for UDP monitoring thread with start/stop | Applied patch, tested stream and stop, studied monitor_thread() |
| 6 | 2026-10-03 | Claude | Asked how to create the GitHub repo and the git author name/email | Followed the steps, created the repo, set git config |
| 7 | 2026-10-03 | Claude | Asked why git push needs a token, not my password | Created a Personal Access Token, push worked |
| 8 | 2026-10-04 | Claude | Asked why nc showed "Connection refused" | Learned the Agent must be running first; started it and retested |
| 9 | 2026-10-05 | Claude | Asked for the Part C plan (tests, screenshots, ZIP, submission files) | Followed it, ran every test myself |
