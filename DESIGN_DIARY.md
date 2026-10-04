# RemoteOps Design Diary - IT24103002

## 2026-10-04 - Project setup
- Created GitHub repository: https://github.com/IT24103002/RemoteOps-IT24103002
- Created local folder ~/RemoteOps_IT24103002 and ran `git init`.
- Connected to remote with `git remote add origin ...`.
- Calculated personalised values:
  - Port: 7000 + 2410 = **9410**
  - SID: 3002 reversed = **2003**
  - Auth token: **OPS-3002**
  - Source files: `agent_002.c`, `controller_002.c`, `Makefile_002`
  - Log: `remoteops_IT24103002.log`
  - Storage: `./agentfiles/IT24103002/`
- Created `.gitignore`, `README.md`, `Makefile_002`.

## 2026-10-04 - Socket setup (Session 1)
- Wrote full socket setup in `agent_002.c`:
  socket() -> setsockopt(SO_REUSEADDR) -> bind() -> listen().
- Added a placeholder `controller_002.c` stub so the Makefile builds both.
- Next: implement the accept() loop with thread-per-client concurrency.
