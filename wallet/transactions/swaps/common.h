// Copyright 2019 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "wallet/core/common.h"

namespace beam::wallet
{
constexpr Height kBeamLockTimeInBlocks = 6 * 60;  // 6h
constexpr Height kMaxSentTimeOfBeamRedeemInBlocks = kBeamLockTimeInBlocks - 60;  // 6h - 1h
constexpr Height kBeamLockTxLifetimeMax = 4 * 60;   // 4h

constexpr uint32_t kSwapProtoVersion = 5;
constexpr uint32_t kSwapSegwitSupportMinProtoVersion = 5;

bool UseMainnetSwap();
extern bool g_EnforceTestnetSwap;

enum SubTxIndex : SubTxID
{
    BEAM_LOCK_TX = 2,
    BEAM_REFUND_TX = 3,
    BEAM_REDEEM_TX = 4,
    LOCK_TX = 5,
    REFUND_TX = 6,
    REDEEM_TX = 7
};

enum class SwapTxState : uint8_t
{
    Initial,
    CreatingTx,
    SigningTx,
    Constructed
};

enum class AtomicSwapCoin : int32_t // explicit signed type for serialization backward compatibility
{
    Bitcoin,
    Litecoin,
    Qtum,
    Bitcoin_Cash,
    Dogecoin,
    Dash,
    Ethereum,
    Dai,
    Usdt,
    WBTC,
    Unknown
};

const AtomicSwapCoin kEthTokens[] = { AtomicSwapCoin::Dai, AtomicSwapCoin::Usdt, AtomicSwapCoin::WBTC };

bool IsEthToken(AtomicSwapCoin swapCoin);

enum class SwapOfferStatus : uint32_t
{
    Pending,
    InProgress,
    Completed,
    Canceled,
    Expired,
    Failed
};

AtomicSwapCoin from_string(const std::string& value);
uint64_t UnitsPerCoin(AtomicSwapCoin swapCoin) noexcept;
// TODO roman.strilets: maybe it is bad name
std::string GetCoinName(AtomicSwapCoin swapCoin);
std::string swapOfferStatusToString(const SwapOfferStatus& status);
}  // namespace beam::wallet

namespace std
{
    string to_string(beam::wallet::AtomicSwapCoin value);
    string to_string(beam::wallet::SwapOfferStatus status);  
}  // namespace std

namespace beam::electrum
{
std::vector<std::string> generateReceivingAddresses
    (wallet::AtomicSwapCoin swapCoin, const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion);
std::vector<std::string> generateChangeAddresses
    (wallet::AtomicSwapCoin swapCoin, const std::vector<std::string>& words, uint32_t amount, uint8_t addressVersion);

bool validateMnemonic(const std::vector<std::string>& words, bool isSegwitType = false);
std::vector<std::string> createMnemonic(const std::vector<uint8_t>& entropy);
bool isAllowedWord(const std::string& word);
}  // namespace beam::electrum
