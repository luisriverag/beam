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
#include "dex_board.h"
#include "wallet/transactions/dex/dex_tx.h"
#include "wallet/client/extensions/broadcast_gateway/broadcast_msg_creator.h"
#include "wallet/client/wallet_client.h"

namespace beam::wallet {

    DexBoard::DexBoard(IBroadcastMsgGateway& gatewayBroadcast, IRawCommGateway& gatewayCtlListen, IWalletDB& wdb)
        : _gatewayBroadcast(gatewayBroadcast)
        , _gatewayCtlListen(gatewayCtlListen)
        , _wdb(wdb)
    {
        auto offersRaw = _wdb.loadDexOffers();
        for (const auto& offerRaw: offersRaw)
        {
            try
            {
                DexOrder offer(offerRaw.first, offerRaw.second);
                if (!offer.isExpired())
                {
                    _orders[offer.getID()] = offer;
                    CtlListen(offer, true);
                }
                else
                    _wdb.dropDexOffer(offer.getID());
            }
            catch(...)
            {
                BEAM_LOG_WARNING() << "DexBoard load order error";
            }
        }

        _gatewayBroadcast.registerListener(BroadcastContentType::DexOffers, this);
    }

    DexBoard::~DexBoard() {}

    std::vector<DexOrder> DexBoard::getDexOrders() const
    {
        std::vector<DexOrder> result;
        result.reserve(_orders.size());

        for (auto pair: _orders)
        {
            result.push_back(pair.second);
        }

        return result;
    }

    boost::optional<DexOrder> DexBoard::getDexOrder(const DexOrderID& orderId) const
    {
        const auto it = _orders.find(orderId);
        if (it != _orders.end())
        {
            return it->second;
        }
        return boost::none;
    }

    void DexBoard::publishOrder(const DexOrder &offer)
    {
        auto message = createMessage(offer);
        _gatewayBroadcast.sendMessage(BroadcastContentType::DexOffers, message);
    }

    void DexBoard::cancelDexOrder(const DexOrderID& id)
    {
        auto it = _orders.find(id);
        if(it == _orders.end() || !it->second.isMine()) return;

        CtlListen(it->second, false);
        it->second.cancel();
        publishOrder(it->second);
    }

    bool DexBoard::handleDexOrder(const boost::optional<DexOrder>& order)
    {
        if (!order) return false;

        auto it = _orders.find(order->getID());
        if (it == _orders.end())
        {
            if (!order->isCanceled() && !order->isExpired() && !order->isAccepted())
            {
                _orders[order->getID()] = *order;
                _wdb.saveDexOffer(order->getID(), toByteBuffer(*order), order->isMine());
                notifyObservers(ChangeAction::Added, std::vector<DexOrder>{ *order });
                CtlListen(*order, true);
            }
        }
        else
        {
            if (order->isCanceled() || order->isAccepted())
            {
                notifyObservers(ChangeAction::Removed, std::vector<DexOrder>{ *order });
                _wdb.dropDexOffer(order->getID());
                CtlListen(it->second, false);
                _orders.erase(it);
            }
            else
            {
                _orders[order->getID()] = *order;
                notifyObservers(ChangeAction::Updated, std::vector<DexOrder>{ *order });
            }
        }

        return true;
    }

    bool DexBoard::onMessage(BroadcastMsg&& msg)
    {
        auto order = parseAssetSwapMessage(msg);
        return order ? handleDexOrder(order) : false;
    }

    BroadcastMsg DexBoard::createMessage(const DexOrder& order)
    {
        ECC::Scalar::Native sk;
        WalletID wid;
        _wdb.get_SbbsWalletID(sk, wid, order.get_KeyID());

        auto buffer = toByteBuffer(order);
        auto message = BroadcastMsgCreator::createSignedMessage(buffer, sk);

        return message;
    }

    boost::optional<DexOrder> DexBoard::parseAssetSwapMessage(const BroadcastMsg& msg)
    {
        try
        {
            DexOrder order(msg.m_content, msg.m_signature);

            WalletID wid;
            _wdb.get_SbbsWalletID(wid, order.get_KeyID());

            bool isMine = (order.getSBBSID() == wid);
            order.setIsMine(isMine);

            return order;
        }
        catch(std::runtime_error& err)
        {
            BEAM_LOG_WARNING() << "DexBoard parse error: " << err.what();
            return boost::none;
        }
        catch(...)
        {
            BEAM_LOG_WARNING() << "DexBoard message type parse error";
            return boost::none;
        }
    }

    void DexBoard::notifyObservers(ChangeAction action, const std::vector<DexOrder>& orders) const
    {
        for (const auto obs : _observers)
        {
            obs->onDexOrdersChanged(action, orders);
        }
    }

    bool DexBoard::acceptIncomingDexSS(const SetTxParameter& msg)
    {
        DexOrderID id;
        msg.GetParameter(TxParameterID::ExternalDexOrderID, id);

        auto it = _orders.find(id);
        if (it == _orders.end()) return false;

        if (!it->second.isMine()
            || it->second.isExpired()
            || it->second.isCanceled()
            || it->second.isAccepted()) return false;

        it->second.setAccepted(true);
        publishOrder(it->second);

        return true;
    }

    void DexBoard::onDexTxCreated(const SetTxParameter& msg, BaseTransaction::Ptr tx)
    {
        DexOrderID id;
        msg.GetParameter(TxParameterID::ExternalDexOrderID, id);

        auto it = _orders.find(id);
        if (it == _orders.end()) return;

        if (it->second.isAccepted())
            publishOrder(it->second);
    }

    void DexBoard::onSystemStateChanged(const Block::SystemState::ID& stateID)
    {
        std::vector<DexOrderID> notActualOffers;
        for (const auto& it: _orders)
        {
            const auto& offer = it.second;
            if (offer.isExpired())
            {
                CtlListen(offer, false);
                notActualOffers.emplace_back(it.first);
                notifyObservers(ChangeAction::Removed, std::vector<DexOrder>{ it.second });
                _wdb.dropDexOffer(it.first);
            }
        }

        for (auto idForRemove: notActualOffers)
        {
            _orders.erase(idForRemove);
        }
    }

    void DexBoard::CtlListen(const DexOrder& order, bool bListen)
    {
        if (bListen)
        {
            WalletID wid;
            ECC::Scalar::Native sk;
            _wdb.get_SbbsWalletID(sk, wid, order.get_KeyID());

            _gatewayCtlListen.Listen(wid, sk);
        }
        else
            _gatewayCtlListen.Unlisten(order.getSBBSID());
    }

}
