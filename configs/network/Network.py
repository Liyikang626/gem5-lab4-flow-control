# Copyright (c) 2016 Georgia Institute of Technology
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import math
import m5
from m5.objects import *
from m5.defines import buildEnv
from m5.util import addToPath, fatal, warn


def define_options(parser):
    # By default, ruby uses the simple timing cpu
    parser.set_defaults(cpu_type="TimingSimpleCPU")

    parser.add_argument(
        "--topology",
        type=str,
        default="Crossbar",
        help="check configs/topologies for complete set",
    )
    parser.add_argument(
        "--mesh-rows",
        type=int,
        default=0,
        help="the number of rows in the mesh topology",
    )
    parser.add_argument(
        "--network",
        default="simple",
        choices=["simple", "garnet"],
        help="""'simple'|'garnet' (garnet2.0 will be deprecated.)""",
    )
    parser.add_argument(
        "--router-latency",
        action="store",
        type=int,
        default=1,
        help="""number of pipeline stages in the garnet router.
            Has to be >= 1.
            Can be over-ridden on a per router basis
            in the topology file.""",
    )
    parser.add_argument(
        "--link-latency",
        action="store",
        type=int,
        default=1,
        help="""latency of each link the simple/garnet networks.
        Has to be >= 1. Can be over-ridden on a per link basis
        in the topology file.""",
    )
    parser.add_argument(
        "--link-width-bits",
        action="store",
        type=int,
        default=128,
        help="width in bits for all links inside garnet.",
    )
    parser.add_argument(
        "--vcs-per-vnet",
        action="store",
        type=int,
        default=4,
        help="""number of virtual channels per virtual network
            inside garnet network.""",
    )
    parser.add_argument(
        "--vc-depth",
        action="store",
        type=int,
        default=None,
        help="buffer entries per VC for Wormhole, Bubble, or Escape VC",
    )
    parser.add_argument(
        "--wormhole",
        action="store_true",
        help="enable shared-buffer wormhole flow control",
    )
    parser.add_argument(
        "--bubble",
        action="store_true",
        help=(
            "enable bubble flow control for the 16-node Ring; "
            "local injection requires at least two credits"
        ),
    )
    parser.add_argument(
        "--elastic-token",
        action="store_true",
        help="enable elastic token flow control",
    )
    parser.add_argument(
        "--escape-vc",
        action="store_true",
        help=(
            "enable dateline escape flow control for the 16-node Ring; "
            "the upper half of the VCs are paired escape VCs"
        ),
    )
    parser.add_argument(
        "--routing-algorithm",
        action="store",
        type=int,
        default=0,
        help="""routing algorithm in network.
            0: weight-based table
            1: XY (for Mesh. see garnet/RoutingUnit.cc)
            2: Custom (see garnet/RoutingUnit.cc""",
    )
    parser.add_argument(
        "--network-fault-model",
        action="store_true",
        default=False,
        help="""enable network fault model:
            see src/mem/ruby/network/fault_model/""",
    )
    parser.add_argument(
        "--garnet-deadlock-threshold",
        action="store",
        type=int,
        default=50000,
        help="network-level deadlock threshold.",
    )
    parser.add_argument(
        "--simple-physical-channels",
        action="store_true",
        default=False,
        help="""SimpleNetwork links uses a separate physical
            channel for each virtual network""",
    )


def create_network(options, ruby):

    # Allow legacy users to use garnet through garnet2.0 option
    # until next gem5 release.
    if options.network == "garnet2.0":
        warn(
            "Usage of option 'garnet2.0' will be depracated. "
            "Please use 'garnet' for using the latest garnet "
            "version. Current version: 3.0"
        )
        options.network = "garnet"

    # Set the network classes based on the command line options
    if options.network == "garnet":
        NetworkClass = GarnetNetwork
        IntLinkClass = GarnetIntLink
        ExtLinkClass = GarnetExtLink
        RouterClass = GarnetRouter
        InterfaceClass = GarnetNetworkInterface

    else:
        NetworkClass = SimpleNetwork
        IntLinkClass = SimpleIntLink
        ExtLinkClass = SimpleExtLink
        RouterClass = Switch
        InterfaceClass = None

    # Instantiate the network object
    # so that the controllers can connect to it.
    network = NetworkClass(
        ruby_system=ruby,
        topology=options.topology,
        routers=[],
        ext_links=[],
        int_links=[],
        netifs=[],
    )

    return (network, IntLinkClass, ExtLinkClass, RouterClass, InterfaceClass)


def init_network(options, network, InterfaceClass):

    if options.network == "garnet":
        network.num_rows = options.mesh_rows
        network.vcs_per_vnet = options.vcs_per_vnet
        network.ni_flit_size = options.link_width_bits / 8
        network.routing_algorithm = options.routing_algorithm
        network.lab_topology = options.topology
        network.garnet_deadlock_threshold = options.garnet_deadlock_threshold
        if options.bubble:
            supported = ("Ring", "Mesh2D", "Mesh_XY", "Torus2D")
            if options.topology not in supported:
                fatal("--bubble requires Ring, Mesh2D, Mesh_XY, or Torus2D")

        if options.elastic_token:
            supported = ("Ring", "Mesh2D", "Mesh_XY", "Torus2D")
            if options.topology not in supported:
                fatal(
                    "--elastic-token requires Ring, Mesh2D, Mesh_XY, or Torus2D"
                )
            if (options.bubble or options.escape_vc or options.wormhole):
                fatal(
                    "--elastic-token cannot be combined with "
                    "Bubble/Escape VC/Wormhole"
                )

        if options.vc_depth is not None and options.vc_depth < 1:
            fatal("--vc-depth must be at least one")

        if options.escape_vc:
            supported = ("Ring", "Mesh2D", "Mesh_XY", "Torus2D")
            if options.topology not in supported:
                fatal("--escape-vc requires Ring, Mesh2D, Mesh_XY, or Torus2D")
            if options.vcs_per_vnet < 2:
                fatal("--escape-vc requires at least two VCs per vnet")
            if options.vcs_per_vnet % 2 != 0:
                fatal("--escape-vc requires an even number of VCs per vnet")
            if options.routing_algorithm != 2:
                fatal("--escape-vc requires --routing-algorithm=2")
            if options.bubble or options.wormhole:
                fatal("--escape-vc cannot be combined with Bubble/Wormhole")

        network.bubble = options.bubble
        network.elastic_token = options.elastic_token
        network.escape_vc = options.escape_vc
        network.wormhole = (
            options.wormhole or options.bubble or options.elastic_token
        )
        if options.escape_vc:
            # Each regular/escape VC has eight buffer entries. With two VCs,
            # this matches the 16-entry total buffer budget of Bubble.
            vc_depth = options.vc_depth or 8
        elif (options.wormhole or options.bubble or options.elastic_token):
            vc_depth = options.vc_depth or 16
        else:
            vc_depth = options.vc_depth

        if options.bubble and vc_depth < 2:
            fatal("Bubble flow control requires --vc-depth of at least two")
        if vc_depth is not None:
            network.buffers_per_ctrl_vc = vc_depth
            network.buffers_per_data_vc = vc_depth

        # Create Bridges and connect them to the corresponding links
        for intLink in network.int_links:
            intLink.src_net_bridge = NetworkBridge(
                link=intLink.network_link,
                vtype="OBJECT_LINK",
                width=intLink.src_node.width,
            )
            intLink.src_cred_bridge = NetworkBridge(
                link=intLink.credit_link,
                vtype="LINK_OBJECT",
                width=intLink.src_node.width,
            )
            intLink.dst_net_bridge = NetworkBridge(
                link=intLink.network_link,
                vtype="LINK_OBJECT",
                width=intLink.dst_node.width,
            )
            intLink.dst_cred_bridge = NetworkBridge(
                link=intLink.credit_link,
                vtype="OBJECT_LINK",
                width=intLink.dst_node.width,
            )

        for extLink in network.ext_links:
            ext_net_bridges = []
            ext_net_bridges.append(
                NetworkBridge(
                    link=extLink.network_links[0],
                    vtype="OBJECT_LINK",
                    width=extLink.width,
                )
            )
            ext_net_bridges.append(
                NetworkBridge(
                    link=extLink.network_links[1],
                    vtype="LINK_OBJECT",
                    width=extLink.width,
                )
            )
            extLink.ext_net_bridge = ext_net_bridges

            ext_credit_bridges = []
            ext_credit_bridges.append(
                NetworkBridge(
                    link=extLink.credit_links[0],
                    vtype="LINK_OBJECT",
                    width=extLink.width,
                )
            )
            ext_credit_bridges.append(
                NetworkBridge(
                    link=extLink.credit_links[1],
                    vtype="OBJECT_LINK",
                    width=extLink.width,
                )
            )
            extLink.ext_cred_bridge = ext_credit_bridges

            int_net_bridges = []
            int_net_bridges.append(
                NetworkBridge(
                    link=extLink.network_links[0],
                    vtype="LINK_OBJECT",
                    width=extLink.int_node.width,
                )
            )
            int_net_bridges.append(
                NetworkBridge(
                    link=extLink.network_links[1],
                    vtype="OBJECT_LINK",
                    width=extLink.int_node.width,
                )
            )
            extLink.int_net_bridge = int_net_bridges

            int_cred_bridges = []
            int_cred_bridges.append(
                NetworkBridge(
                    link=extLink.credit_links[0],
                    vtype="OBJECT_LINK",
                    width=extLink.int_node.width,
                )
            )
            int_cred_bridges.append(
                NetworkBridge(
                    link=extLink.credit_links[1],
                    vtype="LINK_OBJECT",
                    width=extLink.int_node.width,
                )
            )
            extLink.int_cred_bridge = int_cred_bridges

    if options.network == "simple":
        if options.simple_physical_channels:
            network.physical_vnets_channels = [1] * int(
                network.number_of_virtual_networks
            )
        network.setup_buffers()

    if InterfaceClass != None:
        netifs = [
            InterfaceClass(id=i) for (i, n) in enumerate(network.ext_links)
        ]
        network.netifs = netifs

    if options.network_fault_model:
        assert options.network == "garnet"
        network.enable_fault_model = True
        network.fault_model = FaultModel()
