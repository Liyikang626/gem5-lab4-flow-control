from topologies.Mesh_XY import Mesh_XY


class Torus2D(Mesh_XY):
    """2D torus built by adding wrap-around links to Mesh_XY."""

    description = "Torus2D"

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        num_rows = options.mesh_rows
        num_routers = options.num_cpus
        num_columns = num_routers // num_rows
        assert num_rows > 1 and num_columns > 1
        assert num_rows * num_columns == num_routers

        routers = [
            Router(router_id=i, latency=options.router_latency)
            for i in range(num_routers)
        ]
        network.routers = routers

        controllers_per_router, remainder = divmod(
            len(self.nodes), num_routers
        )
        network_nodes = self.nodes[: len(self.nodes) - remainder]
        remainder_nodes = self.nodes[len(self.nodes) - remainder :]

        link_id = 0
        ext_links = []
        for index, node in enumerate(network_nodes):
            level, router_id = divmod(index, num_routers)
            assert level < controllers_per_router
            ext_links.append(
                ExtLink(
                    link_id=link_id,
                    ext_node=node,
                    int_node=routers[router_id],
                    latency=options.link_latency,
                )
            )
            link_id += 1

        for node in remainder_nodes:
            assert node.type == "DMA_Controller"
            ext_links.append(
                ExtLink(
                    link_id=link_id,
                    ext_node=node,
                    int_node=routers[0],
                    latency=options.link_latency,
                )
            )
            link_id += 1
        network.ext_links = ext_links

        int_links = []
        for row in range(num_rows):
            for column in range(num_columns):
                current = column + row * num_columns
                east = (column + 1) % num_columns + row * num_columns
                west = (column - 1) % num_columns + row * num_columns
                int_links.append(
                    IntLink(
                        link_id=link_id,
                        src_node=routers[current],
                        dst_node=routers[east],
                        src_outport="East",
                        dst_inport="West",
                        latency=options.link_latency,
                        weight=1,
                    )
                )
                link_id += 1
                int_links.append(
                    IntLink(
                        link_id=link_id,
                        src_node=routers[current],
                        dst_node=routers[west],
                        src_outport="West",
                        dst_inport="East",
                        latency=options.link_latency,
                        weight=1,
                    )
                )
                link_id += 1

        for row in range(num_rows):
            for column in range(num_columns):
                current = column + row * num_columns
                north = column + ((row + 1) % num_rows) * num_columns
                south = column + ((row - 1) % num_rows) * num_columns
                int_links.append(
                    IntLink(
                        link_id=link_id,
                        src_node=routers[current],
                        dst_node=routers[north],
                        src_outport="North",
                        dst_inport="South",
                        latency=options.link_latency,
                        weight=2,
                    )
                )
                link_id += 1
                int_links.append(
                    IntLink(
                        link_id=link_id,
                        src_node=routers[current],
                        dst_node=routers[south],
                        src_outport="South",
                        dst_inport="North",
                        latency=options.link_latency,
                        weight=2,
                    )
                )
                link_id += 1

        network.int_links = int_links
