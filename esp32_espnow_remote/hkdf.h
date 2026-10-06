#pragma once

#include <Arduino.h>

// Minimal HKDF-SHA256 (RFC 5869) built on mbedTLS HMAC primitives,
// plus the ESP-NOW LMK derivation from SPECIFICATION_ESP_NOW.md §4:
//
//   LMK(remote, gw_i) = HKDF-SHA256(
//       ikm  = remote.enroll_secret,
//       salt = MAC(gw_i),
//       info = "chaba-espnow-v1",
//       L    = 16)
//
// outLen must be <= 64 (two expand iterations cover L=42 for the
// RFC test vector and L=16 for the LMK).

bool hkdf_sha256(const uint8_t* ikm, size_t ikmLen,
                 const uint8_t* salt, size_t saltLen,
                 const uint8_t* info, size_t infoLen,
                 uint8_t* out, size_t outLen);

// Convenience wrapper: derive the 16-byte LMK for one (remote secret,
// gateway MAC) pair. Returns false if the secret is empty or too long.
bool espnow_lmk(const char* remoteEnrollSecret,
                const uint8_t gatewayMac[6], uint8_t lmkOut[16]);

// Runs RFC 5869 appendix A.1 (L=42) through hkdf_sha256 and compares
// the known OKM. Serial-print PASS/FAIL; returns the verdict. Reachable
// from the bench CLI as `selftest`.
bool hkdf_selftest();
