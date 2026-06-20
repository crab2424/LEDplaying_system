#include "lyrics.h"
#include "katakana.h"

// ============================================================
// カタカナ 8x8 フレーム定義（48文字）
//   1文字 = 32bit×3 = 96bit (8x8 マトリクスを 3要素で表現)
// ============================================================

const uint32_t a[3] = {
  0x3f800,
  0x80480500,
  0x40040080,
};
const uint32_t i[3] = {
  0x1806,
  0xc03400,
  0x40040040,
};
const uint32_t u[3] = {
  0x403f,
  0x82082080,
  0x8010060,
};
const uint32_t e[3] = {
  0x1f,
  0x400400,
  0x400403f8,
};
const uint32_t o[3] = {
  0x203f,
  0x80600a00,
  0xa0120260,
};
const uint32_t ka[3] = {
  0x403f,
  0x80480480,
  0x88088138,
};
const uint32_t ki[3] = {
  0x403f,
  0x80400403,
  0xf8040040,
};
const uint32_t ku[3] = {
  0x1f810,
  0x82082080,
  0x10010060,
};
const uint32_t ke[3] = {
  0x1001f,
  0x81102100,
  0x200200c0,
};
const uint32_t ko[3] = {
  0x3f800,
  0x80080080,
  0x80083f8,
};
const uint32_t sa[3] = {
  0x1103f,
  0x81100100,
  0x100200c0,
};
const uint32_t si[3] = {
  0x8,
  0x82481080,
  0x100303c0,
};
const uint32_t su[3] = {
  0x3f800,
  0x80100200,
  0x60090108,
};
const uint32_t se[3] = {
  0x1003f,
  0x81081101,
  0x1001f8,
};
const uint32_t so[3] = {
  0x20820,
  0x81080080,
  0x100201c0,
};
const uint32_t ta[3] = {
  0x1f810,
  0x81882700,
  0x100601c0,
};
const uint32_t ti[3] = {
  0x300e,
  0x203f80,
  0x20040080,
};
const uint32_t tu[3] = {
  0x814,
  0x81480080,
  0x100100e0,
};
const uint32_t te[3] = {
  0x1f000,
  0x3f80200,
  0x200200c0,
};
const uint32_t to[3] = {
  0x8008,
  0xe00900,
  0x80080080,
};
const uint32_t na[3] = {
  0x4004,
  0x3f80400,
  0x40080100,
};
const uint32_t ni[3] = {
  0x1f,
  0x0,
  0x3f8000,
};
const uint32_t nu[3] = {
  0x3f800,
  0x81100a00,
  0x400a0310,
};
const uint32_t ne[3] = {
  0x403f,
  0x80100200,
  0xc0150248,
};
const uint32_t no[3] = {
  0x800,
  0x80080100,
  0x10060380,
};
const uint32_t ha[3] = {
  0xa00a,
  0xa00a01,
  0x10110208,
};
const uint32_t hi[3] = {
  0x20020,
  0x2783802,
  0x2001f8,
};
const uint32_t hu[3] = {
  0x3f,
  0x80080080,
  0x100101e0,
};
const uint32_t he[3] = {
  0x8014,
  0x2202100,
  0x8004000,
};
const uint32_t ho[3] = {
  0x403f,
  0x80401501,
  0x48248040,
};
const uint32_t ma[3] = {
  0x3f800,
  0x81080900,
  0x60020010,
};
const uint32_t mi[3] = {
  0x38007,
  0x3000c00,
  0x30380070,
};
const uint32_t mu[3] = {
  0x8008,
  0x1001202,
  0x103f0008,
};
const uint32_t me[3] = {
  0x800,
  0x80900500,
  0x200d0308,
};
const uint32_t mo[3] = {
  0x1f008,
  0x803f80,
  0x80080070,
};
const uint32_t ya[3] = {
  0x803f,
  0x80880880,
  0x90080080,
};
const uint32_t yu[3] = {
  0x3e,
  0x200200,
  0x200203f8,
};
const uint32_t yo[3] = {
  0x3f800,
  0x80083f80,
  0x80083f8,
};
const uint32_t ra[3] = {
  0x1f000,
  0x3f80080,
  0x80100e0,
};
const uint32_t ri[3] = {
  0x11011,
  0x1101100,
  0x300200c0,
};
const uint32_t ru[3] = {
  0xa00a,
  0xa01201,
  0x28228230,
};
const uint32_t re[3] = {
  0x10010,
  0x1001001,
  0x81301c0,
};
const uint32_t ro[3] = {
  0x3f,
  0x82082082,
  0x82083f8,
};
const uint32_t wa[3] = {
  0x3f820,
  0x82080080,
  0x100200c0,
};
const uint32_t wo[3] = {
  0x3f800,
  0x80081f80,
  0x100200c0,
};
const uint32_t nn[3] = {
  0x820,
  0x81080100,
  0x10060380,
};
const uint32_t ga[3] = { 
  0x809,
  0x41e80a00, 
  0xa0120120 
};


// ============================================================
// カエルの歌 36文字 歌詞配列
//   カエルノウタガ キコエテクルヨ
//   クワクワクワクワ
//   ケロケロケロケロ
//   クワクワクワ
//   表示タイミングは lyrics.h の lyricTiming[] が制御する。
// ============================================================
const uint32_t *kaeruNoUta[LYRICS_LENGTH] = {
  ka, e, ru, no, u, ta, ga, ki, ko, e, te, ku, ru, yo,  // 14
  ku, wa, ku, wa, ku, wa, ku, wa,                       // 8
  ke, ro, ke, ro, ke, ro, ke, ro,                       // 8
  ku, wa, ku, wa, ku, wa                                // 6
};
