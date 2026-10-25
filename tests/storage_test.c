/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define __attribute__(x)   /* (the sources' .pool sections: no Mach-O section names) */

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */
static int fail_bytes = -1;            /* power loss after any programmed byte */
static int erase_error, write_protected;
static uint32_t io_calls;

static int st_read(uint32_t off, void *dst, uint32_t n) { io_calls++; memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    io_calls++;
    if (erase_error) return -8;
    if (!write_protected) memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    io_calls++;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++) {
        if (fail_bytes == 0) return -9;
        if (fail_bytes > 0) fail_bytes--;
        if (!write_protected) nor[off + i] &= s[i];
    }
    return 0;
}
#include "../firmware/src/storage.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PROJECT0 + 2, a, sizeof a);
    st_save(OBJ_PROJECT0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + 2, &h);
        nor[st_sector(OBJ_PROJECT0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PROJECT0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PROJECT0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PROJECT0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PROJECT0 + 2, got, sizeof got) < 0);
    bad += check("data stays in the Felucca regions",
                 st_sector(OBJ_SETTINGS, 1) + 4096 <= 0xFF000 && st_sector(OBJ_PROJECT0 + 3, 1) + 4096 <= 0xE0000 &&
                     st_sector(OBJ_UPRESET0, 0) >= 0xDC000 && st_sector(OBJ_UPRESET0 + 1, 1) + 4096 <= 0xE0000);
    /* the user presets' FM6 patches (1.0.3) in the two sectors of the retired FM6 bank: 0x9F000 (after the projects)
     * and 0xFE000 (after the settings); the two objects share them, told apart by the commit record's type */
    bad += check("FM6 user preset patches (and the retired bank) in the sectors 0x9F000 / 0xFE000",
                 OBJ_UPFM6 == OBJ_AUTOSAVE - 1 && OBJ_FM6BANK == OBJ_UPFM6 - 1 && st_sector(OBJ_UPFM6, 0) == 0x9F000u &&
                     st_sector(OBJ_PROJECT0 + 3, 1) + 4096 == 0x9F000u && st_sector(OBJ_UPFM6, 1) == 0xFE000u &&
                     st_sector(OBJ_FM6BANK, 0) == 0x9F000u && st_sector(OBJ_FM6BANK, 1) == 0xFE000u &&
                     st_sector(OBJ_SETTINGS, 1) + 4096 == 0xFE000u);
    /* the autosave (1.2, project.c): its own pair 0xE5000 / 0xE6000, above the OTA staging, apart from every other
     * object; a save there touches no other object */
    {
        uint32_t o, c, ok = OBJ_AUTOSAVE == OBJ_UPFM6 + 1 && st_sector(OBJ_AUTOSAVE, 0) == 0xE5000u &&
                               st_sector(OBJ_AUTOSAVE, 1) == 0xE6000u;
        static uint8_t snap[0x100000];
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u && o != OBJ_AUTOSAVE && st_live(o); c++)
                ok &= st_sector(o, c) + (st_big(o) ? 8192u : 4096u) <= 0xE5000u || st_sector(o, c) >= 0xE7000u;
        memcpy(snap, nor, sizeof nor);
        ok &= st_save(OBJ_AUTOSAVE, a, sizeof a) == 0 && st_load(OBJ_AUTOSAVE, got, sizeof got) == (int)sizeof a &&
              !memcmp(got, a, sizeof a) && !memcmp(snap, nor, 0xE5000u) && !memcmp(snap + 0xE7000u, nor + 0xE7000u, 0x19000u);
        bad += check("the autosave in its own sectors 0xE5000 / 0xE6000, nothing else touched", ok);
    }
    {
        static uint8_t bank[3472], back[3728], tab[3728];
        uint32_t i;
        memset(nor, 0xFF, sizeof nor);
        for (i = 0; i < sizeof bank; i++) bank[i] = (uint8_t)(i * 7u);
        for (i = 0; i < sizeof tab; i++) tab[i] = (uint8_t)(i * 3u);
        st_save(OBJ_FM6BANK, bank, sizeof bank);         /* A (seq 1) */
        bank[0] ^= 1;
        st_save(OBJ_FM6BANK, bank, sizeof bank);         /* B (seq 2): the newest */
        bad += check("a bank copy does not read as the patches object (its type)", st_load(OBJ_UPFM6, back, sizeof back) < 0);
        bad += check("st_save_to: the first patches write into A, the bank's older copy; the bank's B still loads",
                     st_save_to(OBJ_UPFM6, tab, sizeof tab, 0) == 0 &&
                     st_load(OBJ_UPFM6, back, sizeof back) == (int)sizeof tab && !memcmp(back, tab, sizeof tab) &&
                     st_load(OBJ_FM6BANK, back, sizeof back) == (int)sizeof bank && !memcmp(back, bank, sizeof bank));
        tab[0] ^= 1;
        bad += check("the next save goes to B (over the bank), A/B as any object",
                     st_save(OBJ_UPFM6, tab, sizeof tab) == 0 && st_load(OBJ_FM6BANK, back, sizeof back) < 0 &&
                     st_load(OBJ_UPFM6, back, sizeof back) == (int)sizeof tab && back[0] == tab[0]);
        {
            int erased = 1;
            for (i = 256u + sizeof tab; i < 4096u; i++) erased &= nor[0x9F000u + i] == 0xFFu && nor[0xFE000u + i] == 0xFFu;
            bad += check("FM6 patches: the sector tails stay erased", erased);
        }
    }
    {
        uint8_t copies[2 * ST_SECTOR];
        uint32_t off = st_sector(OBJ_PROJECT0, 0), cut;
        int ok = 1;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PROJECT0, a, sizeof a);
        st_save(OBJ_PROJECT0, b, sizeof b);
        memcpy(copies, nor + off, sizeof copies);
        for (cut = 0; cut < sizeof a + sizeof(st_hdr_t); cut++) {
            memcpy(nor + off, copies, sizeof copies);
            fail_bytes = (int)cut;
            ok &= st_save(OBJ_PROJECT0, a, sizeof a) != 0;
            fail_bytes = -1;
            ok &= st_load(OBJ_PROJECT0, got, sizeof got) == (int)sizeof b && !memcmp(got, b, sizeof b);
        }
        bad += check("every payload/header byte cut keeps old data", ok);
        erase_error = 1;
        ok = st_save(OBJ_PROJECT0, a, sizeof a) != 0;
        erase_error = 0;
        bad += check("erase failure keeps old data", ok && st_load(OBJ_PROJECT0, got, sizeof got) == (int)sizeof b &&
                                                      !memcmp(got, b, sizeof b));
        write_protected = 1;
        ok = st_save(OBJ_PROJECT0, a, sizeof a) != 0;
        write_protected = 0;
        bad += check("write protection is a save error", ok && st_load(OBJ_PROJECT0, got, sizeof got) == (int)sizeof b &&
                                                            !memcmp(got, b, sizeof b));
    }
    {
        st_hdr_t h;
        uint32_t copy;
        for (copy = 0; copy < 2u; copy++) {
            uint32_t off = st_sector(OBJ_PROJECT0, copy);
            memcpy(&h, nor + off, sizeof h);
            h.seq = 0xFFFFFFFEu + copy;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + off, &h, sizeof h);
        }
        bad += check("save sequence wraps", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
        n = st_load(OBJ_PROJECT0, got, sizeof got);
        bad += check("wrapped sequence loads new data", n == (int)sizeof a && !memcmp(got, a, sizeof a));
        bad += check("save after sequence wrap", st_save(OBJ_PROJECT0, b, sizeof b) == 0 &&
                                                st_load(OBJ_PROJECT0, got, sizeof got) == (int)sizeof b &&
                                                !memcmp(got, b, sizeof b));
    }
    {
        uint32_t calls = io_calls;
        bad += check("invalid object load does no flash access", st_load(OBJ_COUNT, got, sizeof got) < 0 && io_calls == calls);
        bad += check("invalid object save does no flash access", st_save(OBJ_COUNT, a, sizeof a) < 0 && io_calls == calls);
        bad += check("wrapped object rejected", st_save(0xFFFFFFFFu, a, sizeof a) < 0 && io_calls == calls);
        memset(got, 'X', sizeof got);
        bad += check("small destination rejects whole object", st_load(OBJ_PROJECT0, got, sizeof got - 1u) < 0 && got[0] == 'X');
        bad += check("oversized save rejected", st_save(OBJ_PROJECT0, a, ST_PAYLOAD_MAX + 1u) < 0);
    }
    {   /* 5-LAB 1.0.1, FUN11: an object of two sectors a copy (the FUN10 record then the fifth track's block) */
        static uint8_t pa[3840], pb[1280], qa[3840], qb[1280], ra[3840], rb[1280], blob[5120];
        uint32_t i, o, c, cut, ok_map = 1;
        int ok = 1;
        for (i = 0; i < sizeof pa; i++) pa[i] = (uint8_t)(i * 5u + 1u);
        for (i = 0; i < sizeof pb; i++) pb[i] = (uint8_t)(i * 11u + 3u);
        for (i = 0; i < sizeof qa; i++) qa[i] = (uint8_t)(i * 7u + 2u);
        for (i = 0; i < sizeof qb; i++) qb[i] = (uint8_t)(i * 13u + 5u);
        memset(nor, 0xFF, sizeof nor);
        bad += check("FUN11: the map is 5 objects x A/B x 2 sectors in 0xC8000..0xDBFFF, no other object there",
                     OBJ_PROJ11_0 == OBJ_RETIRED5_0 + 5 && OBJ_AUTOSAVE11 == OBJ_PROJ11_0 + 4 && OBJ_COUNT == OBJ_AUTOSAVE11 + 1 &&
                     st_sector(OBJ_PROJ11_0, 0) == 0xC8000u && st_sector(OBJ_PROJ11_0, 1) == 0xCA000u &&
                     st_sector(OBJ_AUTOSAVE11, 1) + 8192u == 0xDC000u && OBJ_FM6VB >= OBJ_COUNT);
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++) {
                uint32_t lo = st_sector(o, c), hi = lo + (st_big(o) ? 8192u : 4096u), o2, c2;
                if (!st_live(o)) continue;
                for (o2 = o + 1u; o2 < OBJ_COUNT; o2++)
                    for (c2 = 0; c2 < 2u; c2++) {
                        uint32_t lo2 = st_sector(o2, c2), hi2 = lo2 + (st_big(o2) ? 8192u : 4096u);
                        if (st_live(o2) && lo < hi2 && lo2 < hi && !((o == OBJ_UPFM6 || o == OBJ_FM6BANK) && (o2 == OBJ_UPFM6 || o2 == OBJ_FM6BANK)))
                            ok_map = 0;
                    }
            }
        bad += check("FUN11: no two live objects share a sector (the FM6 patches / retired bank pair aside)", ok_map);
        bad += check("FUN11: a retired 1.0.0 number is no object (no flash access)",
                     !st_live(OBJ_RETIRED5_0) && !st_live(OBJ_RETIRED5_0 + 4) && st_load(OBJ_RETIRED5_0 + 1, got, sizeof got) < 0 &&
                     st_save(OBJ_RETIRED5_0 + 2, a, sizeof a) < 0);
        bad += check("FUN11: nothing yet -> -1 (the caller reads the older FUN10 object)",
                     st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == -1);
        bad += check("FUN11: save, load both parts", st_save2(OBJ_PROJ11_0 + 1, pa, sizeof pa, pb, sizeof pb) == 0 &&
                     st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == 5120 && !memcmp(ra, pa, sizeof pa) &&
                     !memcmp(rb, pb, sizeof pb));
        {   /* where the bytes are: the record in sector 0 (payload from 256), the payload goes on at the start of sector 1 */
            const uint8_t *s0 = nor + st_sector(OBJ_PROJ11_0 + 1, 0);
            memcpy(blob, pa, sizeof pa); memcpy(blob + sizeof pa, pb, sizeof pb);
            bad += check("FUN11: first copy = header + 3840 bytes in sector 0, the last 1280 at the start of sector 1",
                         !memcmp(s0 + ST_PAYLOAD_OFF, blob, ST_PAYLOAD_MAX) && !memcmp(s0 + ST_SECTOR, blob + ST_PAYLOAD_MAX, 5120u - ST_PAYLOAD_MAX) &&
                         s0[ST_SECTOR + 5120u - ST_PAYLOAD_MAX] == 0xFFu);
        }
        bad += check("FUN11: the second save goes to the other copy, the first stays", st_save2(OBJ_PROJ11_0 + 1, qa, sizeof qa, qb, sizeof qb) == 0 &&
                     st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == 5120 && !memcmp(ra, qa, sizeof qa) && !memcmp(rb, qb, sizeof qb) &&
                     !st_head(OBJ_PROJ11_0 + 1, 0, &(st_hdr_t){0}) && !st_head(OBJ_PROJ11_0 + 1, 1, &(st_hdr_t){0}));
        {   /* power lost at every byte of the third save: the previous record (qa, qb) always loads whole */
            uint8_t save[4u * 4096u];
            uint32_t off = st_sector(OBJ_PROJ11_0 + 1, 0);
            memcpy(save, nor + off, sizeof save);
            for (cut = 0; cut < 5120u + sizeof(st_hdr_t); cut++) {
                memcpy(nor + off, save, sizeof save);
                fail_bytes = (int)cut;
                ok &= st_save2(OBJ_PROJ11_0 + 1, pa, sizeof pa, pb, sizeof pb) != 0;
                fail_bytes = -1;
                memset(ra, 0, sizeof ra); memset(rb, 0, sizeof rb);
                ok &= st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == 5120 && !memcmp(ra, qa, sizeof qa) && !memcmp(rb, qb, sizeof qb);
            }
            memcpy(nor + off, save, sizeof save);
            bad += check("FUN11: power lost at any byte of a save (5152 cuts): the previous project and block, whole", ok);
            ok = 1;
            for (cut = 0; cut < 3u; cut++) {   /* an erase / program that fails outright, in either sector */
                fail_after = (int)cut * 8;
                ok &= st_save2(OBJ_PROJ11_0 + 1, pa, sizeof pa, pb, sizeof pb) != 0;
                fail_after = -1;
                ok &= st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == 5120 && !memcmp(ra, qa, sizeof qa) && !memcmp(rb, qb, sizeof qb);
                memcpy(nor + off, save, sizeof save);
            }
            erase_error = 1;
            ok &= st_save2(OBJ_PROJ11_0 + 1, pa, sizeof pa, pb, sizeof pb) != 0;
            erase_error = 0;
            ok &= st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb) == 5120 && !memcmp(ra, qa, sizeof qa);
            bad += check("FUN11: a failed program / erase keeps the previous record", ok);
        }
        {   /* only the second sector of the newest copy rots: that copy is refused whole, the older one loads */
            uint32_t cur = (uint32_t)st_current(OBJ_PROJ11_0 + 1, &(st_hdr_t){0});
            nor[st_sector(OBJ_PROJ11_0 + 1, cur) + ST_SECTOR + 100u] ^= 0x04u;
            memset(ra, 0, sizeof ra); memset(rb, 0, sizeof rb);
            ok = st_load2(OBJ_PROJ11_0 + 1, ra, sizeof ra, rb, sizeof rb);
            bad += check("FUN11: a flipped bit in the second sector refuses that copy (CRC over both), the other copy loads",
                         ok == 5120 && !memcmp(ra, pa, sizeof pa) && !memcmp(rb, pb, sizeof pb));
        }
        {   /* a record of 1.0.0's retired type where a FUN11 copy's header would be is nobody's */
            st_hdr_t h;
            uint32_t at = st_sector(OBJ_PROJ11_0 + 2, 0);
            memset(&h, 0, sizeof h);
            h.magic = ST_MAGIC; h.type = OBJ_RETIRED5_0 + 2u; h.slot = 0; h.seq = 7; h.len = 1280; h.crc = st_crc32(pb, 1280);
            h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu; h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + at, &h, sizeof h); memcpy(nor + at + ST_PAYLOAD_OFF, pb, 1280);
            bad += check("FUN11: a stale 1.0.0 fifth-track record in the new sectors is not an object", st_load2(OBJ_PROJ11_0 + 2, ra, sizeof ra, rb, sizeof rb) == -1);
        }
        bad += check("FUN11: an empty record loads as 0 (not -1: the older FUN10 object is then ignored)",
                     st_save(OBJ_PROJ11_0 + 3, ra, 0) == 0 && st_load2(OBJ_PROJ11_0 + 3, ra, sizeof ra, rb, sizeof rb) == 0);
        bad += check("FUN11: a record of another length is -2 and leaves the destination alone",
                     st_save(OBJ_PROJ11_0, a, sizeof a) == 0 && (memset(rb, 0x5A, sizeof rb), st_load2(OBJ_PROJ11_0, ra, sizeof ra, rb, sizeof rb)) == -2 && rb[0] == 0x5A);
        bad += check("FUN11: up to 7936 bytes fit, one more is refused; a one-sector object still stops at 3840",
                     st_save(OBJ_AUTOSAVE11, blob, 0) == 0 && ST_BIG_MAX == 7936u && st_save(OBJ_AUTOSAVE11, nor, ST_BIG_MAX) == 0 &&
                     st_save(OBJ_AUTOSAVE11, nor, ST_BIG_MAX + 1u) < 0 && st_save(OBJ_PROJECT0, nor, ST_PAYLOAD_MAX + 1u) < 0);
        {   /* the largest record: the second sector full to its last byte */
            static uint8_t big[ST_BIG_MAX], back[ST_BIG_MAX];
            for (i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 31u + 7u);
            ok = st_save(OBJ_AUTOSAVE11, big, sizeof big) == 0 && st_load(OBJ_AUTOSAVE11, back, sizeof back) == (int)sizeof big && !memcmp(big, back, sizeof big);
            bad += check("FUN11: a record of the whole 7936 bytes round trips", ok);
        }
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
