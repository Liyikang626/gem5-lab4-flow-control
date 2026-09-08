#!/usr/bin/env bash

set -u

repo=$(cd "$(dirname "$0")/../.." && pwd)
binary="$repo/build/NULL/gem5.opt"
output=${1:-"$repo/../results/etfc-paper"}
jobs=${2:-2}
tasks="$output/tasks.csv"

mkdir -p "$output/raw" "$output/rows"
printf 'suite,label,topology,controller,rate,cycles,traffic,inj_vnet,vc_depth,vcs,seed\n' > "$tasks"

add_case() {
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$@" >> "$tasks"
}

for topology in Ring Torus2D Mesh2D; do
    for cycles in 10000 50000 100000; do
        for rate in 0.05 0.10 0.20 0.30 0.40; do
            for controller in bubble etfc; do
                label="load_${topology}_${controller}_${cycles}_${rate/./p}"
                add_case load "$label" "$topology" "$controller" "$rate" \
                    "$cycles" uniform_random 2 16 4 5489
            done
        done
    done
done

for topology in Ring Torus2D Mesh2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for cycles in 10000 50000 100000; do
        for controller in wormhole escape; do
            depth=16
            [[ "$controller" == escape ]] && depth=8
            label="baseline_${topology}_${controller}_${cycles}"
            add_case baseline "$label" "$topology" "$controller" "$rate" \
                "$cycles" uniform_random 2 "$depth" 4 5489
        done
    done
done

for topology in Ring Torus2D Mesh2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for traffic in uniform_random tornado bit_complement bit_reverse \
                   bit_rotation neighbor shuffle transpose; do
        for controller in bubble etfc; do
            label="traffic_${topology}_${controller}_${traffic}"
            add_case traffic "$label" "$topology" "$controller" "$rate" \
                50000 "$traffic" 2 16 4 5489
        done
    done
done

for topology in Ring Torus2D Mesh2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for depth in 4 8 16 32; do
        for controller in bubble etfc; do
            label="depth_${topology}_${controller}_${depth}"
            add_case depth "$label" "$topology" "$controller" "$rate" \
                50000 uniform_random 2 "$depth" 4 5489
        done
    done
done

for topology in Ring Torus2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for vcs in 1 2 4 8; do
        for controller in bubble etfc; do
            label="vcs_${topology}_${controller}_${vcs}"
            add_case vcs "$label" "$topology" "$controller" "$rate" \
                50000 uniform_random 2 16 "$vcs" 5489
        done
    done
done

for topology in Ring Torus2D Mesh2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for inj_vnet in 0 2; do
        for controller in bubble etfc; do
            label="packet_${topology}_${controller}_${inj_vnet}"
            add_case packet "$label" "$topology" "$controller" "$rate" \
                50000 uniform_random "$inj_vnet" 16 4 5489
        done
    done
done

for topology in Ring Torus2D Mesh2D; do
    rate=0.30
    [[ "$topology" == Ring ]] && rate=0.20
    for seed in 11 23 47 5489; do
        for controller in bubble etfc; do
            label="seed_${topology}_${controller}_${seed}"
            add_case seed "$label" "$topology" "$controller" "$rate" \
                50000 uniform_random 2 16 4 "$seed"
        done
    done
done

run_case() {
    IFS=, read -r suite label topology controller rate cycles traffic \
        inj_vnet depth vcs seed <<< "$1"
    run_dir="$output/raw/$label"
    temp_dir=$(mktemp -d /tmp/etfc-paper-XXXX)
    row="$output/rows/$label.csv"
    mkdir -p "$run_dir"

    flags=()
    case "$controller" in
        wormhole) flags+=(--wormhole) ;;
        bubble) flags+=(--bubble) ;;
        escape) flags+=(--escape-vc) ;;
        etfc) flags+=(--elastic-token) ;;
    esac

    status=ok
    timeout 600 "$binary" -d "$temp_dir" \
        "$repo/configs/example/garnet_synth_traffic.py" \
        --network=garnet --topology="$topology" --mesh-rows=4 \
        --num-cpus=16 --num-dirs=16 --vcs-per-vnet="$vcs" \
        --vc-depth="$depth" --inj-vnet="$inj_vnet" \
        --routing-algorithm=2 --injectionrate="$rate" \
        --sim-cycles="$cycles" --synthetic="$traffic" --seed="$seed" \
        "${flags[@]}" > "$run_dir/run.log" 2>&1 || status=failed

    stats="$temp_dir/stats.txt"
    injected=NA; received=NA; packet_latency=NA; network_latency=NA
    queue_latency=NA; flit_latency=NA; hops=NA
    if [[ -f "$stats" ]]; then
        value() { awk -v key="$1" '$1==key{print $2}' "$stats" | tail -1; }
        injected=$(value system.ruby.network.packets_injected::total)
        received=$(value system.ruby.network.packets_received::total)
        packet_latency=$(value system.ruby.network.average_packet_latency)
        network_latency=$(value system.ruby.network.average_packet_network_latency)
        queue_latency=$(value system.ruby.network.average_packet_queueing_latency)
        flit_latency=$(value system.ruby.network.average_flit_latency)
        hops=$(value system.ruby.network.average_hops)
    fi
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$suite" "$label" "$topology" "$controller" "$rate" "$cycles" \
        "$traffic" "$inj_vnet" "$depth" "$vcs" "$seed" "$status" \
        "$injected" "$received" "$packet_latency" "$network_latency" \
        "$queue_latency" "$flit_latency" "$hops" > "$row"
    [[ -f "$stats" ]] && cp "$stats" "$run_dir/stats.txt"
    rm -rf "$temp_dir"
    echo "$label $status received=$received latency=$packet_latency"
}

export repo binary output
export -f run_case
tail -n +2 "$tasks" | xargs -d '\n' -P "$jobs" -I '{}' \
    bash -c 'run_case "$1"' _ '{}'

metrics="$output/metrics.csv"
printf 'suite,label,topology,controller,rate,cycles,traffic,inj_vnet,vc_depth,vcs,seed,status,packets_injected,packets_received,packet_latency,network_latency,queue_latency,flit_latency,hops\n' > "$metrics"
find "$output/rows" -name '*.csv' -print0 | sort -z | xargs -0 cat >> "$metrics"
echo "Wrote $metrics"
