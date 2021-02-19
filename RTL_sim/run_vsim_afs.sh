#!/usr/bin/env bash
# Usage ./run_vsim_afs.sh rate input.pkt nb_dsc_queues nb_pkt_queues
# Usage ./run_vsim_afs.sh rate nb_pkts pkt_size nb_dsc_queues nb_pkt_queues

# exit when error occurs
set -e
trap 'last_command=$current_command; current_command=$BASH_COMMAND' DEBUG
trap 'echo "\"${last_command}\" command exited with code $?."' EXIT

RATE=${1:-"100"}

NB_PKTS=$2
PKT_SIZE=$3
NB_DSC_QS=$4
NB_PKT_QS=$5
PKT_FILE="./input_gen/${NB_PKTS}_${PKT_SIZE}_${NB_DSC_QS}_${NB_PKT_QS}.pkt"
if [[ -f "$PKT_FILE" ]]; then
    echo "$PKT_FILE exists, using cache"
else
    echo "Generating $PKT_FILE"
    cd "./input_gen"
    ./generate_synthetic_trace.py ${NB_PKTS} ${PKT_SIZE} ${NB_DSC_QS} \
        ${NB_PKT_QS} "${NB_PKTS}_${PKT_SIZE}_${NB_DSC_QS}_${NB_PKT_QS}.pcap"
    ./run.sh "${NB_PKTS}_${PKT_SIZE}_${NB_DSC_QS}_${NB_PKT_QS}.pcap" \
        "${NB_PKTS}_${PKT_SIZE}_${NB_DSC_QS}_${NB_PKT_QS}.pkt"
    cd -
fi

echo "Using $NB_DSC_QS descriptor queues and $NB_PKT_QS packet queues."

pwd
PKT_FILE_NB_LINES=$(wc -l < $PKT_FILE)

rm -rf work
rm -f vsim.wlf

vlib work
vlog +define+SIM \
     +define+RATE=$RATE \
     +define+PKT_FILE=\"$PKT_FILE\" \
     +define+PKT_FILE_NB_LINES=$PKT_FILE_NB_LINES \
     +define+NB_DSC_QUEUES=$NB_DSC_QS \
     +define+NB_PKT_QUEUES=$NB_PKT_QS \
     +define+PKT_SIZE=$PKT_SIZE \
     ./src/*.sv -sv 
#vlog *.v
vlog +define+SIM ./src/common/*.sv -sv
vlog +define+SIM ./src/common/*.v
#vlog ./src/common/esram/synth/esram.v  
#vlog ./src/common/esram/esram_1913/synth/iopll.v  
#vlog ./src/common/esram/esram_1913/synth/esram_esram_1913_a3ainji.sv -sv
#vlog ./src/common/esram/altera_iopll_1930/synth/stratix10_altera_iopll.v
#vlog ./src/common/esram/altera_iopll_1930/synth/esram_altera_iopll_1930_rnqonzq.v

#GUI full debug
# vsim tb -L altera_mf_ver -L altera_lnsim_ver -L altera_ver -L lpm_ver -L sgate_ver -L fourteennm_ver -L fourteennm_ct1_ver -voptargs="+acc"

#NO GUI
vsim -L altera_mf_ver -L altera_lnsim_ver -L altera_ver -L lpm_ver -L sgate_ver -L fourteennm_ver -L fourteennm_ct1_ver -voptargs="+acc" -c -do "run -all" tb

#NO GUI, Optimized, but the stats may not match
#vsim -L altera_mf_ver -L altera_lnsim_ver -L altera_ver -L lpm_ver -L sgate_ver -L fourteennm_ver -L fourteennm_ct1_ver -c -do "run -all" tb
