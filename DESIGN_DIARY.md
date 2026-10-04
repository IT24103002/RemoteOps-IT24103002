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
