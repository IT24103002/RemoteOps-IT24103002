# AI Prompt Log - IT24103002

Per assignment section 3, all substantive AI interactions are recorded here.

---

## Entry 1 - 2026-10-04 - Project setup and socket code
**Tool:** ChatGPT
**Stage:** Setup / planning / Session 1

**Prompt:**
> "I have an IE3090 RemoteOps assignment. Registration IT24103002.
> Explain as a beginner how to start. I've never used sockets before.
> Give me the socket setup code for the agent, personalised for me."

**Output summary:**
AI explained the agent/controller architecture, calculated the
personalised values (port 9410, SID 2003, token OPS-3002, files,
log, storage), and provided the socket setup code for agent_002.c.

**How I used it:**
- Verified every personalised value by hand with a calculator.
- Typed the socket code into agent_002.c myself, reading each line.
- Created the GitHub repo and initial project structure.

**What I changed/rejected:**
- AI initially suggested `select()` for concurrency; I will use
  pthreads instead because I understand threads better.
- All code was typed by me and will be tested before committing.
