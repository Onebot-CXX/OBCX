# Wave 4 Final Read-Only Validation Report
## Metadata
- **Scope:** `docs/research/im-platform-capabilities/`
- **Mode:** Final targeted read-only recheck; no files edited by this reviewer
- **Research baseline:** 2026-08-17
- **Review history:** Initial Wave 4 `FAIL` → B1–B5 corrections → `PASS WITH CAVEATS` revalidation → final DingTalk cleanup and synthesis-hash refresh
- **Evidence level:** Attested. Exact corrected lines were read directly. Shell results were executed by the sole writer and independently confirmed where noted; this reviewer did not claim to execute unavailable shell tools.
- **Web policy:** No URL fetching or exhaustive URL validation
- **Runtime policy:** No runtime, adapter-conformance, or external API tests
## Final Review
- **Correct:** All initial B1–B5 blocking findings remain resolved.
- **Correct:** The final DingTalk K19/P07 evidence qualification is present and internally consistent.
- **Correct:** The previously stale `component-proposal.md` synthesis hash has been refreshed and verified.
- **Correct:** All nine approved acceptance requirements pass.
- **Blocker:** None.
- **Note:** No remaining non-blocking integration defect was found. External platform evidence volatility remains a research risk, not a documentation caveat.
# Validation History
## Initial Wave 4 result
The initial validation returned `FAIL` for five integration findings:
1. Lark omission-only social capabilities remained definitive.
2. WeCom secondary-only claims remained asserted in the matrix and component catalog.
3. Five matrix cells contained duplicate explicit `A:` status tokens.
4. Consolidated authority labels and source-quality arithmetic were inconsistent.
5. X fixed-destination publishing was asserted without evidence.
A sole repository writer corrected those synthesis defects without modifying runtime code or immutable archived reports.
## B1–B5 correction revalidation
The subsequent read-only recheck confirmed:
- Lark social/feed cells became `P:?/A:?`.
- WeCom mirror-only customer/social/campaign cells became `A:?`.
- K13, K15, P05, and P06 became explicitly evidence- and discovery-qualified.
- Five duplicate API-status tokens were removed.
- X fixed-destination publishing became `P:?/A:?`.
- Discord/X access-limited labels and DingTalk S10’s `SECONDARY/DERIVED` classification were corrected.
- Source arithmetic became:
  - 193 web = 177 primary + 16 secondary/derived
  - 215 total = 199 primary/local + 16 secondary/derived
- The strict 48×9 matrix validator passed.
That recheck found only one non-blocking caveat: DingTalk K19/P07 did not locally repeat the matrix’s conservative treatment of S10-derived recall and status evidence.
# Final DingTalk Recheck
## K19 is now conservative
`component-proposal.md:126` now states:
- Matrix and Lark P2P have primary evidence.
- DingTalk send-status/read-status derives only from S10.
- The DingTalk capability is disabled by default.
- Status remains `UNKNOWN pending primary official verification`.
- Delivery failure is not treated as a read receipt.
This aligns with:
- `capability-matrix.md:80`, where DingTalk read receipt remains `A:?`;
- `sources.md:270`, where S10 is `SECONDARY/DERIVED`.
## P07 is now conservative
`component-proposal.md:230` now:
- reuses only primary-evidenced K01, K02, K09, K13–K15, K20, and K21;
- excludes K04 and K19 from the reusable primary-evidenced set;
- identifies DingTalk recall and send/read status as S10-derived;
- disables both by default; and
- preserves `UNKNOWN pending primary official verification`.
This aligns with `capability-matrix.md:44,80`.
## Counts remain unchanged
Directly confirmed:
- `component-proposal.md:13`: `13 + 21 + 12 + 10 = 56`
- `component-proposal.md:14`: Bridge MVP = 26
- `component-proposal.md:253`: `10 + 4 + 10 + 2 = 26`
The targeted writer validator also rechecked all ID ranges:
- D01–D13
- K01–K21
- R01–R12
- P01–P10
**Final DingTalk cleanup result: PASS**
# Manifest Hash Refresh
The targeted component edit initially made the manifest’s synthesis hash stale. This was treated as a real pre-publication blocker and was not accepted as PASS evidence.
The sole writer subsequently refreshed `audit-manifest.json`. The current recorded `component-proposal.md` SHA-256 is:
```text
85d32370fca0d0c7440db2bb1c070b69d7f836d93db2385207126d39955b17a9
```
It appears at:
- `audit-manifest.json:304`
- `audit-manifest.json:364`
Attested final checks passed:
- JSON parse
- actual-versus-recorded component hash
- all 5 synthesis-output hashes
- all 12 raw and 12 archived lane hashes
- 56/26 component arithmetic
- Git research-tree scope
- zero staged content
- manifest diff check
The supervisor independently reran the JSON parse and component actual-versus-recorded hash comparison; both passed.
**Hash-refresh result: PASS**
# Acceptance Results
| # | Requirement | Result | Evidence |
|---:|---|---|---|
| 1 | Exact tree and all expected reports | **PASS** | Pre-archive tree contains the expected 18 Wave 1–3 files. This report is suitable for the subsequent mechanical addition of `reviews/final-validation.md`. |
| 2 | Ten Wave 1 exact copies; two Wave 2 link-only transforms; manifest integrity | **PASS** | Attested checks: 10/10 `cmp`, 12/12 raw and 12/12 archive hashes, 2/2 review transformations, JSON parse. |
| 3 | Product/API separation, six statuses, matrix completeness, conservative corrections | **PASS** | B1–B5 remain corrected; strict validator passed 48×9 with exactly one valid P and A token per cell and no blanks. DingTalk derived claims remain unknown. |
| 4 | Exact D/K/R/P catalogs, arithmetic, duties, dependencies, protocols, pack boundaries | **PASS** | D13/K21/R12/P10 = 56; MVP = 26. K19/P07 now distinguish primary-evidenced and S10-derived DingTalk subsets. |
| 5 | OBCX process/actor/runtime consistency | **PASS** | Process-owned connection and endpoint, typed actor ingress/egress, explicit installation routing, and isolated OneBot compatibility remain consistent with inspected OBCX source. No runtime file changed. |
| 6 | Source completeness and quality labels; links; no fabricated claims | **PASS** | 193=177+16 and 215=199+16; access-limited/derived labels are normalized; recorded validator reports zero missing repository-relative links across 17 Markdown files. |
| 7 | Manifest hashes and artifact/transcript existence | **PASS** | Raw/archive hashes, transformations, transcripts, and all five synthesis hashes are attested as current. Component synthesis hash was independently confirmed after refresh. |
| 8 | Git scope only research tree; no staged/runtime content | **PASS** | Attested Git checks report research-tree-only diff and no staged content. Latest targeted edits affected documentation/manifest only. |
| 9 | Identify any factual, structural, counting, wording, citation, link, JSON, hash, or auditability defect | **PASS** | All discovered integration defects were corrected. No remaining integration defect was found in the final state. |
# Deterministic Command Evidence
These commands/checks were executed by the sole writer and recorded in the Wave 3 integration reports. They are cited transparently; this reviewer did not personally execute unavailable shell tools.
## Archive and manifest checks
- `python3 -m json.tool .../audit-manifest.json` — passed.
- Ten Wave 1 `cmp` comparisons — passed 10/10.
- Raw/archive SHA-256 validator — passed 12/12 raw and 12/12 archive.
- Review-transform validator — passed 2/2 with only `../wave1/` → `../agents/`.
- Transcript-path validator — passed 12/12.
## Matrix, sources, components, and links
- Strict matrix validator — passed:
  - 48 capability rows
  - 9 platform columns
  - no blank cells
  - exactly one valid P and one valid A status token per cell
- Source authority/count validator — passed:
  - 193=177+16
  - 215=199+16
- Component validator — passed:
  - D01–D13/K01–K21/R01–R12/P01–P10 = 56
  - MVP = 26
- Relative Markdown link validator — passed across 17 Markdown files.
## Final DingTalk and hash checks
- Targeted K19/P07 validator — passed.
- `component-proposal.md` file-specific diff check — passed.
- Post-caveat synthesis-hash refresh — passed.
- All five synthesis-output hashes — passed 5/5.
- Supervisor’s independent component hash comparison — passed.
## Git and formatting checks
- `git status --short`
- `git diff --name-only`
- `git diff --cached --name-only`
Recorded result: research-tree-only scope and zero staged content.
The full `git diff --check` reports only the two preserved Markdown hard breaks in immutable `reviews/evidence-audit.md:3-4`. The exact exception assertion passed, and the check excluding that archived review passed. Latest component and manifest file-specific checks passed.
# Archive Integrity and Publication Sequencing
- Wave 1 agent reports remain byte-identical.
- Wave 2 reviews remain link-only transformed archives.
- No runtime or configuration code changed.
- No `reviews/final-validation.md` was created before reviewer approval.
- README/manifest pre-archive wording correctly left final-review archival pending.
- After this PASS report is mechanically archived, the sole writer will add the Wave 4 audit metadata and revalidate the final tree. That unavoidable post-review bookkeeping is not an integration defect in the content validated here.
# Residual Platform Evidence Risks
These are external or implementation-time risks, not failures of the research integration:
1. **WeCom:** 12 of 18 web sources remain secondary mirrors; affected capabilities stay unknown pending direct official evidence.
2. **QQ:** active-send lifecycle and WebSocket status remain conflicted; active send stays disabled pending approved-app entitlement testing.
3. **X:** access tiers, Account Activity, search, DM, media, and quote entitlement conflicts require operation-specific probing.
4. **Feishu/Lark:** no authoritative feature-parity matrix exists; environment and operation must be discovered independently.
5. **DingTalk:** S10-derived recall and send/read-status capabilities remain disabled and unknown pending primary official evidence.
6. **Matrix:** optional profiles, deployment policy, E2EE interoperability, and MSC transitions require exact homeserver/client negotiation.
7. **Telegram:** Bot API 10.x adoption, webhook retry schedule, global rate details, and client compatibility remain dynamic.
8. **OBCX:** architecture findings are static-source conclusions; no runtime, shutdown-race, or adapter-conformance testing was performed.
9. No exhaustive URL validation was performed.
# Final Verdict
## **PASS**
All initial blockers, the later DingTalk wording caveat, and the temporary stale synthesis hash have been resolved. All approved acceptance requirements pass, no staged or runtime content is present, and no factual, structural, counting, wording, citation, link, JSON, hash, or auditability integration defect remains.
```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "No blocker or non-blocking integration defect remains. Final targeted evidence is at docs/research/im-platform-capabilities/component-proposal.md:126,230; conservative matrix counterparts are at capability-matrix.md:44,80; DingTalk S10 authority is at sources.md:270; and the refreshed component SHA-256 is at audit-manifest.json:304,364."
    }
  ],
  "changedFiles": [
    "docs/research/im-platform-capabilities/capability-matrix.md",
    "docs/research/im-platform-capabilities/component-proposal.md",
    "docs/research/im-platform-capabilities/sources.md",
    "docs/research/im-platform-capabilities/README.md",
    "docs/research/im-platform-capabilities/audit-manifest.json"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "python3 -m json.tool docs/research/im-platform-capabilities/audit-manifest.json",
      "result": "passed",
      "summary": "Executed by the sole writer; final manifest JSON parsed. The supervisor independently repeated the JSON parse."
    },
    {
      "command": "cmp each Wave 1 raw artifact with its agents/ archive",
      "result": "passed",
      "summary": "Sole-writer attestation records 10/10 byte-identical archives."
    },
    {
      "command": "python3 raw/archive hash, review-transform, and transcript validator",
      "result": "passed",
      "summary": "12/12 raw hashes, 12/12 archive hashes, 2/2 link-only review transforms, and 12/12 transcript paths passed."
    },
    {
      "command": "python3 strict 48x9 matrix validator",
      "result": "passed",
      "summary": "No blank cells and exactly one valid P and A status token per platform cell."
    },
    {
      "command": "python3 source authority/count, component arithmetic, and relative-link validators",
      "result": "passed",
      "summary": "193=177+16, 215=199+16, D13/K21/R12/P10=56, MVP=26, and zero missing repository-relative links."
    },
    {
      "command": "python3 targeted DingTalk K19/P07 validator",
      "result": "passed",
      "summary": "K04/K19 are excluded from P07's primary-evidenced reusable set, disabled, and UNKNOWN pending primary official evidence."
    },
    {
      "command": "python3 targeted post-caveat synthesis hash refresh",
      "result": "passed",
      "summary": "All five synthesis hashes match; component-proposal.md SHA-256 is 85d32370fca0d0c7440db2bb1c070b69d7f836d93db2385207126d39955b17a9."
    },
    {
      "command": "independent actual-versus-recorded component-proposal SHA-256 comparison",
      "result": "passed",
      "summary": "Executed by the supervisor; actual and manifest-recorded hashes match."
    },
    {
      "command": "git status --short; git diff --name-only; git diff --cached --name-only",
      "result": "passed",
      "summary": "Sole-writer checks report research-tree-only scope and no staged content."
    },
    {
      "command": "git diff --check",
      "result": "failed",
      "summary": "Reported only the two intentionally preserved Markdown hard breaks in archived reviews/evidence-audit.md."
    },
    {
      "command": "exact hard-break exception assertion and adjusted diff checks",
      "result": "passed",
      "summary": "The full-check exception matched exactly; excluding the immutable archive and checking the final component/manifest edits passed."
    },
    {
      "command": "nix fmt",
      "result": "not-run",
      "summary": "No commit was requested and the repository changes are documentation/audit artifacts only."
    }
  ],
  "validationOutput": [
    "PASS: initial B1-B5 defects remain resolved",
    "PASS: final DingTalk K19/P07 cleanup",
    "PASS: JSON and all 5 synthesis hashes",
    "PASS: 12/12 raw plus 12/12 archive hashes, 2/2 review transforms, 12/12 transcripts",
    "PASS: strict 48x9 matrix grammar and no blanks",
    "PASS: sources 193=177+16 and 215=199+16",
    "PASS: catalogs 13+21+12+10=56 and MVP 10+4+10+2=26",
    "PASS: repository-relative links, Git scope, and no staged content",
    "FINAL VERDICT: PASS"
  ],
  "residualRisks": [
    "External platform documentation and entitlements remain volatile and require implementation-time revalidation.",
    "WeCom secondary-only capabilities and DingTalk S10-derived K04/K19 remain UNKNOWN and disabled pending primary official evidence.",
    "No exhaustive URL, runtime, external API, adapter-conformance, or shutdown-race testing was performed.",
    "Mechanical archival of this report and addition of Wave 4 manifest metadata will be performed and revalidated after this content verdict."
  ],
  "noStagedFiles": true,
  "diffSummary": "Corrections are confined to the IM-platform research tree. Initial synthesis fixes touched matrix, component, sources, README, and manifest; final targeted work changed K19/P07 and refreshed the component synthesis hash. No runtime code or staged content is present.",
  "reviewFindings": [
    "no blockers",
    "no non-blocking integration defects",
    "expected immutable-archive exception: docs/research/im-platform-capabilities/reviews/evidence-audit.md:3-4 retains two Markdown hard breaks",
    "final verdict: PASS"
  ],
  "manualNotes": "This report supersedes the initial FAIL and intermediate PASS WITH CAVEATS reports. Shell evidence is transparently attributed to the sole writer and supervisor; the reviewer performed read-only file verification."
}
```
