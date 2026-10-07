#include "hkdf.h"
#include <Crypto.h>

static const char INFO_TAG[] = "chaba-espnow-v1";

// HMAC-SHA256 via the core's BearSSL-backed crypto (Crypto.h,
// experimental::crypto::SHA256) — the ESP8266 core ships no mbedTLS.
// Signature: hmac(data, dataLen, key, keyLen, out, outLen).
static void hmac_sha256(const uint8_t* key, size_t keyLen,
                        const uint8_t* data, size_t dataLen,
                        uint8_t out[32]) {
  using experimental::crypto::SHA256;
  SHA256::hmac(data, dataLen, key, keyLen, out, 32);
}

bool hkdf_sha256(const uint8_t* ikm, size_t ikmLen,
                 const uint8_t* salt, size_t saltLen,
                 const uint8_t* info, size_t infoLen,
                 uint8_t* out, size_t outLen) {
  if (ikm == NULL || ikmLen == 0 || out == NULL ||
      outLen == 0 || outLen > 64 || infoLen > 32) {
    return false;
  }

  // Extract: PRK = HMAC-SHA256(salt, IKM). RFC 5869 pads a missing
  // salt with hash-length zeros; we always have a salt (gateway MAC),
  // but keep the RFC behaviour for the self-test's shape anyway.
  uint8_t prk[32];
  uint8_t zeros[32] = {0};
  const uint8_t* saltUse = (saltLen > 0) ? salt : zeros;
  size_t saltUseLen = (saltLen > 0) ? saltLen : 32;
  hmac_sha256(saltUse, saltUseLen, ikm, ikmLen, prk);

  // Expand: T(0)="" ; T(i)=HMAC(PRK, T(i-1) | info | i); OKM = first
  // L bytes of T(1)T(2)...
  uint8_t t[32];
  size_t tLen = 0;
  uint8_t counter = 1;
  size_t done = 0;
  uint8_t buf[32 + 32 + 1];
  while (done < outLen) {
    size_t n = 0;
    if (tLen > 0) {
      memcpy(buf, t, tLen);
      n = tLen;
    }
    if (infoLen > 0) {
      memcpy(buf + n, info, infoLen);
      n += infoLen;
    }
    buf[n++] = counter;
    hmac_sha256(prk, 32, buf, n, t);
    tLen = 32;
    size_t copy = outLen - done;
    if (copy > 32) copy = 32;
    memcpy(out + done, t, copy);
    done += copy;
    counter++;
  }
  return true;
}

bool espnow_lmk(const char* remoteEnrollSecret,
                const uint8_t gatewayMac[6], uint8_t lmkOut[16]) {
  if (remoteEnrollSecret == NULL || gatewayMac == NULL) return false;
  size_t len = strlen(remoteEnrollSecret);
  if (len == 0 || len > 96) return false;
  return hkdf_sha256((const uint8_t*)remoteEnrollSecret, len,
                     gatewayMac, 6,
                     (const uint8_t*)INFO_TAG, strlen(INFO_TAG),
                     lmkOut, 16);
}

bool hkdf_selftest() {
  // RFC 5869 appendix A.1
  static const uint8_t ikm[22] = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b};
  static const uint8_t salt[13] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c};
  static const uint8_t info[10] = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
  static const uint8_t okm[42] = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43,
    0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90,
    0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4,
    0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
    0x58, 0x65};

  uint8_t got[42];
  bool ok = hkdf_sha256(ikm, sizeof(ikm), salt, sizeof(salt),
                        info, sizeof(info), got, sizeof(got));
  if (ok) {
    if (memcmp(got, okm, sizeof(okm)) != 0) ok = false;
  }
  Serial.print("hkdf selftest (RFC 5869 A.1): ");
  Serial.println(ok ? "PASS" : "FAIL");
  return ok;
}
