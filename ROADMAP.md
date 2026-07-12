# Roadmap

The plan implementing ADR-134: HAVEN-FSK the protocol and this
application get separate identities. HAVEN-FSK becomes a published
wire format others can implement; the app becomes a POTA/SOTA/Field
Day-focused digital operating and logging platform with HAVEN-FSK as
its native mode.

Phases are ordered by dependency, not calendar. Later phases may pull
individual items forward when they're cheap; earlier phases gate the
work that builds on them.

## Phase 1 — Protocol v1.0: freeze and publish

Leads because third-party adoption has lead time and this work is
nearly code-free.

- [x] Expand `HAVEN-FSK_Specification.md` to implementer grade (rev. 5):
      preamble-not-Gray-coded, exact CRC input, MSB-first bit
      conventions, permuted-systematic codeword bit order,
      interleaved-bits-to-symbols packing, reference TX envelope/phase.
- [x] Publish the LDPC(192,96) parity matrix machine-readably:
      `spec/ldpc_h_192_96.alist` + `spec/ldpc_generator_192_96.txt`
      (generator mapping — H alone cannot convey the bit placement).
- [x] Golden test vectors generated from this codebase
      (`tools/spec_artifacts`, round-trip-verified through the real
      demodulator): `spec/test_vectors/tv1..tv3` .txt + .wav, covering
      single-block, interleaved multi-block, and inline markers.
- [x] Spec §6.2 explicitly defers wire-format-changing wishlist items
      (adaptive FEC rate, narrow weak-signal variant) to a future
      VERSION; HAVEN-E noted as a payload convention needing no wire
      change (ADR-132).
- [ ] Freeze gate: on-air validation with outside testers.
      (PreambleSync sensitivity work is RX-side only and does NOT
      block the freeze.)

## Phase 2 — Platform foundation

Before any new mode is added.

- [ ] IModem capability model: framed vs streaming, sender
      identification, declared-log-data support. UI/logging read
      capabilities, never mode names.
- [ ] Tier-3 manual logging: select RX text → right-click → "Log as
      Call / Name / QTH / Grid / RS-R / POTA / SOTA / State / County"
      context menu; shape-guessed field floated to top. Mode-universal
      and the correction mechanism for HAVEN auto-fills.
- [ ] Streaming RX path: how a character-stream mode renders, and when
      shape-detection links run (line break / idle gap). PSK31 is the
      test case.
- [ ] PSK31 hardening: loopback self-test (mirroring
      MfskLoopbackSelfTest); the outstanding fldigi interop re-test.

## Phase 3 — Activity-first UX

Gated on on-air feedback from outside operators (standing UI hold —
scope is decided then, not now). Candidates, not commitments:

- [ ] Activation session setup (my park/summit entered once).
- [ ] Multi-state park filename suffix (last small logging item).
- [ ] Evaluate: spot integration (pota.app / SOTAwatch), activation
      stats in the log panel.

## Phase 4 — Additional modes

Only after Phase 2 has proven the abstraction on both existing modes.
Entry bar for any mode: serves field-activity operating (ADR-134
criterion), implements the capability model, ships with a loopback
self-test.

- [ ] Olivia (strongest field-utility case: robust in poor conditions).
- [ ] JS8 = implement the mode in-platform if pursued, not a bridge to
      the JS8Call application.

## Phase 5 — v1.0 release

- [ ] App rename/rebrand executes here (protocol keeps "HAVEN-FSK").
- [ ] Cross-platform truth: Linux/Raspberry Pi builds verified,
      ideally GitHub Actions CI.
- [ ] Reliability soak: multi-day RX, audio device hot-unplug.
- [ ] "Save diagnostic bundle" menu item (tester aid).
- [ ] User docs.
- [ ] Resolve remaining decide-or-cut items (Hamlib stub → probably
      cut).

## Parallel track — weak-signal RX sensitivity

Independent of all phases: RX-side only, never touches the wire
format. Bench harness (`--bench`) is the gate for every attempt;
ADR-129/130 record four measured-and-rejected approaches — do not
re-try those. PreambleSync is the established sole bottleneck.
