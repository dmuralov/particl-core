// Copyright (c) 2017-2021 The Particl Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/test/hdwallet_test_fixture.h>
#include <bench/bench.h>
#include <wallet/hdwallet.h>
#include <wallet/coincontrol.h>
#include <interfaces/chain.h>
#include <interfaces/wallet.h>

#include <validation.h>
#include <blind.h>
#include <rpc/rpcutil.h>
#include <rpc/blockchain.h>
#include <timedata.h>
#include <miner.h>
#include <pos/miner.h>
#include <util/string.h>
#include <util/translation.h>

CTransactionRef CreateTxn(CHDWallet *pwallet, CBitcoinAddress &address, CAmount amount, int type_in, int type_out, int nRingSize = 5)
{
    LOCK(pwallet->cs_wallet);

    assert(address.IsValid());

    std::vector<CTempRecipient> vecSend;
    std::string sError;
    CTempRecipient r;
    r.nType = type_out;
    r.SetAmount(amount);
    r.address = address.Get();
    vecSend.push_back(r);

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    CCoinControl coinControl;
    if (type_in == OUTPUT_STANDARD) {
        assert(0 == pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError));
    } else
    if (type_in == OUTPUT_CT) {
        assert(0 == pwallet->AddBlindedInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError));
    } else {
        int nInputsPerSig = 1;
        assert(0 == pwallet->AddAnonInputs(wtx, rtx, vecSend, true, nRingSize, nInputsPerSig, nFee, &coinControl, sError));
    }
    return wtx.tx;
}

static void AddAnonTxn(CHDWallet *pwallet, CBitcoinAddress &address, CAmount amount, OutputTypes output_type)
{
    {
    LOCK(pwallet->cs_wallet);

    assert(address.IsValid());

    std::vector<CTempRecipient> vecSend;
    std::string sError;
    CTempRecipient r;
    r.nType = output_type;
    r.SetAmount(amount);
    r.address = address.Get();
    vecSend.push_back(r);

    CTransactionRef tx_new;
    CWalletTx wtx(pwallet, tx_new);
    CTransactionRecord rtx;
    CAmount nFee;
    CCoinControl coinControl;
    assert(0 == pwallet->AddStandardInputs(wtx, rtx, vecSend, true, nFee, &coinControl, sError));
    assert(wtx.SubmitMemoryPoolAndRelay(sError, true));
    }
    SyncWithValidationInterfaceQueue();
}

void StakeNBlocks(CHDWallet *pwallet, size_t nBlocks)
{
    ChainstateManager *pchainman{nullptr};
    if (pwallet->HaveChain()) {
        pchainman = pwallet->chain().getChainman();
    }
    if (!pchainman) {
        LogPrintf("Error: Chainstate manager not found.\n");
        return;
    }

    int nBestHeight;
    size_t nStaked = 0;
    size_t k, nTries = 10000;
    for (k = 0; k < nTries; ++k) {
        nBestHeight = pwallet->chain().getHeightInt();

        int64_t nSearchTime = GetAdjustedTime() & ~Params().GetStakeTimestampMask(nBestHeight+1);
        if (nSearchTime <= pwallet->nLastCoinStakeSearchTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        std::unique_ptr<CBlockTemplate> pblocktemplate = pwallet->CreateNewBlock();
        assert(pblocktemplate.get());

        if (pwallet->SignBlock(pblocktemplate.get(), nBestHeight+1, nSearchTime)) {
            CBlock *pblock = &pblocktemplate->block;

            if (CheckStake(*pchainman, pblock)) {
                nStaked++;
            }
        }

        if (nStaked >= nBlocks) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    assert(k < nTries);
    SyncWithValidationInterfaceQueue();
};

static std::shared_ptr<CHDWallet> CreateTestWallet(WalletContext& wallet_context, std::string wallet_name)
{
    DatabaseOptions options;
    DatabaseStatus status;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    options.create_flags = WALLET_FLAG_BLANK_WALLET;
    auto database = MakeWalletDatabase(wallet_name, options, status, error);
    auto wallet = CWallet::Create(wallet_context, wallet_name, std::move(database), options.create_flags, error, warnings);

    return std::static_pointer_cast<CHDWallet>(wallet);
}

static void AddTx(benchmark::Bench& bench, const std::string from, const std::string to, const bool owned)
{
    TestingSetup test_setup{CBaseChainParams::REGTEST, {}, true};
    const auto context = util::AnyPtr<NodeContext>(&test_setup.m_node);

    std::unique_ptr<interfaces::Chain> chain = interfaces::MakeChain(test_setup.m_node);
    std::unique_ptr<interfaces::WalletClient> wallet_client = interfaces::MakeWalletClient(*chain, *Assert(test_setup.m_node.args));
    wallet_client->registerRpcs();
    WalletContext& wallet_context = *wallet_client->context();

    ECC_Start_Stealth();
    ECC_Start_Blinding();

    std::unique_ptr<interfaces::Chain> m_chain = interfaces::MakeChain(test_setup.m_node);
    std::unique_ptr<interfaces::ChainClient> m_chain_client = interfaces::MakeWalletClient(*m_chain, *Assert(test_setup.m_node.args));
    m_chain_client->registerRpcs();

    std::shared_ptr<CHDWallet> pwallet_a = CreateTestWallet(wallet_context, "a");
    assert(pwallet_a.get());
    AddWallet(wallet_context, pwallet_a);

    std::shared_ptr<CHDWallet> pwallet_b = CreateTestWallet(wallet_context, "b");
    assert(pwallet_b.get());
    AddWallet(wallet_context, pwallet_b);

    {
        int last_height = context->chainman->ActiveChain().Height();
        uint256 last_hash = context->chainman->ActiveChain().Tip()->GetBlockHash();
        {
            LOCK(pwallet_a->cs_wallet);
            pwallet_a->SetLastBlockProcessed(last_height, last_hash);
        }
        {
            LOCK(pwallet_b->cs_wallet);
            pwallet_b->SetLastBlockProcessed(last_height, last_hash);
        }
    }

    std::string from_address_type, to_address_type;
    OutputTypes from_tx_type = OUTPUT_NULL;
    OutputTypes to_tx_type = OUTPUT_NULL;

    UniValue rv;

    CallRPC("extkeyimportmaster tprv8ZgxMBicQKsPeK5mCpvMsd1cwyT1JZsrBN82XkoYuZY1EVK7EwDaiL9sDfqUU5SntTfbRfnRedFWjg5xkDG5i3iwd3yP7neX5F2dtdCojk4", context, "a");
    CallRPC("extkeyimportmaster \"expect trouble pause odor utility palace ignore arena disorder frog helmet addict\"", context, "b");

    if (from == "plain") {
        from_address_type = "getnewaddress";
        from_tx_type = OUTPUT_STANDARD;
    } else if (from == "blind") {
        from_address_type = "getnewstealthaddress";
        from_tx_type = OUTPUT_CT;
    } else if (from == "anon") {
        from_address_type = "getnewstealthaddress";
        from_tx_type = OUTPUT_RINGCT;
    }

    if (to == "plain") {
        to_address_type = "getnewaddress";
        to_tx_type = OUTPUT_STANDARD;
    } else if (to == "blind") {
        to_address_type = "getnewstealthaddress";
        to_tx_type = OUTPUT_CT;
    } else if (to == "anon") {
        to_address_type = "getnewstealthaddress";
        to_tx_type = OUTPUT_RINGCT;
    }

    assert(from_tx_type != OUTPUT_NULL);
    assert(to_tx_type != OUTPUT_NULL);

    rv = CallRPC(from_address_type, context, "a");
    CBitcoinAddress addr_a(part::StripQuotes(rv.write()));

    rv = CallRPC(to_address_type, context, "b");
    CBitcoinAddress addr_b(part::StripQuotes(rv.write()));

    if (from == "anon" || from == "blind") {
        AddAnonTxn(pwallet_a.get(), addr_a, 1 * COIN, from == "anon" ? OUTPUT_RINGCT : OUTPUT_CT);
        AddAnonTxn(pwallet_a.get(), addr_a, 1 * COIN, from == "anon" ? OUTPUT_RINGCT : OUTPUT_CT);
        AddAnonTxn(pwallet_a.get(), addr_a, 1 * COIN, from == "anon" ? OUTPUT_RINGCT : OUTPUT_CT);
        AddAnonTxn(pwallet_a.get(), addr_a, 1 * COIN, from == "anon" ? OUTPUT_RINGCT : OUTPUT_CT);
        AddAnonTxn(pwallet_a.get(), addr_a, 1 * COIN, from == "anon" ? OUTPUT_RINGCT : OUTPUT_CT);

        StakeNBlocks(pwallet_a.get(), 2);
    }

    CTransactionRef tx = CreateTxn(pwallet_a.get(), owned ? addr_b : addr_a, 1000, from_tx_type, to_tx_type);

    CWalletTx::Confirmation confirm;
    bench.run([&] {
        LOCK(pwallet_b.get()->cs_wallet);
        pwallet_b.get()->AddToWalletIfInvolvingMe(tx, confirm, true);
    });

    RemoveWallet(wallet_context, pwallet_a, std::nullopt);
    pwallet_a.reset();

    RemoveWallet(wallet_context, pwallet_b, std::nullopt);
    pwallet_b.reset();

    ECC_Stop_Stealth();
    ECC_Stop_Blinding();
}

static void FalconAddTxPlainPlainNotOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "plain", false); }
static void FalconAddTxPlainPlainOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "plain", true); }
static void FalconAddTxPlainBlindNotOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "blind", false); }
static void FalconAddTxPlainBlindOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "blind", true); }
// static void FalconAddTxPlainAnonNotOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "anon", false); }
// static void FalconAddTxPlainAnonOwned(benchmark::Bench& bench) { AddTx(bench, "plain", "anon", true); }

static void FalconAddTxBlindPlainNotOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "plain", false); }
static void FalconAddTxBlindPlainOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "plain", true); }
static void FalconAddTxBlindBlindNotOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "blind", false); }
static void FalconAddTxBlindBlindOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "blind", true); }
static void FalconAddTxBlindAnonNotOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "anon", false); }
static void FalconAddTxBlindAnonOwned(benchmark::Bench& bench) { AddTx(bench, "blind", "anon", true); }

static void FalconAddTxAnonPlainNotOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "plain", false); }
static void FalconAddTxAnonPlainOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "plain", true); }
static void FalconAddTxAnonBlindNotOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "blind", false); }
static void FalconAddTxAnonBlindOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "blind", true); }
static void FalconAddTxAnonAnonNotOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "anon", false); }
static void FalconAddTxAnonAnonOwned(benchmark::Bench& bench) { AddTx(bench, "anon", "anon", true); }

BENCHMARK(FalconAddTxPlainPlainNotOwned);
BENCHMARK(FalconAddTxPlainPlainOwned);
BENCHMARK(FalconAddTxPlainBlindNotOwned);
BENCHMARK(FalconAddTxPlainBlindOwned);
// BENCHMARK(FalconAddTxPlainAnonNotOwned);
// BENCHMARK(FalconAddTxPlainAnonOwned);

BENCHMARK(FalconAddTxBlindPlainNotOwned);
BENCHMARK(FalconAddTxBlindPlainOwned);
BENCHMARK(FalconAddTxBlindBlindNotOwned);
BENCHMARK(FalconAddTxBlindBlindOwned);
BENCHMARK(FalconAddTxBlindAnonNotOwned);
BENCHMARK(FalconAddTxBlindAnonOwned);

BENCHMARK(FalconAddTxAnonPlainNotOwned);
BENCHMARK(FalconAddTxAnonPlainOwned);
BENCHMARK(FalconAddTxAnonBlindNotOwned);
BENCHMARK(FalconAddTxAnonBlindOwned);
BENCHMARK(FalconAddTxAnonAnonNotOwned);
BENCHMARK(FalconAddTxAnonAnonOwned);
