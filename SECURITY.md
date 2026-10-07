# Chaba / AgroIoT security policy

This document is the public vulnerability-disclosure contract for the
Chaba (AgroIoT) platform: the backend, the customer PWA, the ESP32/
ESP-01 device firmware (MQTT relay + ESP-NOW gateway/remote), the MCP
server, and the website/docs.

We take the security of the farms and homes running on Chaba
seriously. If you found something, thank you — please tell us
**privately** first, using the channels below.

## Reporting

| Channel | Where |
| --- | --- |
| Email | security@chaba.me (PGP key announced on the same address) |
| GitHub | https://github.com/chaba-dev-me/firmware/security/advisories (private advisories) |

What to include:

- the affected component (backend / PWA / firmware / MCP / infra) and
  version (`GET /health` for the backend, `FW_VERSION` prints on the
  device serial console);
- a minimal reproduction (steps, requests, or a device scenario);
- impact as you see it, and any suggested fix (none is required).

Do **not** include real customer data, real phone numbers, or real
credentials in your report.

## What happens next

1. We acknowledge within **2 business days**.
2. We triage, reproduce, and ship a fix — target: critical within
   **7 days**, high within **30 days**, everything else on the normal
   release cadence.
3. We publish a security advisory for anything user-facing
   (a GitHub advisory + a CHANGELOG entry). You choose whether to be
   credited.

## Scope and rules of engagement

In scope (production and the public repos):

- backend API (api.chaba.me, verified via doc.chaba.me/openapi.yaml)
- customer PWA (app.chaba.me)
- the MCP endpoint (mcp.chaba.me/mcp) and its tool surface
- device firmware and the device↔backend contracts (MQTT, bootstrap,
  ESP-NOW, WireGuard provisioning)
- the website and public docs surfaces

Out of scope:

- physical attacks beyond what the firmware threat model states
  (documented in firmware/.../DESIGN.md);
- social engineering of our staff or farmers;
- third-party services Chaba integrates with (Whop checkout, Meta
  WhatsApp, payment providers).

Rules: no denial-of-service testing against production, no actual
sending of WhatsApp/spam from the platform, no mass enumeration of
real phone numbers, no data destruction or modification of production
records. A vulnerable-by-construction finding is fine — a proof of
concept against your own sandbox tenant is preferred.

## Known hardening status (2026-10-07)

This reposponse follows a black-box red-team assessment
(`SECURITY_RED_TEAM_REPORT.md` in the source workspace). Summary of
controls shipped since:

- MQTT/device bootstrap traffic: transport-security runbook in
  `docs/ops/` (TLS on the broker listener, HTTPS bootstrap).
- ESP-NOW: per-pair HKDF LMKs (never a shared PMK; the old repo
  constant is removed), unknown-MAC and CCMP replay protection.
- Device secrets: flash/NVS encryption runbook for production boards;
  enrollment secrets are erased from storage after a successful claim.
- PWA access: magic links carry a single-use exchange code in the URL
  fragment, never a bearer.
- Operator actions: system-key minting requires explicit confirmation,
  optional TOTP second factor, and audit events to the log/SIEM hook.
- Public signup: the phone-number existence oracle is blinded to
  unauthenticated callers.

Please verify and improve these — that is the point of this file.

— The Chaba team