# RemoteOps - IE3090 Network Programming Assignment

**Student Registration ID:** IT24103002

## Personalised Values

| Item | Value |
|------|-------|
| Agent Port | **9410** |
| Session ID (SID) | **2003** |
| Auth Token | **OPS-3002** |
| Agent Source | `agent_002.c` |
| Controller Source | `controller_002.c` |
| Makefile | `makefile_002` |
| Log File | `remoteops_IT24103002.log` |
| Storage Path | `./agentfiles/IT24103002/` |
| submission ZIP | `IE3090_IT24103002.zip` |

## How the Values Were calculated

Numeric part of `IT24103002` = `24103002`.

- **PORT** = 7000 + first 4 digits (2410) = **9410**
- **Source files** = last 3 digits (002) -> `agent_002.c`, `controller_002.c`, `Makefile_002`
- **SID** = last 4 digits (3002) reversed = **2003**
- **Log file** = `remoteops_<full regno>.log` = **remoteops_IT24103002.log**
- **Storage path** = `./agentfiles/<full regno>/` = **./agentfiles/IT24103002/**

## Build
```bash
make -f Makefile_002
