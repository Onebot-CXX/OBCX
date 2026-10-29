## 1. Evidence and continuous admission

- [x] 1.1 Record a metadata-only read-only audit mapping the 52 logged turns to tool calls/results, including the two slow send failures and limits of timing attribution.
- [x] 1.2 Remove activation/decline-count gating and mutations; preserve explicit operator switches, direct policy, ACLs, idempotency, echo exclusion and durable busy-session admission.
- [x] 1.3 Remove wake-word detection and alias storage; accept correctly typed retired options without requiring them; document old state/config compatibility.

## 2. Tool observability

- [x] 2.1 Add session-correlated, content-free execution/result-persistence timing with allowlisted tool labels and no changes to send/retry semantics.

## 3. Isolated verification and delivery

- [x] 3.1 Build a new isolated actor candidate with scripts/actor_build.py against the frozen installed SDK using all 20 available workers.
- [x] 3.2 Run real-runtime acceptance for ordinary cold-start messages, repeated declines, legacy sleeping-state restart, busy arrivals, permissions/echo/deduplication, retired configuration and timing privacy; do not add unit tests.
- [x] 3.3 Run existing session, Codex, configuration and context-layout runtime regressions against the explicitly selected new candidate.
- [x] 3.4 Verify formatting/diffs, unchanged staged work and protected artifacts; record results and candidate path without deployment, operational configuration changes or restart.
