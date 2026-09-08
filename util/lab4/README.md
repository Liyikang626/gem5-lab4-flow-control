# ETFC experiments

Build Garnet in WSL, then run the paper experiment matrix from the repository
root:

```bash
scons build/NULL/gem5.opt PROTOCOL=Garnet_standalone -j2
bash util/lab4/run_etfc_experiments.sh /path/to/results/etfc-paper 2
```

The first argument is the output directory. Keep it outside the Git checkout.
The second argument is the number of simulations to run in parallel. The
script writes raw gem5 statistics and a combined `metrics.csv`.
The primary experiments use four VCs per vnet with eight entries per VC,
matching the original Lab 4 baseline. Buffer depth and VC count are varied only
in their named ablation suites.

Generate the paper diagrams and plots with Python, pandas, NumPy, and
Matplotlib:

```bash
python3 util/lab4/make_etfc_figures.py \
    /path/to/results/etfc-paper/metrics.csv /path/to/report/figures
```

Every figure is emitted as editable SVG and as a vector PDF for LaTeX.
