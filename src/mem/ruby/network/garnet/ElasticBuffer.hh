/*
 * Copyright (c) 2026
 *
 * Elastic aggregate buffer used by Elastic Token Flow Control.  Payload
 * slots are shared while each VC keeps an ordered descriptor queue.
 */

#ifndef __MEM_RUBY_NETWORK_GARNET_0_ELASTICBUFFER_HH__
#define __MEM_RUBY_NETWORK_GARNET_0_ELASTICBUFFER_HH__

#include <deque>
#include <iostream>
#include <vector>

#include "mem/ruby/network/garnet/CommonTypes.hh"
#include "mem/ruby/network/garnet/flit.hh"

namespace gem5
{

namespace ruby
{

namespace garnet
{

class ElasticBuffer
{
  public:
    ElasticBuffer(int vcs, int capacity);

    bool isReady(int vc, Tick curTime) const;
    bool isEmpty(int vc) const;
    bool isFull() const;
    int getSize() const { return m_size; }

    flit *peekTopFlit(int vc) const;
    flit *getTopFlit(int vc);
    void insert(int vc, flit *t_flit);

    bool functionalRead(Packet *pkt, WriteMask &mask);
    uint32_t functionalWrite(Packet *pkt);

  private:
    int m_size;
    std::vector<flit *> m_slots;
    std::deque<int> m_free_slots;
    std::vector<std::deque<int>> m_vc_slots;
};

} // namespace garnet
} // namespace ruby
} // namespace gem5

#endif // __MEM_RUBY_NETWORK_GARNET_0_ELASTICBUFFER_HH__
