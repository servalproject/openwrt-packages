/* -------------------------------------------------------------------------
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
 * ---------------------------------------------------------------------- */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define SHA3_ASSERT( x )
#define SHA3_TRACE(format, args...)
#define SHA3_TRACE_BUF(format, buf, l, args...)

//#define SHA3_USE_KECCAK
/* 
 * Define SHA3_USE_KECCAK to run "pure" Keccak, as opposed to SHA3.
 * The tests that this macro enables use the input and output from [Keccak]
 * (see the reference below). The used test vectors aren't correct for SHA3, 
 * however, they are helpful to verify the implementation.
 * SHA3_USE_KECCAK only changes one line of code in Finalize.
 */

#if defined(_MSC_VER)
#define SHA3_CONST(x) x
#else
#define SHA3_CONST(x) x##L
#endif

/* The following state definition should normally be in a separate 
 * header file 
 */

/* 'Words' here refers to uint64_t */
#define SHA3_KECCAK_SPONGE_WORDS \
        (((1600)/8/*bits to byte*/)/sizeof(uint64_t))
struct sha3_context {
    uint8_t saved[8];             /* the portion of the input message that we
                                 * didn't consume yet */
    uint8_t s[SHA3_KECCAK_SPONGE_WORDS][8]; /* Keccak's state */
    unsigned byteIndex;         /* 0..7--the next byte after the set one
                                 * (starts from 0; 0--none are buffered) */
    unsigned wordIndex;         /* 0..24--the next word to integrate input
                                 * (starts from 0) */
    unsigned capacityWords;     /* the double size of the hash output in
                                 * words (e.g. 16 for Keccak 512) */
};

extern struct sha3_context ctx;

/* *************************** Public Inteface ************************ */

/* For Init or Reset call these: */
void sha3_Init256(void);
void sha3_Init384(void);
void sha3_Init512(void);
void sha3_Update(void *bufIn, size_t len);
void sha3_Finalize(void);
