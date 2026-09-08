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
