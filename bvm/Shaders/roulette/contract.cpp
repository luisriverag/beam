#include "../common.h"
#include "../Math.h"
#include "contract.h"

export void Ctor(const Roulette::Params& r)
{
    Roulette::State s;
    Utils::ZeroObject(s);

    static const char szMeta[] = "roulette jetton";
    s.m_Aid = Env::AssetCreate(szMeta, sizeof(szMeta) - 1);
    Env::Halt_if(!s.m_Aid);

    s.m_Dealer = r.m_Dealer;
    Env::SaveVar_T((uint8_t) 0, s);
}

export void Dtor(void*)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Env::Halt_if(!Env::AssetDestroy(s.m_Aid));
    Env::DelVar_T((uint8_t) 0);

    Env::AddSig(s.m_Dealer);
}

export void Method_2(const Roulette::Restart& r)
{
    Env::Halt_if(!(r.m_dhRound));

    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Height h = Env::get_Height();
    Env::Halt_if(s.m_hRoundEnd > h);

    s.m_hRoundEnd = h;
    Strict::Add(s.m_hRoundEnd, r.m_dhRound);

    if (r.m_PlayingSectors)
    {
        Env::Halt_if(r.m_PlayingSectors > Roulette::State::s_Sectors);
        s.m_PlayingSectors = r.m_PlayingSectors;
    }
    else
        s.m_PlayingSectors = Roulette::State::s_Sectors;

    Env::SaveVar_T((uint8_t) 0, s);

    Env::AddSig(s.m_Dealer);
}

export void Method_3(const Roulette::PlaceBid& r)
{
    Env::Halt_if(r.m_iSector >= Roulette::State::s_Sectors);

    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Height h = Env::get_Height();
    Env::Halt_if(h >= s.m_hRoundEnd);

    Roulette::BidInfo bi;
    Env::Halt_if(Env::LoadVar_T(r.m_Player, bi) && (bi.m_hRoundEnd == s.m_hRoundEnd)); // bid already placed for this round

    // looks good
    bi.m_iSector = r.m_iSector; // don't care if out-of-bounds for the current round with limited num of sectors
    bi.m_hRoundEnd = s.m_hRoundEnd;
    Env::SaveVar_T(r.m_Player, bi);

    Env::AddSig(r.m_Player);
}

export void Method_4(const Roulette::Take& r)
{
    Roulette::State s;
    Env::LoadVar_T((uint8_t) 0, s);

    Roulette::BidInfo bi;
    Env::Halt_if(!Env::LoadVar_T(r.m_Player, bi) || (s.m_hRoundEnd != bi.m_hRoundEnd));

    uint32_t iWinSector = s.DeriveWinSector();

    Amount nWonAmount;
    if (bi.m_iSector == iWinSector)
        nWonAmount = s.s_PrizeSector;
    else
    {
        Env::Halt_if(1 & (bi.m_iSector ^ iWinSector));
        nWonAmount = s.s_PrizeParity;
    }

    Env::DelVar_T(r.m_Player);

    // looks good. The amount that should be withdrawn is: val / winners

    Env::Halt_if(!Env::AssetEmit(s.m_Aid, nWonAmount, 1));
    Env::FundsUnlock(s.m_Aid, nWonAmount);
    Env::AddSig(r.m_Player);
}