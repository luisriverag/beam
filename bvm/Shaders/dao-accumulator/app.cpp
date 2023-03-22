#include "../common.h"
#include "../common.h"
#include "../app_common_impl.h"
#include "../upgradable3/app_common_impl.h"
#include "contract.h"
#include "../dao-core2/contract.h"
#include "../amm/contract.h"

#define DaoAcc_schedule_upgrade(macro) Upgradable3_schedule_upgrade(macro)
#define DaoAcc_replace_admin(macro) Upgradable3_replace_admin(macro)
#define DaoAcc_set_min_approvers(macro) Upgradable3_set_min_approvers(macro)
#define DaoAcc_explicit_upgrade(macro) macro(ContractID, cid)

#define DaoAcc_deploy(macro) \
    Upgradable3_deploy(macro) \
    macro(Height, hPrePhaseEnd) \
    macro(ContractID, cidDaoCore)

#define DaoAcc_view_deployed(macro)
#define DaoAcc_view_params(macro) macro(ContractID, cid)
#define DaoAcc_user_view(macro) macro(ContractID, cid)
#define DaoAcc_users_view_all(macro) macro(ContractID, cid)

#define DaoAcc_start_farming(macro) \
    macro(ContractID, cid) \
    macro(ContractID, cidAmm) \
    macro(ContractID, cidDaoCore) \

#define DaoAcc_user_lock_prephase(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeamX) \
    macro(uint32_t, lockPeriods)

#define DaoAcc_user_update(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountBeamX) \
    macro(Amount, amountLpToken) \
    macro(uint32_t, bLockOrUnlock) \

#define DaoAcc_user_get_yield(macro) \
    macro(ContractID, cid) \
    macro(Amount, amountLpToken) \
    macro(uint32_t, bLockOrUnlock) \

#define DaoAccActions_All(macro) \
    macro(view_deployed) \
    macro(view_params) \
    macro(deploy) \
	macro(schedule_upgrade) \
	macro(replace_admin) \
	macro(set_min_approvers) \
	macro(explicit_upgrade) \
	macro(start_farming) \
	macro(user_view) \
	macro(users_view_all) \
	macro(user_lock_prephase) \
	macro(user_update) \
	macro(user_get_yield) \


namespace DaoAccumulator {

BEAM_EXPORT void Method_0()
{
    // scheme
    Env::DocGroup root("");

    {   Env::DocGroup gr("actions");

#define THE_FIELD(type, name) Env::DocAddText(#name, #type);
#define THE_ACTION(name) { Env::DocGroup gr(#name);  DaoAcc_##name(THE_FIELD) }
        
        DaoAccActions_All(THE_ACTION)
#undef THE_ACTION
#undef THE_FIELD
    }
}

#define THE_FIELD(type, name) const type& name,
#define ON_METHOD(name) void On_##name(DaoAcc_##name(THE_FIELD) int unused = 0)

void OnError(const char* sz)
{
    Env::DocAddText("error", sz);
}

const char g_szAdminSeed[] = "upgr3-dao-accumulator";

struct AdminKeyID :public Env::KeyID {
    AdminKeyID() :Env::KeyID(g_szAdminSeed, sizeof(g_szAdminSeed)) {}
};

const Upgradable3::Manager::VerInfo g_VerInfo = { s_pSID, _countof(s_pSID) };

ON_METHOD(view_deployed)
{
    AdminKeyID kid;
    g_VerInfo.DumpAll(&kid);
}

AssetID ReadAidBeamX(const ContractID& cidDaoCore)
{
    Env::Key_T<uint8_t> key;
    _POD_(key.m_Prefix.m_Cid) = cidDaoCore;
    key.m_KeyInContract = DaoCore2::Tags::s_State;

    DaoCore2::State s;
    if (!Env::VarReader::Read_T(key, s))
        s.m_Aid = 0;

    if (!s.m_Aid)
        OnError("Dao-Core state not found");

    return s.m_Aid;
}

ON_METHOD(deploy)
{
    AdminKeyID kid;
    PubKey pk;
    kid.get_Pk(pk);

    Method::Create arg;
    if (!g_VerInfo.FillDeployArgs(arg.m_Upgradable, &pk))
        return;

    if (hPrePhaseEnd <= Env::get_Height())
        return OnError("pre-phase too short");
    arg.m_hPrePhaseEnd = hPrePhaseEnd;

    arg.m_aidBeamX = ReadAidBeamX(cidDaoCore);
    if (!arg.m_aidBeamX)
        return;

    const uint32_t nCharge =
        Upgradable3::Manager::get_ChargeDeploy() +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::Cycle * 50;

    Env::GenerateKernel(nullptr, arg.s_iMethod, &arg, sizeof(arg), nullptr, 0, nullptr, 0, "Deploy Dao-Accumulator contract", nCharge);
}

ON_METHOD(schedule_upgrade)
{
    AdminKeyID kid;
    g_VerInfo.ScheduleUpgrade(cid, kid, hTarget);
}

ON_METHOD(explicit_upgrade)
{
    Upgradable3::Manager::MultiSigRitual::Perform_ExplicitUpgrade(cid);
}

ON_METHOD(replace_admin)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_ReplaceAdmin(cid, kid, iAdmin, pk);
}

ON_METHOD(set_min_approvers)
{
    AdminKeyID kid;
    Upgradable3::Manager::MultiSigRitual::Perform_SetApprovers(cid, kid, newVal);
}

struct MyState
    :public State
{
    bool Read(const ContractID& cid)
    {
        Env::Key_T<uint8_t> key;
        _POD_(key.m_Prefix.m_Cid) = cid;
        key.m_KeyInContract = Tags::s_State;

        if (Env::VarReader::Read_T(key, *this))
            return true;

        OnError("State not found");
        return false;
    }
};

ON_METHOD(view_params)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Env::DocGroup gr("res");
    Env::DocAddNum("aid-beamX", s.m_aidBeamX);
    Env::DocAddNum("hPreEnd", s.m_hPreEnd);

    Amount totalBeamX = WalkerFunds::FromContract_Lo(cid, s.m_aidBeamX);

    if (s.m_aidLpToken)
    {
        Env::DocAddNum("aid-LpToken", s.m_aidLpToken);
        Env::DocAddNum("locked-LpToken", WalkerFunds::FromContract_Lo(cid, s.m_aidLpToken));

        s.m_Pool.Update(Env::get_Height());

        if (s.m_Pool.m_hRemaining)
        {
            Env::DocAddNum("farm-remaining-height", s.m_Pool.m_hRemaining);
            Env::DocAddNum("farm-remaining-beamX", s.m_Pool.m_AmountRemaining);
        }

        Env::DocAddNum("farm-beamX-claimable", totalBeamX - s.m_Pool.m_AmountRemaining);
    }
    else
    {
        Env::DocAddNum("locked-Beam", WalkerFunds::FromContract_Lo(cid, 0));
        Env::DocAddNum("locked-BeamX", totalBeamX);
    }
}

ON_METHOD(start_farming)
{
    MyState s;
    if (!s.Read(cid))
        return;
    if (s.m_aidLpToken)
        return OnError("farming already started");

    Amount valBeams = WalkerFunds::FromContract_Lo(cid, 0);
    Amount valBeamX = valBeams / State::s_InitialRatio;

    Env::Key_T<Amm::Pool::Key> pk;
    _POD_(pk.m_Prefix.m_Cid) = cidAmm;
    pk.m_KeyInContract.m_ID.m_Aid1 = 0;
    pk.m_KeyInContract.m_ID.m_Aid2 = s.m_aidBeamX;
    pk.m_KeyInContract.m_ID.m_Fees.m_Kind = Amm::FeeSettings::s_Kinds - 1; // highest volatility

    Amm::Pool pool;
    if (!Env::VarReader::Read_T(pk, pool))
        return OnError("target pool not created");

    if (pool.m_Totals.m_Ctl)
        return OnError("target pool not empty");

    const Amount valFarmBeamX = 6'000'000ull * g_Beam2Groth;

    // 1. Activate farming
    {
        Method::FarmStart arg;
        arg.m_aidLpToken = pool.m_aidCtl;
        arg.m_FarmBeamX = valFarmBeamX;
        arg.m_hFarmDuration = 1440 * 365 * 2; // 2 years

        Upgradable3::Manager::MultiSigRitual msp;
        msp.m_szComment = "Dao-Accumulator Farming start";
        msp.m_iMethod = arg.s_iMethod;
        msp.m_nArg = sizeof(arg);
        msp.m_pCid = &cid;
        msp.m_Kid = AdminKeyID();

        FundsChange pFc[4];

        pFc[0].m_Consume = 0;
        pFc[0].m_Aid = 0;
        pFc[0].m_Amount = valBeams;

        pFc[1].m_Consume = 0;
        pFc[1].m_Aid = s.m_aidBeamX;
        pFc[1].m_Amount = valBeamX;

        pFc[2].m_Consume = 1;
        pFc[2].m_Aid = pool.m_aidCtl;
        pFc[2].m_Amount = valBeams;

        pFc[3].m_Consume = 1;
        pFc[3].m_Aid = s.m_aidBeamX;
        pFc[3].m_Amount = valFarmBeamX;

        msp.m_pFc = pFc;
        msp.m_nFc = _countof(pFc);

        msp.m_Charge +=
            Env::Cost::LoadVar_For(sizeof(State)) +
            Env::Cost::SaveVar_For(sizeof(State)) +
            Env::Cost::FundsLock * 3 +
            Env::Cost::Cycle * 1000;

        msp.Perform(arg);
    }

    // 2. Exchange liquidity for LP-token
    {
        Amm::Method::AddLiquidity arg;
        arg.m_Pid = pk.m_KeyInContract.m_ID;
        arg.m_Amounts.m_Tok1 = valBeams;
        arg.m_Amounts.m_Tok2 = valBeamX;

        FundsChange pFc[3];

        pFc[0].m_Consume = 1;
        pFc[0].m_Aid = 0;
        pFc[0].m_Amount = valBeams;

        pFc[1].m_Consume = 1;
        pFc[1].m_Aid = s.m_aidBeamX;
        pFc[1].m_Amount = valBeamX;

        pFc[2].m_Consume = 0;
        pFc[2].m_Aid = pool.m_aidCtl;
        pFc[2].m_Amount = valBeams;

        uint32_t nCharge =
            Env::Cost::LoadVar_For(sizeof(Amm::Pool)) +
            Env::Cost::SaveVar_For(sizeof(Amm::Pool)) +
            Env::Cost::FundsLock * 3 +
            Env::Cost::AssetEmit +
            Env::Cost::Cycle * 1000;

        Env::GenerateKernel(&cidAmm, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "Amm put liquidity", nCharge);
    }

    // 3. Withdraw BeamX for farming from dao-core
    {
        DaoCore2::Method::AdminWithdraw arg;
        arg.m_BeamX = valFarmBeamX;

        Upgradable3::Manager::MultiSigRitual msp;
        msp.m_szComment = "Dao-Core Withdraw unassigned BeamX";
        msp.m_iMethod = arg.s_iMethod;
        msp.m_nArg = sizeof(arg);
        msp.m_pCid = &cidDaoCore;

        const char g_szDaoCoreAdminSeed[] = "upgr2-dao-core";
        msp.m_Kid.m_pID = g_szDaoCoreAdminSeed;
        msp.m_Kid.m_nID = sizeof(g_szDaoCoreAdminSeed);

        FundsChange fc;
        fc.m_Consume = 0;
        fc.m_Aid = s.m_aidBeamX;
        fc.m_Amount = valFarmBeamX;


        msp.m_pFc = &fc;
        msp.m_nFc = 1;

        msp.m_Charge +=
            Env::Cost::LoadVar_For(sizeof(Amount)) +
            Env::Cost::SaveVar_For(sizeof(Amount)) +
            Env::Cost::FundsLock +
            Env::Cost::Cycle * 100;

        msp.Perform(arg);
    }

}

struct MyUser
    :public User
{
    bool Load(const ContractID& cid)
    {
        Env::Key_T<User::Key> uk;
        _POD_(uk.m_Prefix.m_Cid) = cid;
        Env::KeyID(cid).get_Pk(uk.m_KeyInContract.m_pk);

        if (Env::VarReader::Read_T(uk, *this))
            return true;

        OnError("no user");
        return false;
    }

    bool AdjustTokens(Amount val, uint32_t bLockOrUnlock)
    {
        if (bLockOrUnlock)
            Strict::Add(m_LpTokenPostPhase, val);
        else
        {
            if (m_LpTokenPostPhase >= val)
                m_LpTokenPostPhase -= val;
            else
            {
                val -= m_LpTokenPostPhase;
                m_LpTokenPostPhase = 0;

                if (m_LpTokenPrePhase < val)
                {
                    OnError("not enough LP-tokens");
                    return false;
                }

                m_LpTokenPrePhase -= val;
            }
        }

        return true;
    }

    void AddEarned(State& s)
    {
        m_EarnedBeamX += s.m_Pool.Remove(m_PoolUser);
    }

    void Print(State& s)
    {
        Env::DocAddNum32("lock-periods", m_PrePhaseLockPeriods);
        Env::DocAddNum("lpToken-pre", m_LpTokenPrePhase);

        Env::DocAddNum("unlock-height", get_UnlockHeight(s));

        if (s.m_aidLpToken)
        {
            AddEarned(s);
            Env::DocAddNum("lpToken-post", m_LpTokenPostPhase);
            Env::DocAddNum("avail-BeamX", m_EarnedBeamX);
        }

    }
};

ON_METHOD(user_view)
{
    MyState s;
    if (!s.Read(cid))
        return;

    if (s.m_aidLpToken)
        s.m_Pool.Update(Env::get_Height());

    MyUser u;
    if (!u.Load(cid))
        return;

    Env::DocGroup gr("res");

    u.Print(s);
}

ON_METHOD(users_view_all)
{
    MyState s;
    if (!s.Read(cid))
        return;

    if (s.m_aidLpToken)
        s.m_Pool.Update(Env::get_Height());

    Env::Key_T<User::Key> k0, k1;
    _POD_(k0.m_Prefix.m_Cid) = cid;
    _POD_(k1.m_Prefix.m_Cid) = cid;
    _POD_(k0.m_KeyInContract.m_pk).SetZero();
    _POD_(k1.m_KeyInContract.m_pk).SetObject(0xff);

    Env::DocArray gr0("res");

    MyUser u;
    for (Env::VarReader r(k0, k1); r.MoveNext_T(k0, u); )
    {
        Env::DocGroup gr1("");
        u.Print(s);
        Env::DocAddBlob_T("pk", k0.m_KeyInContract.m_pk);
    }
}

ON_METHOD(user_lock_prephase)
{
    if (!amountBeamX)
        return OnError("amount not specified");
    if (lockPeriods > User::s_PreLockPeriodsMax)
        return OnError("lock period too large");

    MyState s;
    if (!s.Read(cid))
        return;

    if (Env::get_Height() >= s.m_hPreEnd)
        return OnError("pre-phase is over");

    Method::UserLockPrePhase arg;
    arg.m_AmountBeamX = amountBeamX;
    arg.m_PrePhaseLockPeriods = lockPeriods;
    Env::KeyID(cid).get_Pk(arg.m_pkUser);

    FundsChange pFc[2];
    pFc[0].m_Aid = 0;
    pFc[0].m_Consume = 1;
    pFc[0].m_Amount = amountBeamX * State::s_InitialRatio;
    pFc[1].m_Aid = s.m_aidBeamX;
    pFc[1].m_Consume = 1;
    pFc[1].m_Amount = amountBeamX;

    const uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::LoadVar_For(sizeof(User)) +
        Env::Cost::SaveVar_For(sizeof(User)) +
        Env::Cost::FundsLock * 2 +
        Env::Cost::Cycle * 200;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), nullptr, 0, "Dao-Accumulator Lock pre-phase", nCharge);
}

ON_METHOD(user_update)
{
    if (!amountBeamX && !amountLpToken)
        return OnError("no effect");

    MyState s;
    if (!s.Read(cid))
        return;

    if (!s.m_aidLpToken)
        return OnError("can't update before farming");

    s.m_Pool.Update(Env::get_Height());

    MyUser u;
    if (u.Load(cid))
        u.AddEarned(s);
    else
        _POD_(u).SetZero();

    if (!u.AdjustTokens(amountLpToken, bLockOrUnlock))
        return;

    FundsChange pFc[2];
    pFc[0].m_Aid = s.m_aidBeamX;
    pFc[0].m_Consume = 0;
    pFc[0].m_Amount = amountBeamX;
    pFc[1].m_Aid = s.m_aidLpToken;
    pFc[1].m_Consume = !!bLockOrUnlock;
    pFc[1].m_Amount = amountLpToken;

    Method::UserUpdate arg;
    arg.m_WithdrawBeamX = amountBeamX;
    arg.m_NewLpToken = u.m_LpTokenPrePhase + u.m_LpTokenPostPhase;

    Env::KeyID kid(cid);
    kid.get_Pk(arg.m_pkUser);

    uint32_t nCharge =
        Env::Cost::CallFar +
        Env::Cost::LoadVar_For(sizeof(State)) +
        Env::Cost::SaveVar_For(sizeof(State)) +
        Env::Cost::LoadVar_For(sizeof(User)) +
        Env::Cost::SaveVar_For(sizeof(User)) +
        Env::Cost::FundsLock * 2 +
        Env::Cost::AddSig +
        Env::Cost::Cycle * 2000;

    Env::GenerateKernel(&cid, arg.s_iMethod, &arg, sizeof(arg), pFc, _countof(pFc), &kid, 1, "Lock/Unlock and get farmed beamX tokens", nCharge);
}

ON_METHOD(user_get_yield)
{
    MyState s;
    if (!s.Read(cid))
        return;

    Amount res = 0;

    if (s.m_aidLpToken)
    {
        Height h = Env::get_Height();

        MyUser u;
        if (u.Load(cid))
            u.AddEarned(s);
        else
            _POD_(u).SetZero();

        s.m_Pool.Update(h);
        u.AddEarned(s);
        res = u.m_EarnedBeamX;

        if (!u.AdjustTokens(amountLpToken, bLockOrUnlock))
            return;

        u.m_PoolUser.m_Weight = u.get_WeightPrePhase() + u.get_WeightPostPhase();
        u.m_PoolUser.m_Sigma0 = s.m_Pool.m_Sigma;
        s.m_Pool.m_Weight += u.m_PoolUser.m_Weight;

        s.m_Pool.Update(h + 1440);
        u.AddEarned(s);
        res = u.m_EarnedBeamX - res;
    }

    Env::DocGroup gr("res");
    Env::DocAddNum("daily_reward", res);
}

#undef ON_METHOD
#undef THE_FIELD

BEAM_EXPORT void Method_1() 
{
    Env::DocGroup root("");

    char szAction[0x20];

    if (!Env::DocGetText("action", szAction, sizeof(szAction)))
        return OnError("Action not specified");

#define PAR_READ(type, name) type arg_##name; Env::DocGet(#name, arg_##name);
#define PAR_PASS(type, name) arg_##name,

#define THE_METHOD(name) \
        static_assert(sizeof(szAction) >= sizeof(#name)); \
        if (!Env::Strcmp(szAction, #name)) { \
            DaoAcc_##name(PAR_READ) \
            On_##name(DaoAcc_##name(PAR_PASS) 0); \
            return; \
        }

    DaoAccActions_All(THE_METHOD)

#undef THE_METHOD
#undef PAR_PASS
#undef PAR_READ

    OnError("unknown action");
}

} // namespace DaoAccumulator