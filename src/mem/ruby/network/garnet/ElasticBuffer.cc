/*
 * Copyright (c) 2026
 */

#include "mem/ruby/network/garnet/ElasticBuffer.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

ElasticBuffer::ElasticBuffer(int vcs, int capacity)
  : m_size(0), m_slots(capacity, nullptr), m_vc_slots(vcs)
{
    for (int slot = 0; slot < capacity; slot++)
        m_free_slots.push_back(slot);
}

bool
ElasticBuffer::isReady(int vc, Tick curTime) const
{
    if (m_vc_slots[vc].empty())
        return false;
    return m_slots[m_vc_slots[vc].front()]->get_time() <= curTime;
}

bool
ElasticBuffer::isEmpty(int vc) const
{
    return m_vc_slots[vc].empty();
}

bool
ElasticBuffer::isFull() const
{
    return m_free_slots.empty();
}

flit *
ElasticBuffer::peekTopFlit(int vc) const
{
    return m_slots[m_vc_slots[vc].front()];
}

flit *
ElasticBuffer::getTopFlit(int vc)
{
    int slot = m_vc_slots[vc].front();
    m_vc_slots[vc].pop_front();
    flit *t_flit = m_slots[slot];
    m_slots[slot] = nullptr;
    m_free_slots.push_back(slot);
    m_size--;
    return t_flit;
}

void
ElasticBuffer::insert(int vc, flit *t_flit)
{
    int slot = m_free_slots.front();
    m_free_slots.pop_front();
    m_slots[slot] = t_flit;
    m_vc_slots[vc].push_back(slot);
    m_size++;
}

bool
ElasticBuffer::functionalRead(Packet *pkt, WriteMask &mask)
{
    bool read = false;
    for (flit *t_flit : m_slots) {
        if (t_flit && t_flit->functionalRead(pkt, mask))
            read = true;
    }
    return read;
}

uint32_t
ElasticBuffer::functionalWrite(Packet *pkt)
{
    uint32_t writes = 0;
    for (flit *t_flit : m_slots) {
        if (t_flit && t_flit->functionalWrite(pkt))
            writes++;
    }
    return writes;
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
