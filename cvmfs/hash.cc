/**
 * This file is part of the CernVM File System.
 */

#include "cvmfs_config.h"
#include "hash.h"

#include <alloca.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>

#include <cstdio>

using namespace std;  // NOLINT

#ifdef CVMFS_NAMESPACE_GUARD
namespace CVMFS_NAMESPACE_GUARD {
#endif

namespace shash {

const char *kSuffixes[] = {"", "", "-rmd160", ""};


bool HexPtr::IsValid() const {
  const unsigned l = str->length();
  if (l == 0)
    return false;
  const char *c = str->data();  // Walks through the string
  unsigned i = 0;  // String position of *c

  for ( ; i < l; ++i, ++c) {
    if (*c == '-')
      break;
    if ((*c < '0') || (*c > 'f') || ((*c > '9') && (*c < 'a')))
      return false;
  }

  // Walk through all algorithms
  for (unsigned j = 0; j < kAny; ++j) {
    const unsigned hex_length = 2*kDigestSizes[j];
    const unsigned suffix_length = kSuffixLengths[j];
    if (i == hex_length) {
      // Right suffix?
      for ( ; (i < l) && (i-hex_length < suffix_length); ++i, ++c) {
        if (*c != kSuffixes[j][i-hex_length])
          break;
      }
      if (i == l)
        return true;
      i = hex_length;
      c = str->data() + i;
    }
  }

  return false;
}


Algorithms ParseHashAlgorithm(const string &algorithm_option) {
  if (algorithm_option == "sha1")
    return kSha1;
  if (algorithm_option == "rmd160")
    return kRmd160;
  return kAny;
}


Any MkFromHexPtr(const HexPtr hex) {
  Any result;

  const unsigned length = hex.str->length();
  if (length == 2*kDigestSizes[kMd5])
    result = Any(kMd5, hex);
  if (length == 2*kDigestSizes[kSha1])
    result = Any(kSha1, hex);
  // TODO compare -rmd160
  if ((length == 2*kDigestSizes[kRmd160] + kSuffixLengths[kRmd160]))
    result = Any(kRmd160, hex);

  return result;
}


/**
 * Allows the caller to create the context on the stack.
 */
unsigned GetContextSize(const Algorithms algorithm) {
  switch (algorithm) {
    case kMd5:
      return sizeof(MD5_CTX);
    case kSha1:
      return sizeof(SHA_CTX);
    case kRmd160:
      return sizeof(RIPEMD160_CTX);
    default:
      LogCvmfs(kLogHash, kLogDebug | kLogSyslogErr, "tried to generate hash "
               "context for unspecified hash. Aborting...");
      abort();  // Undefined hash
  }
}

void Init(ContextPtr &context) {
  switch (context.algorithm) {
    case kMd5:
      assert(context.size == sizeof(MD5_CTX));
      MD5_Init(reinterpret_cast<MD5_CTX *>(context.buffer));
      break;
    case kSha1:
      assert(context.size == sizeof(SHA_CTX));
      SHA1_Init(reinterpret_cast<SHA_CTX *>(context.buffer));
      break;
    case kRmd160:
      assert(context.size == sizeof(RIPEMD160_CTX));
      RIPEMD160_Init(reinterpret_cast<RIPEMD160_CTX *>(context.buffer));
      break;
    default:
      abort();  // Undefined hash
  }
}

void Update(const unsigned char *buffer, const unsigned buffer_length,
            ContextPtr &context)
{
  switch (context.algorithm) {
    case kMd5:
      assert(context.size == sizeof(MD5_CTX));
      MD5_Update(reinterpret_cast<MD5_CTX *>(context.buffer),
                 buffer, buffer_length);
      break;
    case kSha1:
      assert(context.size == sizeof(SHA_CTX));
      SHA1_Update(reinterpret_cast<SHA_CTX *>(context.buffer),
                  buffer, buffer_length);
      break;
    case kRmd160:
      assert(context.size == sizeof(RIPEMD160_CTX));
      RIPEMD160_Update(reinterpret_cast<RIPEMD160_CTX *>(context.buffer),
                       buffer, buffer_length);
      break;
    default:
      abort();  // Undefined hash
  }
}

void Final(ContextPtr &context, Any *any_digest) {
  switch (context.algorithm) {
    case kMd5:
      assert(context.size == sizeof(MD5_CTX));
      MD5_Final(any_digest->digest,
                reinterpret_cast<MD5_CTX *>(context.buffer));
      break;
    case kSha1:
      assert(context.size == sizeof(SHA_CTX));
      SHA1_Final(any_digest->digest,
                 reinterpret_cast<SHA_CTX *>(context.buffer));
      break;
    case kRmd160:
      assert(context.size == sizeof(RIPEMD160_CTX));
      RIPEMD160_Final(any_digest->digest,
                      reinterpret_cast<RIPEMD160_CTX *>(context.buffer));
      break;
    default:
      abort();  // Undefined hash
  }
  any_digest->algorithm = context.algorithm;
}


void HashMem(const unsigned char *buffer, const unsigned buffer_size,
             Any *any_digest)
{
  Algorithms algorithm = any_digest->algorithm;
  ContextPtr context(algorithm);
  context.buffer = alloca(context.size);

  Init(context);
  Update(buffer, buffer_size, context);
  Final(context, any_digest);
}


  void Hmac(const string &key,
	    const unsigned char *buffer, const unsigned buffer_size,
	    Any *any_digest) 
{
    Algorithms algorithm = any_digest->algorithm;
    assert(algorithm != kAny);

    const unsigned block_size = kBlockSizes[algorithm];
    unsigned char key_block[block_size];
    memset(key_block, 0, block_size);
    if (key.length() > block_size) {
      Any hash_key(algorithm);
      HashMem(reinterpret_cast<const unsigned char *>(key.data()),
	      key.length(), &hash_key);
      memcpy(key_block, hash_key.digest, kDigestSizes[algorithm]);
    } else {
      if (key.length() > 0)
	memcpy(key_block, key.data(), key.length());
    }

    unsigned char pad_block[block_size];
    // Inner hash                                                                                                                                     
    Any hash_inner(algorithm);
    ContextPtr context_inner(algorithm);
    context_inner.buffer = alloca(context_inner.size);
    Init(context_inner);
    for (unsigned i = 0; i < block_size; ++i)
      pad_block[i] = key_block[i] ^ 0x36;
    Update(pad_block, block_size, context_inner);
    Update(buffer, buffer_size, context_inner);
    Final(context_inner, &hash_inner);

    // Outer hash                                                                                                                                     
    ContextPtr context_outer(algorithm);
    context_outer.buffer = alloca(context_outer.size);
    Init(context_outer);
    for (unsigned i = 0; i < block_size; ++i)
      pad_block[i] = key_block[i] ^ 0x5c;
    Update(pad_block, block_size, context_outer);
    Update(hash_inner.digest, kDigestSizes[algorithm], context_outer);

    Final(context_outer, any_digest);
  }

bool HashFile(const std::string filename, Any *any_digest) {
  FILE *file = fopen(filename.c_str(), "r");
  if (file == NULL)
    return false;

  Algorithms algorithm = any_digest->algorithm;
  ContextPtr context(algorithm);
  context.buffer = alloca(context.size);

  Init(context);
  unsigned char io_buffer[4096];
  int actual_bytes;
  while ((actual_bytes = fread(io_buffer, 1, 4096, file))) {
    Update(io_buffer, actual_bytes, context);
  }

  if (ferror(file)) {
    fclose(file);
    return false;
  }

  Final(context, any_digest);
  fclose(file);
  return true;
}


/**
 * Fast constructor for hashing path names.
 */
Md5::Md5(const AsciiPtr ascii) {
  algorithm = kMd5;
  const string *str = ascii.str;

  MD5_CTX md5_state;
  MD5_Init(&md5_state);
  MD5_Update(&md5_state, reinterpret_cast<const unsigned char *>(&(*str)[0]),
             str->length());
  MD5_Final(digest, &md5_state);
}


Md5::Md5(const char *chars, const unsigned length) {
  algorithm = kMd5;

  MD5_CTX md5_state;
  MD5_Init(&md5_state);
  MD5_Update(&md5_state, reinterpret_cast<const unsigned char *>(chars),
             length);
  MD5_Final(digest, &md5_state);
}


Md5::Md5(const uint64_t lo, const uint64_t hi) {
  algorithm = kMd5;
  memcpy(digest, &lo, 8);
  memcpy(digest+8, &hi, 8);
}

void Md5::ToIntPair(uint64_t *lo, uint64_t *hi) const {
  memcpy(lo, digest, 8);
  memcpy(hi, digest+8, 8);
}

}  // namespace hash

#ifdef CVMFS_NAMESPACE_GUARD
}
#endif
