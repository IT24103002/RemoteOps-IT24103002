## 2026-10-04 - Session 2: Accept loop + threads
- Added `accept()` loop in `main()` to handle incoming connections.
- Chose **thread-per-client** concurrency with pthreads.
  - Justification: state per client is isolated, easy to reason about,
    and the brief only requires 5 simultaneous clients.
  - Alternative: select()/poll() — rejected as more complex for a
    beginner without benefit at this scale.
- Added thread-safe `log_message()` using pthread_mutex to prevent
  log corruption when multiple clients write at once.
- Added `handle_client()` thread function - for now it just echoes
  received data back; real protocol handlers in the next session.
- Added detached threads (`pthread_detach`) so no joins are needed.
- Tested:
  - Single client connects, echoes, disconnects cleanly.
  - Three clients connected simultaneously - all served at once.
  - `pkill -9 nc` on a client - agent did NOT crash; it logged the
    disconnect and continued listening on port 9410.
- Log file now contains connect/receive/disconnect entries with
  timestamps - verified with `cat remoteops_IT24103002.log`.
- Next: implement AUTH command and the line-based protocol framing.



## 2026-10-05 - Session 3: AUTH + line-based framing
- Implemented **line-based framing**: accumulate bytes in `line[]`,
  split on `\n`. Handles partial recv() and multiple lines in one recv().
- Added `send_line()` helper that appends ` SID:2003\n` automatically -
  so every response has the personalised tag.
- Added `authed` flag to `client_info_t` per client.
- Implemented AUTH command: compares against token `OPS-3002`.
  - Correct token: `OK AUTHENTICATED SID:2003`, authed=1.
  - Wrong token: `ERR 001 AUTH_FAILED SID:2003`, authed=0.
- Gate: any non-AUTH command before auth returns
  `ERR 001 AUTH_REQUIRED SID:2003` - as per §2.3.
- Implemented QUIT command: `OK BYE SID:2003` and clean disconnect.
- Any other command returns `ERR 999 NOT_IMPLEMENTED SID:2003`
  (temporary - real handlers in later sessions).
- Tested:
  - AUTH happy path -> OK AUTHENTICATED.
  - AUTH wrong token -> ERR 001.
  - SYSINFO before auth -> ERR 001 AUTH_REQUIRED.
  - Multiple lines pasted at once -> all processed correctly.
  - Partial line without \n -> agent buffers until newline arrives.
- Next: implement SYSINFO and LISTPROC.
