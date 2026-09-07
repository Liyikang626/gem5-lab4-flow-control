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
        m_elastic_vc_loads.resize(m_num_vcs, 0);
        m_elastic_credits.resize(m_router->get_num_vnets());
        for (int vnet = 0; vnet < m_router->get_num_vnets(); vnet++) {
            int depth = m_router->get_net_ptr()->get_vnet_type(vnet) ==
                DATA_VNET_ ?
                m_router->get_net_ptr()->getBuffersPerDataVC() :
                m_router->get_net_ptr()->getBuffersPerCtrlVC();
            m_elastic_credits[vnet] = consumerVcs * depth;
        }
    }

    auto network = m_router->get_net_ptr();
    const std::string& topology = network->getLabTopology();
    bool ring_direction =
        direction == "Clockwise" || direction == "CounterClockwise";
    bool torus_direction =
        direction == "East" || direction == "West" ||
        direction == "North" || direction == "South";
    if (!network->isBalancedBubbleEnabled() || network->isBubbleEnabled() ||
        !((topology == "Ring" && ring_direction) ||
          (topology == "Torus2D" && torus_direction))) {
        return;
    }

    int router_id = m_router->get_id();
    int ring_size = network->getNumRouters();
    int position = router_id;
    if (topology == "Torus2D") {
        int rows = network->getNumRows();
        int cols = network->getNumRouters() / rows;
        bool horizontal = direction == "East" || direction == "West";
        ring_size = horizontal ? cols : rows;
        position = horizontal ? router_id % cols : router_id / cols;
    }

    int bubbles = network->getCriticalBubbles();
    if (bubbles == 0)
        bubbles = ring_size;
    int local_bubbles = bubbles / ring_size;
    if (position < bubbles % ring_size)
        local_bubbles++;

    for (int vnet = 0; vnet < m_router->get_num_vnets(); vnet++) {
        for (int i = 0; i < local_bubbles; i++) {
            int vc_offset = (position + i) % m_vc_per_vnet;
            outVcState[vnet * m_vc_per_vnet + vc_offset]
                .mark_critical_credit();
            network->addCriticalCredit(router_id, direction, vnet);
        }
    }
}

bool
OutputUnit::decrement_credit(int out_vc)
{
    auto network = m_router->get_net_ptr();
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local") {
        assert(m_elastic_credits[vnet] > 0);
        m_elastic_credits[vnet]--;
        m_elastic_vc_loads[out_vc]++;
        network->consumeElasticSlot(m_router->get_id(), m_direction, vnet);
        return false;
    }
    bool critical = network->isBalancedBubbleEnabled() &&
                    outVcState[out_vc].get_credit_count() ==
                        outVcState[out_vc].get_critical_count();
    DPRINTF(RubyNetwork, "Router %d OutputUnit %s decrementing credit:%d for "
            "outvc %d critical:%d at time: %lld for %s\n", m_router->get_id(),
            m_router->getPortDirectionName(get_direction()),
            outVcState[out_vc].get_credit_count(),
            out_vc, critical, m_router->curCycle(), m_credit_link->name());

    outVcState[out_vc].decrement_credit(critical);
    if (critical)
        network->consumeCriticalCredit(
            m_router->get_id(), m_direction, vnet);
    if (network->isSharedBubbleEnabled())
        network->consumeSharedCredit(
            m_router->get_id(), m_direction, vnet);
    return critical;
}

void
OutputUnit::increment_credit(int out_vc, bool critical)
{
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local") {
        m_elastic_credits[vnet]++;
        m_elastic_vc_loads[out_vc]--;
        m_router->get_net_ptr()->releaseElasticSlot(
            m_router->get_id(), m_direction, vnet);
        return;
    }
    DPRINTF(RubyNetwork, "Router %d OutputUnit %s incrementing credit:%d for "
            "outvc %d at time: %lld from:%s\n", m_router->get_id(),
            m_router->getPortDirectionName(get_direction()),
            outVcState[out_vc].get_credit_count(),
            out_vc, m_router->curCycle(), m_credit_link->name());

    outVcState[out_vc].increment_credit(critical);
    if (critical)
        m_router->get_net_ptr()->addCriticalCredit(
            m_router->get_id(), m_direction, vnet);
    if (m_router->get_net_ptr()->isSharedBubbleEnabled())
        m_router->get_net_ptr()->returnSharedCredit(
            m_router->get_id(), m_direction, vnet);
}

// Check if the output VC (i.e., input VC at next router)
// has free credits (i..e, buffer slots).
// This is tracked by OutVcState
bool
OutputUnit::has_credit(int out_vc, int min_credits, bool protect_critical)
{
    assert(outVcState[out_vc].isInState(ACTIVE_, curTick()));
    int vnet = out_vc / m_vc_per_vnet;
    if (m_elastic && m_direction != "Local")
        return m_elastic_credits[vnet] >= min_credits;
    int credits = outVcState[out_vc].get_credit_count();
    if (protect_critical)
        credits -= outVcState[out_vc].get_critical_count();
    return credits >= min_credits;
}


// Check if the output port (i.e., input port at next router) has free VCs.
bool
OutputUnit::has_free_vc(int vnet, int min_credits, int vc_offset,
                        bool protect_critical)
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
        if (protect_critical)
            credits -= outVcState[vc].get_critical_count();
        if (vc_available && credits >= min_credits)
            return true;
    }

    return false;
}

// Assign a free output VC to the winner of Switch Allocation
int
OutputUnit::select_free_vc(int vnet, int min_credits, int vc_offset,
                           bool protect_critical, bool balance,
                           bool prefer_normal)
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
        int count = vc_end - vc_begin;
        int start = vc_offset >= 0 ? 0 : m_next_vc[vnet];
        int best_vc = -1;
        for (int i = 0; i < count; i++) {
            int offset = vc_offset >= 0 ? vc_offset :
                         (start + i) % m_vc_per_vnet;
            int vc = vc_base + offset;
            if ((is_vc_idle(vc, curTick()) || wormhole) &&
                (best_vc == -1 ||
                 m_elastic_vc_loads[vc] < m_elastic_vc_loads[best_vc]))
                best_vc = vc;
        }
        if (best_vc == -1)
            return -1;
        if (is_vc_idle(best_vc, curTick()))
            outVcState[best_vc].setState(ACTIVE_, curTick());
        m_next_vc[vnet] = (best_vc - vc_base + 1) % m_vc_per_vnet;
        return best_vc;
    }

    if (balance) {
        int first_vc = -1;
        int best_vc = -1;
        int best_credits = -1;
        int first_credits = -1;
        bool found_normal = false;
        int count = vc_end - vc_begin;
        int start = vc_offset >= 0 ? 0 : m_next_vc[vnet];
        for (int i = 0; i < count; i++) {
            int offset = vc_offset >= 0 ? vc_offset :
                         (start + i) % m_vc_per_vnet;
            int vc = vc_base + offset;
            bool vc_available = is_vc_idle(vc, curTick()) || wormhole;
            int credits = outVcState[vc].get_credit_count();
            if (protect_critical)
                credits -= outVcState[vc].get_critical_count();
            if (!vc_available || credits < min_credits)
                continue;
            int normal_credits = protect_critical ? credits :
                credits - outVcState[vc].get_critical_count();
            if (prefer_normal && normal_credits > 0 && !found_normal) {
                found_normal = true;
                best_vc = -1;
                best_credits = -1;
            }
            if (prefer_normal && found_normal && normal_credits == 0)
                continue;
            if (first_vc == -1)
                first_vc = vc;
            int score = prefer_normal && found_normal ? normal_credits : credits;
            if (first_credits == -1)
                first_credits = score;
            if (score > best_credits) {
                best_vc = vc;
                best_credits = score;
            }
        }

        if (best_vc == -1)
            return -1;
        if (best_credits - first_credits < 2)
            best_vc = first_vc;
        if (is_vc_idle(best_vc, curTick()))
            outVcState[best_vc].setState(ACTIVE_, curTick());
        m_next_vc[vnet] = (best_vc - vc_base + 1) % m_vc_per_vnet;
        if (best_vc != first_vc)
            m_router->get_net_ptr()->incrementBalancedVcChoices();
        return best_vc;
    }

    for (int vc = vc_begin; vc < vc_end; vc++) {
        int credits = outVcState[vc].get_credit_count();
        if (protect_critical)
            credits -= outVcState[vc].get_critical_count();
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
        increment_credit(t_credit->get_vc(), t_credit->is_critical());

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
