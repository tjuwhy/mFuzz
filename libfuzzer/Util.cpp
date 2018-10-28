#include "Util.h"

namespace fuzzer {
  u32 UR(u32 limit) {
    return random() % limit;
  }
  
  int effAPos(int p) {
    return p >> EFF_MAP_SCALE2;
  }
  
  int effRem(int x) {
    return (x) & ((1 << EFF_MAP_SCALE2) - 1);
  }
  
  int effALen(int l) {
    return effAPos(l) + !!effRem(l);
  }
  /* Helper function to see if a particular change (xor_val = old ^ new) could
   be a product of deterministic bit flips with the lengths and stepovers
   attempted by afl-fuzz. This is used to avoid dupes in some of the
   deterministic fuzzing operations that follow bit flips. We also
   return 1 if xor_val is zero, which implies that the old and attempted new
   values are identical and the exec would be a waste of time. */
  bool couldBeBitflip(u32 xorValue) {
    u32 sh = 0;
    if (!xorValue) return true;
    /* Shift left until first bit set. */
    while (!(xorValue & 1)) { sh++ ; xorValue >>= 1; }
    /* 1-, 2-, and 4-bit patterns are OK anywhere. */
    if (xorValue == 1 || xorValue == 3 || xorValue == 15) return 1;
    /* 8-, 16-, and 32-bit patterns are OK only if shift factor is
     divisible by 8, since that's the stepover for these ops. */
    if (sh & 7) return false;
    if (xorValue == 0xff || xorValue == 0xffff || xorValue == 0xffffffff)
      return true;
    return false;
  }
  /* Helper function to see if a particular value is reachable through
   arithmetic operations. Used for similar purposes. */
  bool couldBeArith(u32 old_val, u32 new_val, u8 blen) {
    u32 i, ov = 0, nv = 0, diffs = 0;
    if (old_val == new_val) return true;
    /* See if one-byte adjustments to any byte could produce this result. */
    for (i = 0; i < blen; i++) {
      u8 a = old_val >> (8 * i),
      b = new_val >> (8 * i);
      if (a != b) { diffs++; ov = a; nv = b; }
    }
    /* If only one byte differs and the values are within range, return 1. */
    if (diffs == 1) {
      if ((u8)(ov - nv) <= ARITH_MAX ||
          (u8)(nv - ov) <= ARITH_MAX) return true;
    }
    if (blen == 1) return false;
    /* See if two-byte adjustments to any byte would produce this result. */
    diffs = 0;
    for (i = 0; i < blen / 2; i++) {
      u16 a = old_val >> (16 * i),
      b = new_val >> (16 * i);
      if (a != b) { diffs++; ov = a; nv = b; }
    }
    /* If only one word differs and the values are within range, return 1. */
    if (diffs == 1) {
      if ((u16)(ov - nv) <= ARITH_MAX || (u16)(nv - ov) <= ARITH_MAX)
        return  true;
      ov = swap16(ov); nv = swap16(nv);
      if ((u16)(ov - nv) <= ARITH_MAX || (u16)(nv - ov) <= ARITH_MAX)
        return true;
    }
    /* Finally, let's do the same thing for dwords. */
    if (blen == 4) {
      if ((u32)(old_val - new_val) <= (u32) ARITH_MAX || (u32)(new_val - old_val) <= (u32) ARITH_MAX)
        return true;
      new_val = swap32(new_val);
      old_val = swap32(old_val);
      if ((u32)(old_val - new_val) <= (u32) ARITH_MAX || (u32)(new_val - old_val) <= (u32) ARITH_MAX)
        return true;
    }
    return false;
  }
  /* Last but not least, a similar helper to see if insertion of an
   interesting integer is redundant given the insertions done for
   shorter blen. The last param (check_le) is set if the caller
   already executed LE insertion for current blen and wants to see
   if BE variant passed in new_val is unique. */
  bool couldBeInterest(u32 old_val, u32 new_val, u8 blen, u8 check_le) {
    u32 i, j;
    if (old_val == new_val) return true;
    /* See if one-byte insertions from interesting_8 over old_val could
     produce new_val. */
    for (i = 0; i < blen; i++) {
      for (j = 0; j < sizeof(INTERESTING_8); j++) {
        u32 tval = (old_val & ~(0xff << (i * 8))) |
        (((u8)INTERESTING_8[j]) << (i * 8));
        if (new_val == tval) return true;
      }
    }
    /* Bail out unless we're also asked to examine two-byte LE insertions
     as a preparation for BE attempts. */
    if (blen == 2 && !check_le) return false;
    /* See if two-byte insertions over old_val could give us new_val. */
    for (i = 0; i < blen - 1; i++) {
      for (j = 0; j < sizeof(INTERESTING_16) / 2; j++) {
        u32 tval = (old_val & ~(0xffff << (i * 8))) |
        (((u16)INTERESTING_16[j]) << (i * 8));
        if (new_val == tval) return true;
        /* Continue here only if blen > 2. */
        if (blen > 2) {
          tval = (old_val & ~(0xffff << (i * 8))) |
          (swap16(INTERESTING_16[j]) << (i * 8));
          if (new_val == tval) return true;
        }
      }
    }
    if (blen == 4 && check_le) {
      /* See if four-byte insertions could produce the same result
       (LE only). */
      for (j = 0; j < sizeof(INTERESTING_32) / 4; j++)
        if (new_val == (u32)INTERESTING_32[j]) return true;
    }
    return false;
  }
  
  u16 swap16(u16 x) {
    return x << 8 | x >> 8;
  }
  
  u32 swap32(u32 x) {
    return x << 24 | x >> 24 | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00);
  }
  
  u32 chooseBlockLen(u32 limit) {
    u32 minValue, maxValue;
    auto MIN = [](u32 a, u32 b) {
      if (a > b) return b;
      return a;
    };
    switch (UR(1)) {
      case 0: {
        minValue = 1;
        maxValue = HAVOC_BLK_SMALL;
        break;
      }
      case 1: {
        minValue = HAVOC_BLK_SMALL;
        maxValue = HAVOC_BLK_MEDIUM;
        break;
      }
    }
    if (minValue >= limit) minValue = 1;
    return minValue + UR(MIN(maxValue, limit) - minValue + 1);
  }
}
