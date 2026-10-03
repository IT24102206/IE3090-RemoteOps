# Design Diary

## 2026-10-03
- Read the brief and calculated the personalised values from IT24102206.
- Chose threads (pthread) for concurrency: simple, and threads share the log mutex easily.
- Each connection has its own buffer so partial lines and multiple lines per recv() are handled.
