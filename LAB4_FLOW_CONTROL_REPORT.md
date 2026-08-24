# Lab 4：Garnet 流量控制与网络拓扑实验

本项目基于 gem5 Garnet 网络模型，实现并比较了三种流量控制方法：普通
Wormhole、Bubble Flow Control 和 Escape VC。实验保留 Lab 3 的 16 节点
Ring，并新增 4×4 2D Mesh 和 4×4 2D Torus。

## 1. 完成内容

### 1.1 流量控制方法

- **Wormhole baseline**：只要下游 VC 至少有 1 个 credit，就允许 flit 前进。
- **Bubble**：普通转发至少需要 1 个 credit；本地注入或形成新通道依赖的转向
  至少需要 2 个 credit。保留一个空闲 buffer slot，可以破坏循环等待。
- **Escape VC**：VC 成对分为 regular VC 和 escape VC。数据包跨越 Ring 或
  Torus 的 dateline 时，从 regular VC 切换到对应 escape VC，以建立无环的
  通道获取顺序。

三种方法都支持可配置的 VC 数量和深度。本实验统一使用：

- 4 VC / vnet
- 每个 VC 深度 8
- 数据包大小 5 flit（`--inj-vnet=2`）

### 1.2 网络拓扑

| 拓扑 | 规模 | 有向内部链路 | 路由 | 特点 |
|---|---:|---:|---|---|
| Ring | 16 节点 | 32 | 最短顺/逆时针 | 平均路径长，存在循环依赖 |
| Mesh2D | 4×4 | 48 | 确定性 XY | XY 通道依赖无环 |
| Torus2D | 4×4 | 64 | 最短环绕、先 X 后 Y | 路径短，但环绕链路产生循环依赖 |

原来的 `Ring` 拓扑被完整保留。运行时使用
`--topology=Ring|Mesh2D|Torus2D` 选择拓扑。

## 2. 主要代码

| 文件 | 作用 |
|---|---|
| `configs/topologies/Ring.py` | 原有 16 节点 Ring |
| `configs/topologies/Mesh2D.py` | 新增的 2D Mesh 入口 |
| `configs/topologies/Torus2D.py` | 2D Torus 节点、链路和环绕链路 |
| `configs/network/Network.py` | 命令行参数、模式检查和网络参数配置 |
| `src/mem/ruby/network/garnet/RoutingUnit.cc` | Ring、Mesh、Torus 的自定义路由 |
| `src/mem/ruby/network/garnet/SwitchAllocator.cc` | Bubble 和 Escape VC 分配规则 |
| `src/mem/ruby/network/garnet/NetworkInterface.cc` | 注入 VC 选择和死锁检测 |
| `src/mem/ruby/network/garnet/GarnetNetwork.*` | 拓扑、统计量和全局配置 |

## 3. 实验设置

所有性能测试采用相同资源，保证比较公平：

| 参数 | 值 |
|---|---|
| 节点数 | 16 |
| Traffic | uniform random |
| Packet | 5 flit |
| VC | 4 / vnet |
| VC 深度 | 8 |
| 路由算法 | Custom（`--routing-algorithm=2`） |
| 扫描时长 | 50,000 cycle |
| 长时死锁验证 | 100,000 cycle |

报告中的吞吐量是整个网络每 cycle 接收的 packet 数。命令行注入率则是每个节点
每 cycle 的注入概率，因此总 offered load 为 `16 × injection rate`。

## 4. 实验结果

### 4.1 Ring

| 注入率 | 方法 | 吞吐量 | 平均 packet 延迟 | 状态 |
|---:|---|---:|---:|---|
| 0.10 | Wormhole | 0.79422 | 27.86 | 正常 |
| 0.10 | Bubble | 0.79422 | 27.76 | 正常 |
| 0.10 | Escape VC | 0.59698 | 1709.13 | 已拥塞 |
| 0.14 | Wormhole | 0.02638 | 57.26* | 死锁 |
| 0.14 | Bubble | 1.10944 | 86.38 | 正常 |
| 0.14 | Escape VC | 0.50580 | 5141.16 | 正常但拥塞严重 |
| 0.25 | Bubble | 1.15432 | 10565.94 | 饱和但仍有全局进展 |
| 0.25 | Escape VC | 0.48300 | 11700.88 | 饱和但仍有全局进展 |

Ring Wormhole 的 100,000-cycle 长时测试在约 51,259 cycle 触发 gem5 的
`Possible network deadlock` 检测。

### 4.2 Mesh2D

| 注入率 | 方法 | 吞吐量 | 平均 packet 延迟 |
|---:|---|---:|---:|
| 0.10 | Wormhole | 0.79444 | 17.49 |
| 0.10 | Bubble | 0.79444 | 17.47 |
| 0.10 | Escape VC | 0.79444 | 17.60 |
| 0.20 | Wormhole | 1.58928 | 25.49 |
| 0.20 | Bubble | 1.58934 | 25.47 |
| 0.20 | Escape VC | 1.58928 | 25.29 |
| 0.30 | Wormhole | 2.24252 | 1421.62 |
| 0.30 | Bubble | 2.21394 | 1511.37 |
| 0.30 | Escape VC | 2.21508 | 1748.60 |

Mesh2D 的饱和吞吐量约为 2.23–2.26 packet/cycle。确定性 XY 路由的通道
依赖本身无环，因此普通 Wormhole 没有出现网络死锁。Bubble 和 Escape VC 在
这里主要作为额外机制开销的对照。

### 4.3 Torus2D

| 注入率 | 方法 | 吞吐量 | 平均 packet 延迟 | 状态 |
|---:|---|---:|---:|---|
| 0.10 | Wormhole | 0.79446 | 16.10 | 正常 |
| 0.10 | Bubble | 0.79446 | 16.09 | 正常 |
| 0.10 | Escape VC | 0.79444 | 16.77 | 正常 |
| 0.20 | Wormhole | 1.58948 | 22.31 | 正常 |
| 0.20 | Bubble | 1.58950 | 22.23 | 正常 |
| 0.20 | Escape VC | 1.58928 | 35.91 | 正常 |
| 0.30 | Wormhole | 0.20672 | 67.07* | 死锁 |
| 0.30 | Bubble | 2.37808 | 88.83 | 正常 |
| 0.30 | Escape VC | 1.79522 | 6073.65 | 正常但已饱和 |
| 1.00 | Bubble | 2.46336 | 17341.83 | 饱和但仍有全局进展 |
| 1.00 | Escape VC | 1.80024 | 19384.56 | 饱和但仍有全局进展 |

Torus2D Wormhole 的长时测试在约 54,293 cycle 触发网络死锁。Bubble 和
Escape VC 均完成长时测试。

> `*`：死锁场景的平均延迟只统计死锁前已经到达的少量 packet，不能作为正常
> 延迟使用。

## 5. 结果分析

1. **Mesh2D 不需要额外机制来保证 XY 路由无死锁。** 三种方法在低负载时性能
   几乎相同，饱和吞吐量也接近。
2. **Ring 和 Torus2D 的普通 Wormhole 会死锁。** 两种拓扑都存在环，buffer
   被循环占用后可能没有 packet 能继续前进。
3. **Bubble 在本实验中性能最好。** 它只保留一个空 buffer slot，不需要长期
   独占一组逃逸 VC。Torus2D Bubble 的饱和吞吐量约为 2.46 packet/cycle。
4. **Escape VC 可以消除 dateline 产生的循环等待，但性能较低。** escape VC
   的使用限制使有效并行度下降，Ring 上尤为明显。
5. **Torus2D 的低负载延迟最低。** 它的平均路径约 2 hop，比 Mesh2D 的约
   2.5 hop 和 Ring 的约 4 hop 更短；同时其链路数也最多。

## 6. 编译

在 gem5 仓库根目录执行：

```bash
scons build/NULL/gem5.opt -j4
```

本机实验使用 Python 3.11。如果 SCons 没有自动找到对应 Python，可以按本机
路径设置 `PYTHON_CONFIG`。

## 7. 复现实验

以下示例运行 Torus2D Bubble，注入率为 0.30：

```bash
build/NULL/gem5.opt -d m5out-torus-bubble-0p30 \
  configs/example/garnet_synth_traffic.py \
  --network=garnet \
  --num-cpus=16 \
  --num-dirs=16 \
  --topology=Torus2D \
  --mesh-rows=4 \
  --sim-cycles=50000 \
  --injectionrate=0.30 \
  --synthetic=uniform_random \
  --inj-vnet=2 \
  --vcs-per-vnet=4 \
  --vc-depth=8 \
  --routing-algorithm=2 \
  --bubble
```

切换流量控制方法：

```text
Wormhole baseline: --wormhole
Bubble:            --bubble
Escape VC:         --escape-vc
```

切换拓扑：

```text
Ring:     --topology=Ring
Mesh2D:   --topology=Mesh2D  --mesh-rows=4
Torus2D:  --topology=Torus2D --mesh-rows=4
```

实验完成后，从 `m5out-*/stats.txt` 中读取：

```text
system.ruby.network.packets_received::total
system.ruby.network.average_packet_latency
system.ruby.network.average_hops
system.ruby.network.escape_vc_transitions
```

## 8. Git 提交结构

核心实现按功能保留为独立提交，便于查看和讲解：

```text
1f841627b3  Implement bubble flow control for Ring
e98b2ad73a  Support multi-flit wormhole traffic
d2b4eae907  Implement dateline escape VC for Ring
d806e4d90c  Scale Ring escape VC pairs
31d6c0946c  Support configurable multi-VC Bubble flow
df18224c7f  Add 2D Mesh and Torus flow-control support
```

基线提交 `lab3-complete` 保留在历史中，可以使用 `git diff lab3-complete..lab4-work`
查看 Lab 4 的全部代码改动。
