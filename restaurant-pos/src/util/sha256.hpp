#pragma once
#include <array>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <string>

// Minimal SHA-256 (public domain style) for PIN hashing demos.
namespace rp::sha256 {

namespace detail {
inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint32_t bsig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t bsig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t ssig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t ssig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
}

inline std::string hash(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h0=0x6a09e667,h1=0xbb67ae85,h2=0x3c6ef372,h3=0xa54ff53a;
    uint32_t h4=0x510e527f,h5=0x9b05688c,h6=0x1f83d9ab,h7=0x5be0cd19;

    std::string msg = input;
    const uint64_t bitlen = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bitlen >> (i * 8)) & 0xff));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(uint8_t(msg[chunk + i*4])) << 24) |
                   (uint32_t(uint8_t(msg[chunk + i*4 + 1])) << 16) |
                   (uint32_t(uint8_t(msg[chunk + i*4 + 2])) << 8) |
                   (uint32_t(uint8_t(msg[chunk + i*4 + 3])));
        }
        for (int i = 16; i < 64; ++i)
            w[i] = detail::ssig1(w[i-2]) + w[i-7] + detail::ssig0(w[i-15]) + w[i-16];

        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4,f=h5,g=h6,h=h7;
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + detail::bsig1(e) + detail::ch(e,f,g) + K[i] + w[i];
            uint32_t t2 = detail::bsig0(a) + detail::maj(a,b,c);
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e; h5+=f; h6+=g; h7+=h;
    }

    std::ostringstream oss;
    auto out = [&](uint32_t v){ oss << std::hex << std::setfill('0') << std::setw(8) << v; };
    out(h0); out(h1); out(h2); out(h3); out(h4); out(h5); out(h6); out(h7);
    return oss.str();
}

inline std::string hash_pin(const std::string& pin, const std::string& salt) {
    return hash(salt + ":" + pin);
}

} // namespace rp::sha256
