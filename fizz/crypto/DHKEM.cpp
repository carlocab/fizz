/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <fizz/crypto/DHKEM.h>
#include <folly/io/Cursor.h>

namespace fizz {

uint16_t getDHKEMId(NamedGroup group) {
  switch (group) {
    case NamedGroup::secp256r1:
      return 0x0010;
    case NamedGroup::secp384r1:
      return 0x0011;
    case NamedGroup::secp521r1:
      return 0x0012;
    case NamedGroup::x25519:
      return 0x0020;
    default:
      throw std::runtime_error("ke: not implemented");
  }
}

Buf generateSuiteId(NamedGroup group) {
  std::unique_ptr<folly::IOBuf> buf = folly::IOBuf::copyBuffer("KEM");
  folly::io::Appender appender(buf.get(), 2);
  appender.writeBE<uint16_t>(getDHKEMId(group));
  return buf;
}

DHKEM::DHKEM(
    std::unique_ptr<KeyExchange> kex,
    size_t nSecret,
    NamedGroup group,
    std::unique_ptr<fizz::hpke::Hkdf> hkdf)
    : kex_(std::move(kex)),
      nSecret_(nSecret),
      group_(group),
      hkdf_(std::move(hkdf)) {}

std::unique_ptr<folly::IOBuf> DHKEM::extractAndExpand(
    std::unique_ptr<folly::IOBuf> dh,
    std::unique_ptr<folly::IOBuf> kemContext) {
  std::vector<uint8_t> eae_prkVec = hkdf_->labeledExtract(
      folly::IOBuf::copyBuffer(""),
      folly::Range("eae_prk"),
      std::move(dh),
      generateSuiteId(group_));
  folly::ByteRange eaePrk(eae_prkVec.data(), eae_prkVec.size());
  std::unique_ptr<folly::IOBuf> sharedSecret = hkdf_->labeledExpand(
      eaePrk,
      folly::Range("shared_secret"),
      std::move(kemContext),
      nSecret_,
      generateSuiteId(group_));
  return sharedSecret;
}

DHKEM::EncapResult DHKEM::encap(folly::ByteRange pkR) {
  kex_->generateKeyPair();
  std::unique_ptr<folly::IOBuf> dh = kex_->generateSharedSecret(pkR);
  std::unique_ptr<folly::IOBuf> enc = kex_->getKeyShare();

  std::unique_ptr<folly::IOBuf> kemContext = enc->clone();
  kemContext->prependChain(folly::IOBuf::copyBuffer(pkR));
  std::unique_ptr<folly::IOBuf> sharedSecret =
      extractAndExpand(std::move(dh), std::move(kemContext));

  return EncapResult{std::move(sharedSecret), std::move(enc)};
}

std::unique_ptr<folly::IOBuf> DHKEM::decap(folly::ByteRange enc) {
  std::unique_ptr<folly::IOBuf> dh = kex_->generateSharedSecret(enc);
  std::unique_ptr<folly::IOBuf> pkRm = kex_->getKeyShare();

  std::unique_ptr<folly::IOBuf> kemContext = folly::IOBuf::copyBuffer(enc);
  kemContext->prependChain(std::move(pkRm));

  std::unique_ptr<folly::IOBuf> sharedSecret =
      extractAndExpand(std::move(dh), std::move(kemContext));
  return sharedSecret;
}

} // namespace fizz
