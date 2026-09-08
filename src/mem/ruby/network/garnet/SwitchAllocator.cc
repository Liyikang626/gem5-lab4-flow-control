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


#include "mem/ruby/network/garnet/SwitchAllocator.hh"

#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/GarnetNetwork.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/OutputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

SwitchAllocator::SwitchAllocator(Router *router)
    : Consumer(router)
{
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_vc_per_vnet = m_router->get_vc_per_vnet();

    m_input_arbiter_activity = 0;
    m_output_arbiter_activity = 0;
}

void
SwitchAllocator::init()
{
    m_num_inports = m_router->get_num_inports();
    m_num_outports = m_router->get_num_outports();
    m_round_robin_inport.resize(m_num_outports);
    m_round_robin_invc.resize(m_num_inports);
    m_port_requests.resize(m_num_inports);
    m_vc_winners.resize(m_num_inports);
    m_elastic_reservations.resize(m_num_inports, false);
    m_elastic_vnets.resize(m_num_inports, -1);

    for (int i = 0; i < m_num_inports; i++) {
        m_round_robin_invc[i] = 0;
        m_port_requests[i] = -1;
        m_vc_winners[i] = -1;
        m_elastic_reservations[i] = false;
        m_elastic_vnets[i] = -1;
    }

    for (int i = 0; i < m_num_outports; i++) {
        m_round_robin_inport[i] = 0;
    }
}

/*
 * The wakeup function of the SwitchAllocator performs a 2-stage
 * seperable switch allocation. At the end of the 2nd stage, a free
 * output VC is assigned to the winning flits of each output port.
 * There is no separate VCAllocator stage like the one in garnet1.0.
 * At the end of this function, the router is rescheduled to wakeup
 * next cycle for peforming SA for any flits ready next cycle.
 */

void
SwitchAllocator::wakeup()
{
    arbitrate_inports(); // First stage of allocation
    arbitrate_outports(); // Second stage of allocation

    clear_request_vector();
    check_for_wakeup();
}

/*
 * SA-I (or SA-i) loops through all input VCs at every input port,
 * and selects one in a round robin manner.
 *    - For HEAD/HEAD_TAIL flits only selects an input VC whose output port
 *     has at least one free output VC.
 *    - For BODY/TAIL flits, only selects an input VC that has credits
 *      in its output VC.
 * Places a request for the output port from this input VC.
 */

void
SwitchAllocator::arbitrate_inports()
{
    // Select a VC from each input in a round robin manner
    // Independent arbiter at each input port
    for (int inport = 0; inport < m_num_inports; inport++) {
        int invc = m_round_robin_invc[inport];

        for (int invc_iter = 0; invc_iter < m_num_vcs; invc_iter++) {
            auto input_unit = m_router->getInputUnit(inport);

            if (input_unit->need_stage(invc, SA_, curTick())) {
                // This flit is in SA stage

                bool wormhole =
                    m_router->get_net_ptr()->isWormholeEnabled();
                if (wormhole) {
                    flit *t_flit = input_unit->peekTopFlit(invc);
                    int next_outport = m_router->route_compute(
                        t_flit->get_route(), inport,
                        input_unit->get_direction());
                    input_unit->grant_outport(invc, next_outport);
                    input_unit->grant_outvc(invc, -1);
                }

                int outport = input_unit->get_outport(invc);
                int outvc = input_unit->get_outvc(invc);

                // check if the flit in this InputVC is allowed to be sent
                // send_allowed conditions described in that function.
                bool make_request =
                    send_allowed(inport, invc, outport, outvc);

                if (make_request) {
                    m_input_arbiter_activity++;
                    m_port_requests[inport] = outport;
                    m_vc_winners[inport] = invc;

                    break; // got one vc winner for this port
                }
            }

            invc++;
            if (invc >= m_num_vcs)
                invc = 0;
        }
    }
}

/*
 * SA-II (or SA-o) loops through all output ports,
 * and selects one input VC (that placed a request during SA-I)
 * as the winner for this output port in a round robin manner.
 *      - For HEAD/HEAD_TAIL flits, performs simplified outvc allocation.
 *        (i.e., select a free VC from the output port).
 *      - For BODY/TAIL flits, decrement a credit in the output vc.
 * The winning flit is read out from the input VC and sent to the
 * CrossbarSwitch.
 * An increment_credit signal is sent from the InputUnit
 * to the upstream router. For HEAD_TAIL/TAIL flits, is_free_signal in the
 * credit is set to true.
 */

void
SwitchAllocator::arbitrate_outports()
{
    // Now there are a set of input vc requests for output vcs.
    // Again do round robin arbitration on these requests
    // Independent arbiter at each output port
    for (int outport = 0; outport < m_num_outports; outport++) {
        int inport = m_round_robin_inport[outport];
        auto output_unit = m_router->getOutputUnit(outport);

        // Keep a pressured directional ring moving before admitting new
        // traffic.  The original Ring rule is unchanged; Torus applies the
        // same ordering only to the row or column ring under pressure.
        if (m_router->get_net_ptr()->isElasticTokenEnabled() &&
            output_unit->get_direction() != "Local" &&
            (m_router->get_net_ptr()->getLabTopology() == "Ring" ||
             m_router->get_net_ptr()->getLabTopology() == "Torus2D")) {
            int candidate = inport;
            for (int i = 0; i < m_num_inports; i++) {
                if (m_port_requests[candidate] == outport) {
                    int invc = m_vc_winners[candidate];
                    int vnet = get_vnet(invc);
                    bool pressured =
                        m_router->get_net_ptr()->getLabTopology() == "Ring" ?
                        output_unit->get_elastic_credits(vnet) <=
                            m_vc_per_vnet :
                        m_router->get_net_ptr()->isElasticRingPressured(
                            m_router->get_id(), output_unit->get_direction(),
                            vnet);
                    if (pressured && !enters_ring(candidate, outport)) {
                        inport = candidate;
                        break;
                    }
                }
                candidate++;
                if (candidate == m_num_inports)
                    candidate = 0;
            }
        }

        for (int inport_iter = 0; inport_iter < m_num_inports;
                 inport_iter++) {

            // inport has a request this cycle for outport
            if (m_port_requests[inport] == outport) {
                auto input_unit = m_router->getInputUnit(inport);

                // grant this outport to this inport
                int invc = m_vc_winners[inport];

                auto network = m_router->get_net_ptr();
                if (m_elastic_reservations[inport]) {
                    network->commitElasticEntry(
                        m_router->get_id(), output_unit->get_direction(),
                        m_elastic_vnets[inport]);
                    m_elastic_reservations[inport] = false;
                }

                int outvc = input_unit->get_outvc(invc);
                if (outvc == -1) {
                    // VC Allocation - select any free VC from outport
                    outvc = vc_allocate(outport, inport, invc);
                }

                // remove flit from Input VC
                flit *t_flit = input_unit->getTopFlit(invc);

                DPRINTF(RubyNetwork, "SwitchAllocator at Router %d "
                                     "granted outvc %d at outport %d "
                                     "to invc %d at inport %d to flit %s at "
                                     "cycle: %lld\n",
                        m_router->get_id(), outvc,
                        m_router->getPortDirectionName(
                            output_unit->get_direction()),
                        invc,
                        m_router->getPortDirectionName(
                            input_unit->get_direction()),
                            *t_flit,
                        m_router->curCycle());


                // Update outport field in the flit since this is
                // used by CrossbarSwitch code to send it out of
                // correct outport.
                // Note: post route compute in InputUnit,
                // outport is updated in VC, but not in flit
                t_flit->set_outport(outport);

                // set outvc (i.e., invc for next hop) in flit
                // (This was updated in VC by vc_allocate, but not in flit)
                t_flit->set_vc(outvc);

                // Return the marker carried by the freed input slot, then
                // carry the marker consumed from the next output VC.
                output_unit->decrement_credit(outvc);

                // flit ready for Switch Traversal
                t_flit->advance_stage(ST_, curTick());
                m_router->grant_switch(inport, t_flit);
                m_output_arbiter_activity++;

                if ((t_flit->get_type() == TAIL_) ||
                    t_flit->get_type() == HEAD_TAIL_) {

                    bool wormhole =
                        m_router->get_net_ptr()->isWormholeEnabled();
                    if (wormhole) {
                        input_unit->increment_credit(
                            invc, false, curTick());
                    } else {
                        // This Input VC should now be empty
                        assert(!(input_unit->isReady(invc, curTick())));

                        // Free this VC
                        input_unit->set_vc_idle(invc, curTick());

                        // Send a credit back
                        // along with the information that this VC is now idle
                        input_unit->increment_credit(
                            invc, true, curTick());
                    }
                } else {
                    // Send a credit back
                    // but do not indicate that the VC is idle
                    input_unit->increment_credit(
                        invc, false, curTick());
                }


                // remove this request
                m_port_requests[inport] = -1;

                // Update Round Robin pointer
                m_round_robin_inport[outport] = inport + 1;
                if (m_round_robin_inport[outport] >= m_num_inports)
                    m_round_robin_inport[outport] = 0;

                // Update Round Robin pointer to the next VC
                // We do it here to keep it fair.
                // Only the VC which got switch traversal
                // is updated.
                m_round_robin_invc[inport] = invc + 1;
                if (m_round_robin_invc[inport] >= m_num_vcs)
                    m_round_robin_invc[inport] = 0;


                break; // got a input winner for this outport
            }

            inport++;
            if (inport >= m_num_inports)
                inport = 0;
        }
    }
}

/*
 * A flit can be sent only if
 * (1) there is at least one free output VC at the
 *     output port (for HEAD/HEAD_TAIL),
 *  or
 * (2) if there is at least one credit (i.e., buffer slot)
 *     within the VC for BODY/TAIL flits of multi-flit packets.
 * and
 * (3) pt-to-pt ordering is not violated in ordered vnets, i.e.,
 *     there should be no other flit in this input port
 *     within an ordered vnet
 *     that arrived before this flit and is requesting the same output port.
 */

bool
SwitchAllocator::send_allowed(int inport, int invc, int outport, int outvc)
{
    // Check if outvc needed
    // Check if credit needed (for multi-flit packet)
    // Check if ordering violated (in ordered vnet)

    int vnet = get_vnet(invc);
    bool has_outvc = (outvc != -1);
    bool has_credit = false;
    int min_credits = required_credits(inport, outport);
    int vc_offset = required_vc_offset(inport, invc, outport);
    auto network = m_router->get_net_ptr();
    bool elastic_entry = network->isElasticTokenEnabled() &&
                         enters_ring(inport, outport);
    bool ring_token = elastic_entry &&
                      network->getLabTopology() == "Ring";
    if (elastic_entry && !ring_token)
        min_credits = 2;

    auto output_unit = m_router->getOutputUnit(outport);
    if (!has_outvc) {

        // needs outvc
        // this is only true for HEAD and HEAD_TAIL flits.

        if (output_unit->has_free_vc(
                vnet, min_credits, vc_offset)) {

            has_outvc = true;

            // each VC has at least one buffer,
            // so no need for additional credit check
            has_credit = true;
        }
    } else {
        has_credit = output_unit->has_credit(outvc, min_credits);
    }

    // cannot send if no outvc or no credit.
    if (!has_outvc || !has_credit) {
        return false;
    }


    // protocol ordering check
    if ((m_router->get_net_ptr())->isVNetOrdered(vnet)) {
        auto input_unit = m_router->getInputUnit(inport);

        // enqueue time of this flit
        Tick t_enqueue_time = input_unit->get_enqueue_time(invc);

        // check if any other flit is ready for SA and for same output port
        // and was enqueued before this flit
        int vc_base = vnet*m_vc_per_vnet;
        for (int vc_offset = 0; vc_offset < m_vc_per_vnet; vc_offset++) {
            int temp_vc = vc_base + vc_offset;
            if (input_unit->need_stage(temp_vc, SA_, curTick()) &&
               (input_unit->get_outport(temp_vc) == outport) &&
               (input_unit->get_enqueue_time(temp_vc) < t_enqueue_time)) {
                return false;
            }
        }
    }

    if (ring_token) {
        if (!network->reserveElasticEntry(
                m_router->get_id(), output_unit->get_direction(), vnet))
            return false;
        m_elastic_reservations[inport] = true;
        m_elastic_vnets[inport] = vnet;
    }

    return true;
}

// Assign a free VC to the winner of the output port.
int
SwitchAllocator::vc_allocate(int outport, int inport, int invc)
{
    // Select a free VC from the output port
    int vnet = get_vnet(invc);
    int min_credits = required_credits(inport, outport);
    int vc_offset = required_vc_offset(inport, invc, outport);
    auto output_unit = m_router->getOutputUnit(outport);
    int outvc = output_unit->select_free_vc(
        vnet, min_credits, vc_offset);

    // has to get a valid VC since it checked before performing SA
    assert(outvc != -1);
    m_router->getInputUnit(inport)->grant_outvc(invc, outvc);
    if (m_router->get_net_ptr()->isEscapeVcEnabled() &&
        crosses_dateline(outport)) {
        m_router->get_net_ptr()->incrementEscapeVcTransitions();
    }
    return outvc;
}

// Cyclic dimensions use paired regular/escape VCs. A Ring packet switches
// once at its dateline. A Torus packet may switch once in X, return to the
// paired regular VC on the dimension-order X->Y turn, and switch again at
// the Y dateline. A Mesh needs no dateline restriction.
int
SwitchAllocator::required_vc_offset(int inport, int invc, int outport)
{
    if (!m_router->get_net_ptr()->isEscapeVcEnabled())
        return -1;

    const std::string& topology =
        m_router->get_net_ptr()->getLabTopology();
    if (topology == "Mesh2D" || topology == "Mesh_XY")
        return -1;

    assert(m_vc_per_vnet >= 2 && m_vc_per_vnet % 2 == 0);
    int in_vc_offset = invc % m_vc_per_vnet;
    int first_escape_vc_offset = m_vc_per_vnet / 2;
    int regular_vc_offset = in_vc_offset % first_escape_vc_offset;
    PortDirection in_dir =
        m_router->getInputUnit(inport)->get_direction();
    PortDirection out_dir =
        m_router->getOutputUnit(outport)->get_direction();

    if (topology == "Ring") {
        if (in_vc_offset >= first_escape_vc_offset)
            return in_vc_offset;
        return crosses_dateline(outport) ?
            regular_vc_offset + first_escape_vc_offset : in_vc_offset;
    }

    assert(topology == "Torus2D");
    bool input_is_x = in_dir == "East" || in_dir == "West";
    bool output_is_y = out_dir == "North" || out_dir == "South";
    bool turns_to_y = input_is_x && output_is_y;
    int next_vc_offset = turns_to_y ? regular_vc_offset : in_vc_offset;

    return crosses_dateline(outport) ?
        regular_vc_offset + first_escape_vc_offset : next_vc_offset;
}

bool
SwitchAllocator::crosses_dateline(int outport)
{
    const std::string& topology =
        m_router->get_net_ptr()->getLabTopology();
    PortDirection out_dir =
        m_router->getOutputUnit(outport)->get_direction();
    int router_id = m_router->get_id();

    if (topology == "Ring") {
        return (router_id == 15 && out_dir == "Clockwise") ||
               (router_id == 0 && out_dir == "CounterClockwise");
    }
    if (topology != "Torus2D")
        return false;

    int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    int x = router_id % num_cols;
    int y = router_id / num_cols;
    return (x == num_cols - 1 && out_dir == "East") ||
           (x == 0 && out_dir == "West") ||
           (y == num_rows - 1 && out_dir == "North") ||
           (y == 0 && out_dir == "South");
}

// Injection into a cyclic channel reserves an additional downstream slot.
// On a Torus, an X->Y turn is also injection into a new cyclic dimension.
// Straight transit traffic still needs only one credit. Applying the same
// rule to a Mesh is safe and exposes its injection-overhead cost.
int
SwitchAllocator::required_credits(int inport, int outport)
{
    auto network = m_router->get_net_ptr();
    if (!network->isBubbleEnabled())
        return 1;

    auto input_unit = m_router->getInputUnit(inport);
    auto output_unit = m_router->getOutputUnit(outport);
    PortDirection in_dir = input_unit->get_direction();
    PortDirection out_dir = output_unit->get_direction();
    if (out_dir == "Local")
        return 1;


    bool straight_transit =
        (out_dir == "Clockwise" && in_dir == "CounterClockwise") ||
        (out_dir == "CounterClockwise" && in_dir == "Clockwise") ||
        (out_dir == "East" && in_dir == "West") ||
        (out_dir == "West" && in_dir == "East") ||
        (out_dir == "North" && in_dir == "South") ||
        (out_dir == "South" && in_dir == "North");
    return straight_transit ? 1 : 2;
}

// Injection and an X-to-Y turn enter a new cyclic directional ring.
bool
SwitchAllocator::enters_ring(int inport, int outport)
{
    PortDirection in_dir =
        m_router->getInputUnit(inport)->get_direction();
    PortDirection out_dir =
        m_router->getOutputUnit(outport)->get_direction();
    if (out_dir == "Local")
        return false;
    if (in_dir == "Local")
        return true;

    if (m_router->get_net_ptr()->getLabTopology() != "Torus2D")
        return false;
    bool input_is_x = in_dir == "East" || in_dir == "West";
    bool output_is_y = out_dir == "North" || out_dir == "South";
    return input_is_x && output_is_y;
}

// Wakeup the router next cycle to perform SA again
// if there are flits ready.
void
SwitchAllocator::check_for_wakeup()
{
    Tick nextCycle = m_router->clockEdge(Cycles(1));

    if (m_router->alreadyScheduled(nextCycle)) {
        return;
    }

    for (int i = 0; i < m_num_inports; i++) {
        for (int j = 0; j < m_num_vcs; j++) {
            if (m_router->getInputUnit(i)->need_stage(j, SA_, nextCycle)) {
                m_router->schedule_wakeup(Cycles(1));
                return;
            }
        }
    }
}

int
SwitchAllocator::get_vnet(int invc)
{
    int vnet = invc/m_vc_per_vnet;
    assert(vnet < m_router->get_num_vnets());
    return vnet;
}


// Clear the request vector within the allocator at end of SA-II.
// Was populated by SA-I.
void
SwitchAllocator::clear_request_vector()
{
    auto network = m_router->get_net_ptr();
    for (int inport = 0; inport < m_num_inports; inport++) {
        if (m_elastic_reservations[inport]) {
            int outport = m_port_requests[inport];
            if (outport >= 0)
                network->releaseElasticEntry(
                    m_router->get_id(),
                    m_router->getOutputUnit(outport)->get_direction(),
                    m_elastic_vnets[inport]);
            m_elastic_reservations[inport] = false;
        }
        m_elastic_vnets[inport] = -1;
    }
    std::fill(m_port_requests.begin(), m_port_requests.end(), -1);
}

void
SwitchAllocator::resetStats()
{
    m_input_arbiter_activity = 0;
    m_output_arbiter_activity = 0;
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
