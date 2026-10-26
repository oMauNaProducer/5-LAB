/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)
#define ST_BIG_MAX (ST_PAYLOAD_MAX + ST_SECTOR)        /* an object of two sectors a copy: the payload goes on in the second, from its start
                                                        * (read and written in 256-byte chunks: st_buf stays one sector's) */

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000.., FL_AUTO 0xE5000..0xE6FFF, FL_VBANK 0xE7000): settings 0xFC000, projects
 * 0x97000..0x9EFFF, user sample slots 0xA0000..0xC7FFF (eng_sample.c: two since 5-LAB; the third's flash, 0xC8000..0xD1FFF,
 * holds the fifth tracks' half of the four projects and of the autosave: OBJ_PROJ5_0 .. OBJ_AUTOSAVE5), user preset banks 0xDC000..0xDFFFF (upreset.c),
 * the user presets' FM6 patches (up_fm6.c, since 1.0.3): copy A 0x9F000, copy B 0xFE000; the autosave (project.c,
 * 1.2): A 0xE5000, B 0xE6000, above the OTA staging (0xE0000..0xE4FFF), in the margin the stock firmware's own update
 * stages into (only ever while the stock firmware runs: a later Felucca finds no valid copy and starts as without one).
 * Those two sectors held the FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, retired): both objects use the same pair,
 * told apart by the commit record's type. The bank is only read, once, to move its patches into the user presets
 * (up_fm6.c upf_boot); the first write of the new object goes to the sector that does not hold the bank's newest
 * copy (st_save_to), so a power cut never loses both.
 * The FM6 voice bank (1.4.1, fm6_vbank.c): 0xE7000, the sector above the autosave, with a commit record of the same
 * kind (type OBJ_FM6VB, not an st_load object); a new bank is first staged in the autosave's older copy (fm6_vbank.c). */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_UPFM6,
       OBJ_AUTOSAVE,
       OBJ_RETIRED5_0,                               /* 5-LAB 1.0.0's fifth-track objects (10..14): never written again, and
                                                      * their numbers never reused (a stale record there is no object's) */
       OBJ_PROJ11_0 = OBJ_RETIRED5_0 + 5, OBJ_AUTOSAVE11 = OBJ_PROJ11_0 + 4, OBJ_COUNT };
/* 5-LAB 1.0.1, "FUN11": a project is ONE object of two sectors a copy (st_big): the FUN10 record (tracks 1..4, the format
 * every Felucca reads) then the fifth track's block (project.c PROJ5), committed together. The four slots and the autosave
 * are OBJ_PROJ11_0.. at 0xC8000..0xDBFFF (the old third user sample slot); OBJ_PROJECT0.. and OBJ_AUTOSAVE (FUN10 alone,
 * Felucca's and 5-LAB 1.0.0's) are only read, when a slot has no FUN11 object yet. */
#define OBJ_FM6VB 32u                          /* the voice bank's commit records (fm6_vbank.c; >= OBJ_COUNT: st_load
                                                * never takes one, no object here takes its sectors) */

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step; running:
                                                                         * from 0xFFFFFFFF, the CRC is ~c */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc32_add(0xFFFFFFFFu, p, n); }

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK || obj == OBJ_UPFM6)          /* (the same pair: see the flash map) */
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_AUTOSAVE)
        return 0xE5000u + copy * ST_SECTOR;
    if (obj >= OBJ_PROJ11_0)                             /* (5-LAB: USR3's flash, 0xC8000..0xDBFFF: 5 objects, A/B, two sectors a copy) */
        return 0xC8000u + (obj - OBJ_PROJ11_0) * 4u * ST_SECTOR + copy * 2u * ST_SECTOR;
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4), section(".pool")));   /* (main loop only) */

static int st_big(uint32_t obj) { return obj >= OBJ_PROJ11_0 && obj < OBJ_COUNT; }   /* two sectors a copy */
static int st_live(uint32_t obj) { return obj < OBJ_COUNT && (obj < OBJ_RETIRED5_0 || obj >= OBJ_PROJ11_0); }

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (!st_live(obj) || copy > 1u)
        return -1;
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->slot != copy || h->len > (st_big(obj) ? ST_BIG_MAX : ST_PAYLOAD_MAX) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

/* flash address of payload byte p of a copy: a big object's goes on at the start of its second sector */
static uint32_t st_pay_at(uint32_t obj, uint32_t copy, uint32_t p)
{
    return p < ST_PAYLOAD_MAX ? st_sector(obj, copy) + ST_PAYLOAD_OFF + p : st_sector(obj, copy) + ST_SECTOR + (p - ST_PAYLOAD_MAX);
}
/* a big object's payload bytes [from, from + n) -> dst (from and n in 256s except the last: a chunk never straddles the sectors) */
static int st_pay_read(uint32_t obj, uint32_t copy, uint32_t from, void *dst, uint32_t n)
{
    uint32_t done = 0;
    while (done < n) {
        uint32_t p = from + done, room = p < ST_PAYLOAD_MAX ? ST_PAYLOAD_MAX - p : 0x7FFFFFFFu, c = n - done < room ? n - done : room;
        if (st_read(st_pay_at(obj, copy, p), (uint8_t *)dst + done, c))
            return -1;
        done += c;
    }
    return 0;
}

/* a one-sector object: payload -> st_buf, CRC ok: 0. A big object (5 KB of a project would not fit st_buf, and the pool has no
 * room for a second one): the CRC is checked in 256-byte chunks and the payload stays in flash (st_load / st_load2 read it
 * from there) */
static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)
{
    if (st_big(obj)) {
        uint8_t tmp[256];
        uint32_t c = 0xFFFFFFFFu, p;
        for (p = 0; p < h->len; p += 256u) {
            uint32_t n = h->len - p > 256u ? 256u : h->len - p;
            if (st_pay_read(obj, copy, p, tmp, n))
                return -1;
            c = st_crc32_add(c, tmp, n);
        }
        return ~c == h->crc ? 0 : -1;
    }
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the newest valid sequence (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (a
 * one-sector object's is left in st_buf); *h gets its header. */
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    int cur;
    if (!st_live(obj) || (cur = st_current(obj, &h)) < 0 || h.len > max)
        return -1;
    if (st_big(obj))
        return st_pay_read(obj, (uint32_t)cur, 0, dst, h.len) ? -1 : (int)h.len;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* a two-part object (5-LAB FUN11: the FUN10 record, then the fifth track's block) into a (alen bytes) and b (blen): the
 * object's length (alen + blen); 0 an empty record; -1 no valid copy at all (the caller may look at an older object);
 * -2 a record of another length (b is not touched then; 1.1.4: b may hold less than blen, the rest of it zero) */
static int st_load2(uint32_t obj, void *a, uint32_t alen, void *b, uint32_t blen)
{
    st_hdr_t h;
    int cur;
    if (!st_live(obj) || !st_big(obj) || (cur = st_current(obj, &h)) < 0)
        return -1;
    if (h.len == 0u)
        return 0;
    if (blen ? (h.len <= alen || h.len > alen + blen) : h.len != alen)
        return -2;
    if (st_pay_read(obj, (uint32_t)cur, 0, a, alen))
        return -1;
    if (blen) {                                       /* 1.1.4: b may be a shorter one (an older block): the rest of it is zero */
        uint32_t got = h.len - alen, z;
        for (z = got; z < blen; z++)
            ((uint8_t *)b)[z] = 0u;
        if (got && st_pay_read(obj, (uint32_t)cur, alen, b, got))
            return -1;
    }
    return (int)h.len;
}

/* save a (alen bytes) then b (blen; b may be 0) as one record into copy `to` (0 A, 1 B; -1: the one that is not the
 * current copy, as st_save). A big object (two sectors a copy) erases the sectors it needs, programs the payload in
 * 256-byte pages straight from a and b (the second sector from its start; no copy of it in st_buf), and commits with the
 * record in the FIRST sector last: a cut anywhere before it leaves no valid header there, so the copy is ignored whole and
 * the other (the previous record) stays in charge */
static int st_save_parts(uint32_t obj, const void *a, uint32_t alen, const void *b, uint32_t blen, int to)
{
    uint32_t seq, base, off, len = alen + blen, crc = 0xFFFFFFFFu;
    int cur, rc;
    st_hdr_t h;
    if (!st_live(obj) || len > (st_big(obj) ? ST_BIG_MAX : ST_PAYLOAD_MAX) || to > 1 || (blen && !st_big(obj)))
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    if (to < 0)
        to = cur == 0 ? 1 : 0;                        /* write the other copy */
    base = st_sector(obj, (uint32_t)to);
    if (!st_big(obj)) {
        for (off = 0; off < len; off++)
            st_buf[off] = ((const uint8_t *)a)[off];  /* the driver wants RAM sources */
        if ((rc = st_erase(base)) != 0)
            return rc;
        for (off = 0; off < len; off += 256u) {
            uint32_t n = len - off > 256u ? 256u : len - off;
            if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
                return rc;
        }
        crc = st_crc32_add(crc, st_buf, len);
    } else {
        uint8_t tmp[256];
        if ((rc = st_erase(base)) != 0)
            return rc;
        if (len > ST_PAYLOAD_MAX && (rc = st_erase(base + ST_SECTOR)) != 0)   /* (the second sector only when the record goes into it:
                                                                              * an empty record is one erase) */
            return rc;
        for (off = 0; off < len; off += 256u) {
            uint32_t n = len - off > 256u ? 256u : len - off, i;
            for (i = 0; i < n; i++)
                tmp[i] = off + i < alen ? ((const uint8_t *)a)[off + i] : ((const uint8_t *)b)[off + i - alen];
            crc = st_crc32_add(crc, tmp, n);
            if ((rc = st_prog(st_pay_at(obj, (uint32_t)to, off), tmp, n)) != 0)
                return rc;
        }
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)to;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = ~crc;
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = (uint32_t)to;
        if (st_head(obj, c, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}
static int st_save_to(uint32_t obj, const void *src, uint32_t len, int to) { return st_save_parts(obj, src, len, 0, 0, to); }
static int st_save2(uint32_t obj, const void *a, uint32_t alen, const void *b, uint32_t blen)
{
    return st_save_parts(obj, a, alen, b, blen, -1);
}

static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save_to(obj, src, len, -1); }
