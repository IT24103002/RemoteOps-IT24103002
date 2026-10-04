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


## 2026-10-05 - Session 4: SYSINFO + LISTPROC
- Implemented `handle_sysinfo()` reading from /proc:
  - /proc/loadavg for 1-minute CPU load average.
  - /proc/meminfo for MemTotal and MemAvailable (used = total - available).
  - /proc/uptime for uptime in seconds.
- Implemented `handle_listproc()` using `popen("ps -eo pid,comm ...")`,
  limited to 20 processes, comma-separated output.
- Wired both commands into the dispatcher, gated behind AUTH.
- Tested:
  - AUTH then SYSINFO returns real values with SID:2003.
  - AUTH then LISTPROC returns real process list with SID:2003.
  - SYSINFO before AUTH rejected with ERR 001 AUTH_REQUIRED.
  - Multiple simultaneous clients served without blocking.
- Next: implement EXEC with strict whitelist.


## 2026-10-05 - Session 5: EXEC with strict whitelist
- Implemented `handle_exec()` with a **fixed whitelist** of five
  commands exactly as specified in §2.3:
    DATE -> date
    UPTIME -> uptime
    DISKFREE -> df -h
    HOSTNAME -> hostname
    WHOAMI -> whoami
- Safety decisions:
  - Only hardcoded literal strings are passed to popen(); the
    user-supplied name is NEVER concatenated into the shell command.
  - Any name not on the whitelist returns ERR 002 COMMAND_NOT_ALLOWED.
  - Empty argument is rejected.
  - Injection attempts like "DATE; rm -rf /" fail because the whole
    string does not match any whitelist entry.
- Wired EXEC into the dispatcher (auth-gated).
- Tested:
  - All five allowed commands return OK EXEC_RESULT with the SID tag.
  - Rejected: rm -rf /, cat /etc/passwd, ls, empty, DATE; whoami.
  - EXEC before AUTH -> ERR 001 AUTH_REQUIRED.
- Next: implement PUT file upload with exact byte-count handling.


## 2026-10-06 - Session 6: PUT file upload
- Implemented `handle_put_body()`:
  - Rejects path-traversal filenames (containing "/" or "..").
  - Rejects files over 10 MB (MAX_FILE_SIZE).
  - Reads **exactly** `filesize` bytes in a while-loop:
    recv() may return fewer bytes than requested, so we accumulate
    until we have the full count.
  - Stores under `./agentfiles/IT24103002/<filename>`.
  - Cleans up partial files on failure or disconnect mid-transfer.
- Wired PUT into the dispatcher; after PUT header, we skip the
  rest of the current buffer and call the raw-byte handler.
- Tested:
  - PUT hello.txt with 12 bytes -> OK FILE_RECEIVED, file matches
    via `cmp` (byte-identical).
  - PUT with path traversal -> ERR 006 INVALID_FILENAME.
  - PUT with missing size -> ERR 006 INVALID_FILENAME.
- Note: this implementation assumes the controller sends the PUT
  header and file body as separate logical messages (with a small
  delay). The controller (Session 10) will honour this. Fully
  robust "leftover bytes in buffer" handling would require a
  refactor to a per-connection buffered reader, planned as a
  possible improvement if time allows.
- Next: GET file download.


