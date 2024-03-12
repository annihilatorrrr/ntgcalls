//
// Created by iraci on 09/03/2024.
//

#include "encryption.hpp"

#include <openssl/aes.h>
#include <openssl/sha.h>
#include <climits>

namespace openssl {
    bytes::binary Sha256::Digest(const bytes::binary& data) {
        auto bytes = bytes::binary(SHA256_DIGEST_LENGTH);
        SHA256(data, data.size(), bytes);
        return bytes;
    }

    bytes::binary Sha256::Concat(const bytes::span& first, const bytes::span& second) {
        bytes::binary result(SHA256_DIGEST_LENGTH);
        auto context = SHA256_CTX();
        SHA256_Init(&context);
        SHA256_Update(&context, first, first.size());
        SHA256_Update(&context, second, second.size());
        SHA256_Final(result, &context);
        return result;
    }

    bytes::binary Sha1::Digest(const bytes::binary& data) {
        auto bytes = bytes::binary(SHA_DIGEST_LENGTH);
        SHA1(data, data.size(), bytes);
        return bytes;
    }

    Aes::KeyIv Aes::PrepareKeyIv(const bytes::binary& key, const bytes::binary& msgKey, const int x) {
        auto result = KeyIv();
        const auto sha256a = Sha256::Concat(
            bytes::span(msgKey, 16),
            bytes::span(key + x, 36)
        );
        const auto sha256b = Sha256::Concat(
            bytes::span(key + 40 + x, 36),
            bytes::span(msgKey, 16)
        );
        const auto aesKey = result.key;
        const auto aesIv = result.iv;
        memcpy(aesKey, sha256a, 8);
        memcpy(aesKey + 8, sha256b + 8, 16);
        memcpy(aesKey + 8 + 16, sha256a + 24, 8);
        memcpy(aesIv, sha256b, 4);
        memcpy(aesIv + 4, sha256a + 8, 8);
        memcpy(aesIv + 4 + 8, sha256b + 24, 4);
        return result;
    }

    void Aes::ProcessCtr(const bytes::binary& from, const bytes::binary& to, KeyIv& keyIv) {
        auto aes = AES_KEY();
        AES_set_encrypt_key(keyIv.key, 32 * CHAR_BIT, &aes);
        const bytes::binary ecountBuf(AES_BLOCK_SIZE);
        uint32_t offsetInBlock = 0;
        AES_ctr128_encrypt(
            from,
            to,
            from.size(),
            &aes,
            keyIv.iv,
            ecountBuf,
            &offsetInBlock
        );
    }
} // openssl