// Copyright (c) 2014-2015 The ShadowCoin developers
// Copyright (c) 2017-2020 The Particl Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FALCON_KEY_MNEMONIC_H
#define FALCON_KEY_MNEMONIC_H

#include <string>
#include <vector>

namespace mnemonic {

enum WordListLanguages
{
    WLL_ENGLISH         = 1,
    WLL_FRENCH          = 2,
    WLL_JAPANESE        = 3,
    WLL_SPANISH         = 4,
    WLL_CHINESE_S       = 5,
    WLL_CHINESE_T       = 6,
    WLL_ITALIAN         = 7,
    WLL_KOREAN          = 8,
    WLL_CZECH           = 9,

    WLL_MAX
};

extern const char *mnLanguagesDesc[WLL_MAX];
extern const char *mnLanguagesTag[WLL_MAX];

int GetWord(int o, const char *pwl, int max, std::string &sWord);
int GetWordOffset(const char *p, const char *pwl, int max, int &o);

int GetLanguageOffset(std::string sIn);
int DetectLanguage(const std::string &sWordList);
int Encode(int nLanguage, const std::vector<uint8_t> &vEntropy, std::string &sWordList, std::string &sError);
int Decode(int &nLanguage, const std::string &sWordListIn, std::vector<uint8_t> &vEntropy, std::string &sError, bool fIgnoreChecksum=false);
int ToSeed(const std::string &sMnemonic, const std::string &sPasswordIn, std::vector<uint8_t> &vSeed);
int AddChecksum(int nLanguageIn, const std::string &sWordListIn, std::string &sWordListOut, std::string &sError);
int GetWord(int nLanguage, int nWord, std::string &sWord, std::string &sError);
std::string GetLanguage(int nLanguage);
std::string ListEnabledLanguages(std::string separator);
bool HaveLanguage(int nLanguage);

}

#endif // FALCON_KEY_MNEMONIC_H

