// Copyright (c) 2017-2020 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef FALCON_KEY_KEYUTIL_H
#define FALCON_KEY_KEYUTIL_H

#include <vector>
#include <stdint.h>

inline bool IsHardened(uint32_t n)              { return (n & ((uint32_t)1 << 31)); };
inline uint32_t &SetHardenedBit(uint32_t &n)    { return (n |= ((uint32_t)1 << 31)); };
inline uint32_t &ClearHardenedBit(uint32_t &n)  { return (n &= ~((uint32_t)1 << 31)); };
inline uint32_t WithHardenedBit(uint32_t n)     { return (n |= ((uint32_t)1 << 31)); };
inline uint32_t WithoutHardenedBit(uint32_t n)  { return (n &= ~((uint32_t)1 << 31)); };

bool VerifyChecksum(const std::vector<uint8_t> &data);


#endif  // FALCON_KEY_KEYUTIL_H
