from m5.objects import *
from m5.params import *

from common import FileSystemConfig
from topologies.BaseTopology import SimpleTopology


class Ring(SimpleTopology):
    description = "Ring"

    def __init__(self, controllers):
        self.nodes = controllers

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        router_count = options.num_cpus
        link_latency = options.link_latency
        router_latency = options.router_latency

        assert router_count == 16, "Ring requires --num-cpus=16"

        routers = [
            Router(router_id=i, latency=router_latency)
            for i in range(router_count)
        ]
        network.routers = routers

        link_id = 0
        ext_links = []

        remainder = len(self.nodes) % router_count
        regular_count = len(self.nodes) - remainder

        for index, controller in enumerate(self.nodes[:regular_count]):
            router_id = index % router_count

            ext_links.append(
                ExtLink(
                    link_id=link_id,
                    ext_node=controller,
                    int_node=routers[router_id],
                    latency=link_latency,
                )
            )
            link_id += 1

        # Any remaining controllers should be DMA controllers.
        for controller in self.nodes[regular_count:]:
            assert controller.type == "DMA_Controller"

            ext_links.append(
                ExtLink(
                    link_id=link_id,
                    ext_node=controller,
                    int_node=routers[0],
                    latency=link_latency,
                )
            )
            link_id += 1

        network.ext_links = ext_links

        int_links = []

        for current in range(router_count):
            neighbor = (current + 1) % router_count

            directions = [
                (
                    current,
                    neighbor,
                    "Clockwise",
                    "CounterClockwise",
                ),
                (
                    neighbor,
                    current,
                    "CounterClockwise",
                    "Clockwise",
                ),
            ]

            for source, destination, outport, inport in directions:
                int_links.append(
                    IntLink(
                        link_id=link_id,
                        src_node=routers[source],
                        dst_node=routers[destination],
                        src_outport=outport,
                        dst_inport=inport,
                        latency=link_latency,
                        weight=1,
                    )
                )
                link_id += 1

        network.int_links = int_links

    def registerTopology(self, options):
        memory_per_node = (
            MemorySize(options.mem_size) // options.num_cpus
        )

        for node_id in range(options.num_cpus):
            FileSystemConfig.register_node(
                [node_id],
                memory_per_node,
                node_id,
            )