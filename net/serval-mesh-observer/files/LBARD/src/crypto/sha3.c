/* -------------------------------------------------------------------------
 * Based on the following: 
 *
 * Works when compiled for either 32-bit or 64-bit targets, optimized for 
 * 64 bit.
 *
 * Canonical implementation of Init/Update/Finalize for SHA-3 byte input. 
 *
 * SHA3-256, SHA3-384, SHA-512 are implemented. SHA-224 can easily be added.
 *
 * Based on code from http://keccak.noekeon.org/ .
 *
 * I place the code that I wrote into public domain, free to use. 
 *
 * I would appreciate if you give credits to this work if you used it to 
 * write or test * your code.
 *
 * Aug 2015. Andrey Jivsov. crypto@brainhub.org
 *
 *
 * Adapted for SDCC by Paul Gardner-Stephen, Feb 2017, 
 * paul.gardner-stephen@flinders.edu.au
 *
 * All 64-bit operations removed, and replaced with 8-bit operations.
 * ---------------------------------------------------------------------- */

#include "sha3.h"
#include "code_instrumentation.h"

static const  uint8_t keccakf_rndc[24][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01}, {0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x82},
  {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x8a}, {0x80,0x00,0x00,0x00,0x80,0x00,0x80,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x8b}, {0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x01},
  {0x80,0x00,0x00,0x00,0x80,0x00,0x80,0x81}, {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x09},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x8a}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x88},
  {0x00,0x00,0x00,0x00,0x80,0x00,0x80,0x09}, {0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x0a},
  {0x00,0x00,0x00,0x00,0x80,0x00,0x80,0x8b}, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x8b},
  {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x89}, {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x03},
  {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x02}, {0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x80},
  {0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x0a}, {0x80,0x00,0x00,0x00,0x80,0x00,0x00,0x0a},
  {0x80,0x00,0x00,0x00,0x80,0x00,0x80,0x81}, {0x80,0x00,0x00,0x00,0x00,0x00,0x80,0x80},
  {0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x01}, {0x80,0x00,0x00,0x00,0x80,0x00,0x80,0x08}
};

static const  uint8_t keccakf_rotc[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62,
    18, 39, 61, 20, 44
};

static const  uint8_t keccakf_piln[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20,
    14, 22, 9, 6, 1
};

 struct sha3_context ctx;

#if 0
 unsigned int report_counter=0;
void sha3_report_(uint8_t v[8],int line)
{
  uint32_t v1,v2;

  // Make line numbers match reference version
  switch(line) {
  case 121: line=126; break;
  case 130: line=133; break;
  case 142: line=143; break;
  case 150: line=151; break;
  case 154: line=155; break;
  case 160: line=161; break;
    
  }
  
  v1=(v[3]<<24)|(v[2]<<16)|(v[1]<<8)|(v[0]<<0);
  v2=(v[7]<<24)|(v[6]<<16)|(v[5]<<8)|(v[4]<<0);
  
  fprintf(stdout,"%x : %x : %x : %d\n",report_counter,v2,v1,line);
  report_counter++;

}
#define sha3_report(X) sha3_report_(X,__LINE__)
#else
#define sha3_report(X)
#endif

typedef union rotate64_ {
        uint8_t bytes[8];
} rotate64;
                                
 rotate64 rotate;

void rotate_left(void)
{
  LOG_ENTRY;

  uint8_t i, bit63 = rotate.bytes[7] >> 7;
  for (i = 7; i < 8; i--) {
    rotate.bytes[i] = rotate.bytes[i] << 1;
    if (i && (rotate.bytes[i-1] & 0x80)) {
      rotate.bytes[i] |= 1;             
    }
  }
  rotate.bytes[0] |= bit63;

  LOG_EXIT;
}

/* generally called after SHA3_KECCAK_SPONGE_WORDS-ctx->capacityWords words 
 * are XORed into the state s 
 */
uint8_t t[8], bc[5][8], t_out[8];
uint8_t n, j, r, b,round_num;
static void keccakf(void)
{
  LOG_ENTRY;

#define KECCAK_ROUNDS 24

  for (round_num = 0; round_num < KECCAK_ROUNDS; round_num++) {
    /* Theta */
    for (n = 0; n < 5; n++) {
      for (b = 0; b < 8; b++) {
        bc[n][b] = ctx.s[n][b];
        bc[n][b] ^= ctx.s[n + 5][b];
        bc[n][b] ^= ctx.s[n + 10][b];
        bc[n][b] ^= ctx.s[n + 15][b];
        bc[n][b] ^= ctx.s[n + 20][b];
      }
      sha3_report(bc[n]);
    }

    for (n = 0; n < 5; n++) {
      for (b = 0;b < 8; b++) {
        rotate.bytes[b] = bc[(n + 1) % 5][b];
      }
      rotate_left();
      for (b = 0;b < 8; b++) {
        t[b] = bc[(n + 4) % 5][b] ^ rotate.bytes[b];
      }
      for (j = 0; j < 25; j += 5) {
        for (b = 0;b < 8; b++) {
          ctx.s[j + n][b] ^= t[b];
        }
        sha3_report(ctx.s[j + n]);
      }
    }
        
    /* Rho Pi */
    for (b = 0; b < 8; b++) {
      t[b] = ctx.s[1][b];
    }

    for (n = 0; n < 24; n++) {
      j = keccakf_piln[n];
      for (b = 0; b < 8; b++) {
        bc[0][b] = ctx.s[j][b];
      }
      for (b = 0; b < 8; b++) {
        rotate.bytes[b]=t[b];
      }
      for (r = 0; r < keccakf_rotc[n]; r++) {
        rotate_left();
      }
      for (b = 0; b < 8; b++) {
        ctx.s[j][b] = rotate.bytes[b];
      }
      sha3_report(ctx.s[j]);
      for (b = 0; b < 8; b++) {
        t[b] = bc[0][b];
      }
    }

    /* Chi */
    for (j = 0; j < 25; j += 5) {
      for (n = 0; n < 5; n++) {
            for (b = 0; b < 8; b++) {
              bc[n][b] = ctx.s[j + n][b];
            }
            sha3_report(bc[n]);
      }
      for (n = 0; n < 5; n++) {
        for (b = 0; b < 8; b++) {
          ctx.s[j + n][b] ^= (~bc[(n + 1) % 5][b]) & bc[(n + 2) % 5][b];
        }
        sha3_report(ctx.s[j+n]);
      }
    }

    /* Iota */
    for (b = 0; b < 8; b++) {
      ctx.s[0][b] ^= keccakf_rndc[round_num][7-b];
    }

    sha3_report(ctx.s[0]);
  }
    
  LOG_EXIT;

}

/* *************************** Public Inteface ************************ */

/* For Init or Reset call these: */
void sha3_Init256(void)
{
  LOG_ENTRY;

  memset(&ctx, 0, sizeof(ctx));
  ctx.capacityWords = 2 * 256 / (8 * sizeof(uint64_t));

  LOG_EXIT;
}

void sha3_Init384(void)
{
  LOG_ENTRY;

  memset(&ctx, 0, sizeof(ctx));
  ctx.capacityWords = 2 * 384 / (8 * sizeof(uint64_t));

  LOG_EXIT;
}

void sha3_Init512(void)
{
  LOG_ENTRY;

  memset(&ctx, 0, sizeof(ctx));
  ctx.capacityWords = 2 * 512 / (8 * sizeof(uint64_t));

  LOG_EXIT;
}

 uint32_t old_tail;
 size_t words;
 uint32_t tail;
 size_t ii;
 uint8_t *buf;

void sha3_Update(void *bufIn, size_t len)
{

  LOG_ENTRY;

  do {

    if (! bufIn) {
      LOG_ERROR("bufIn is null");
      break;
    }

    /* 0...7 -- how much is needed to have a word */
    buf = bufIn;

    LOG_TRACE("called to update with: %s, %d", buf, (int) len);

    if (ctx.byteIndex >= 8) {
      LOG_ERROR("ctx.byteIndex >= 8");
      break;
    }

    if (ctx.wordIndex >= sizeof(ctx.s) / sizeof(ctx.s[0])) {
      LOG_ERROR("ctx.wordIndex >= sizeof(ctx.s) / sizeof(ctx.s[0])");
      break;
    }

    // An 8-bit oriented implementation makes this all much simpler!
    // We just add bytes, and run rounds whenever we have a multiple of
    // 8 bytes received.
    while (len--) {
      ctx.saved[ctx.byteIndex++] = *(buf++);
      if (ctx.byteIndex == 8) {   
        // Save complete word, and run keccakf()
        for (b = 0; b < 8; b++) {
          ctx.s[ctx.wordIndex][b] ^= ctx.saved[b];
        }
        ctx.byteIndex = 0;
        for (b = 0; b < 8; b++) {
          ctx.saved[b] = 0;
        }
        if(++ctx.wordIndex ==
           (SHA3_KECCAK_SPONGE_WORDS - ctx.capacityWords)) {
          keccakf();
          ctx.wordIndex = 0;
        }

      }
    }
  }
  while (0);

  LOG_EXIT;

}

/* This is simply the 'update' with the padding block.
 * The padding block is 0x01 || 0x00* || 0x80. First 0x01 and last 0x80 
 * bytes are always present, but they can be the same byte.
 */
 uint32_t t1;
 uint32_t t2;
 uint8_t word;

void sha3_Finalize(void)
{
  LOG_ENTRY;
  
  LOG_NOTE("called with %d bytes in the buffer", ctx.byteIndex);

  /* Append 2-bit suffix 01, per SHA-3 spec. Instead of 1 for padding we
   * use 1<<2 below. The 0x02 below corresponds to the suffix 01.
   * Overall, we feed 0, then 1, and finally 1 to start padding. Without
   * M || 01, we would simply use 1 to start padding. */

  for (b = 0; b < 8; b++) {
    ctx.s[ctx.wordIndex][b]^=ctx.saved[b];
  }

#ifndef SHA3_USE_KECCAK
  /* SHA3 version */
  ctx.s[ctx.wordIndex][ctx.byteIndex] ^= (ctx.saved[ctx.byteIndex] ^ 0x06);    
#else
  /* For testing the "pure" Keccak version */
  ctx.s[ctx.wordIndex][ctx.byteIndex] ^= ctx.saved[7] ^ 1; 
#endif

  ctx.s[SHA3_KECCAK_SPONGE_WORDS - ctx.capacityWords - 1][7] ^= 0x80;
  keccakf();

  LOG_EXIT;

    // return (ctx.sb);
}
