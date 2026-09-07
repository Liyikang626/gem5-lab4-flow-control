/*
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/OutputUnit.hh"

#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/Credit.hh"
#include "mem/ruby/network/garnet/CreditLink.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/network/garnet/flitBuffer.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

OutputUnit::OutputUnit(int id, PortDirection direction, Router *router,
  uint32_t consumerVcs)
  : Consumer(router), m_router(router), m_id(id), m_direction(direction),
    m_vc_per_vnet(consumerVcs),
    m_elastic(m_router->get_net_ptr()->isElasticTokenEnabled())
{
    const int m_num_vcs = consumerVcs * m_router->get_num_vnets();
    outVcState.reserve(m_num_vcs);
    for (int i = 0; i < m_num_vcs; i++) {
        outVcState.emplace_back(i, m_router->get_net_ptr(), consumerVcs);
    }

    m_next_vc.resize(m_router->get_num_vnets(), 0);
    if (m_elastic) {
        m_elastic_credits.resize(m_router->get_num_vnets());
        for (int vnet = 0; vnet < m_router->get_num_vnets(); vnet++) {
            int depth = m_router->get_net_ptr()->get_vnet_type(vnet) ==
                DATA_VNET_ ?
                m_router->get_net_ptr()->getBuffersPerDataVC() :
                m_router->get_net_ptr()->getBuffersPerCtrlVC();
            m_elastic_credits[vnet] = consumerVcs * depth;
        }
    }

}

void
OutputUnit::decrement_credit(int out_vc)
{
    auto network = m_router->get_net_ptr();
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local") {
        assert(m_elastic_credits[vnet] > 0);
        m_elastic_credits[vnet]--;
        network->consumeElasticSlot(m_router->get_id(), m_direction, vnet);
        return;
    }
    DPRINTF(RubyNetwork, "Router %d OutputUnit %s decrementing credit:%d for "
            "outvc %d at time: %lld for %s\n", m_router->get_id(),
            m_router->getPortDirectionName(get_direction()),
            outVcState[out_vc].get_credit_count(),
            out_vc, m_router->curCycle(), m_credit_link->name());

    outVcState[out_vc].decrement_credit();
}

void
OutputUnit::increment_credit(int out_vc)
{
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local") {
        m_elastic_credits[vnet]++;
        return;
    }
    DPRINTF(RubyNetwork, "Router %d OutputUnit %s incrementing credit:%d for "
            "outvc %d at time: %lld from:%s\n", m_router->get_id(),
            m_router->getPortDirectionName(get_direction()),
            outVcState[out_vc].get_credit_count(),
            out_vc, m_router->curCycle(), m_credit_link->name());

    outVcState[out_vc].increment_credit();
}

// Check if the output VC (i.e., input VC at next router)
// has free credits (i..e, buffer slots).
// This is tracked by OutVcState
bool
OutputUnit::has_credit(int out_vc, int min_credits)
{
    assert(outVcState[out_vc].isInState(ACTIVE_, curTick()));
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local")
        return m_elastic_credits[vnet] >= min_credits;
    return outVcState[out_vc].get_credit_count() >= min_credits;
}


// Check if the output port (i.e., input port at next router) has free VCs.
bool
OutputUnit::has_free_vc(int vnet, int min_credits, int vc_offset)
{
    int vc_base = vnet*m_vc_per_vnet;
    int vc_begin = vc_base;
    int vc_end = vc_base + m_vc_per_vnet;
    if (vc_offset >= 0) {
        assert(vc_offset < m_vc_per_vnet);
        vc_begin += vc_offset;
        vc_end = vc_begin + 1;
    }
    bool wormhole = m_router->get_net_ptr()->isWormholeEnabled();
    if (m_elastic && m_direction != "Local") {
        if (m_elastic_credits[vnet] < min_credits)
            return false;
        for (int vc = vc_begin; vc < vc_end; vc++) {
            if (is_vc_idle(vc, curTick()) || wormhole)
                return true;
        }
        return false;
    }
    for (int vc = vc_begin; vc < vc_end; vc++) {
        bool vc_available = is_vc_idle(vc, curTick()) || wormhole;
        int credits = outVcState[vc].get_credit_count();
        if (vc_available && credits >= min_credits)
            return true;
    }

    return false;
}

// Assign a free output VC to the winner of Switch Allocation
int
OutputUnit::select_free_vc(int vnet, int min_credits, int vc_offset)
{
    int vc_base = vnet*m_vc_per_vnet;
    int vc_begin = vc_base;
    int vc_end = vc_base + m_vc_per_vnet;
    if (vc_offset >= 0) {
        assert(vc_offset < m_vc_per_vnet);
        vc_begin += vc_offset;
        vc_end = vc_begin + 1;
    }
    bool wormhole = m_router->get_net_ptr()->isWormholeEnabled();

    if (m_elastic && m_direction != "Local") {
        if (m_elastic_credits[vnet] < min_credits)
            return -1;
        for (int vc = vc_begin; vc < vc_end; vc++) {
            if (is_vc_idle(vc, curTick()) || wormhole) {
                if (is_vc_idle(vc, curTick()))
                    outVcState[vc].setState(ACTIVE_, curTick());
                return vc;
            }
        }
        return -1;
    }

    for (int vc = vc_begin; vc < vc_end; vc++) {
        int credits = outVcState[vc].get_credit_count();
        if (is_vc_idle(vc, curTick()) && credits >= min_credits) {
            outVcState[vc].setState(ACTIVE_, curTick());
            return vc;
        }
        if (wormhole && credits >= min_credits)
            return vc;
    }

    return -1;
}

/*
 * The wakeup function of the OutputUnit reads the credit signal from the
 * downstream router for the output VC (i.e., input VC at downstream router).
 * It increments the credit count in the appropriate output VC state.
 * If the credit carries is_free_signal as true,
 * the output VC is marked IDLE.
 */

void
OutputUnit::wakeup()
{
    if (m_credit_link->isReady(curTick())) {
        Credit *t_credit = (Credit*) m_credit_link->consumeLink();
        increment_credit(t_credit->get_vc());

        if (t_credit->is_free_signal())
            set_vc_state(IDLE_, t_credit->get_vc(), curTick());

        delete t_credit;

        if (m_credit_link->isReady(curTick())) {
            scheduleEvent(Cycles(1));
        }
    }
}

flitBuffer*
OutputUnit::getOutQueue()
{
    return &outBuffer;
}

void
OutputUnit::set_out_link(NetworkLink *link)
{
    m_out_link = link;
}

void
OutputUnit::set_credit_link(CreditLink *credit_link)
{
    m_credit_link = credit_link;
}

void
OutputUnit::insert_flit(flit *t_flit)
{
    outBuffer.insert(t_flit);
    m_out_link->scheduleEventAbsolute(m_router->clockEdge(Cycles(1)));
}

bool
OutputUnit::functionalRead(Packet *pkt, WriteMask &mask)
{
    return outBuffer.functionalRead(pkt, mask);
}

uint32_t
OutputUnit::functionalWrite(Packet *pkt)
{
    return outBuffer.functionalWrite(pkt);
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
