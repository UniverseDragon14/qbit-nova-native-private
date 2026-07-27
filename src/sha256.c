#include "qn.h"
#include <string.h>

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
} SHA256Ctx;

static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void compress(SHA256Ctx *c, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7],cc=c->h[2];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch=(e&f)^((~e)&g);
        uint32_t t1=h+S1+ch+K[i]+w[i];
        uint32_t S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj=(a&b)^(a&cc)^(b&cc);
        uint32_t t2=S0+maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d;
    c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}

static void init(SHA256Ctx *c) {
    static const uint32_t iv[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memcpy(c->h, iv, sizeof(iv));
    c->bits = 0;
    c->used = 0;
}

static void update(SHA256Ctx *c, const uint8_t *data, size_t len) {
    c->bits += (uint64_t)len * 8u;
    while (len) {
        size_t take = 64 - c->used;
        if (take > len) take = len;
        memcpy(c->block + c->used, data, take);
        c->used += take;
        data += take;
        len -= take;
        if (c->used == 64) {
            compress(c, c->block);
            c->used = 0;
        }
    }
}

static void final(SHA256Ctx *c, uint8_t out[32]) {
    c->block[c->used++] = 0x80;
    if (c->used > 56) {
        while (c->used < 64) c->block[c->used++] = 0;
        compress(c, c->block);
        c->used = 0;
    }
    while (c->used < 56) c->block[c->used++] = 0;
    for (int i = 7; i >= 0; --i) c->block[c->used++] = (uint8_t)(c->bits >> (i * 8));
    compress(c, c->block);
    for (int i = 0; i < 8; ++i) {
        out[i*4]=(uint8_t)(c->h[i]>>24);
        out[i*4+1]=(uint8_t)(c->h[i]>>16);
        out[i*4+2]=(uint8_t)(c->h[i]>>8);
        out[i*4+3]=(uint8_t)c->h[i];
    }
}

void qn_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    SHA256Ctx c;
    init(&c);
    update(&c, data, len);
    final(&c, out);
}
