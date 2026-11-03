# Top-level driver for the DSME CFP-extension study.
PY      ?= python3
VENV    ?= .venv/bin/python
JOBS    ?= 10
NODES   ?= 21 41 61 81 101
SEEDS   ?= 1 2 3

.PHONY: help test build run run-all analyse figures clean clean-results

help:
	@echo "make test         host-compiled unit tests for the estimator"
	@echo "make build        build every protocol variant"
	@echo "make run          scenario A: the offered load of the paper"
	@echo "make run-all      every scenario (A, B moderate load, C short bursts, sensitivity)"
	@echo "make analyse      tables, CSV and docs/RESULTS.md"
	@echo "make figures      figures into analysis/out/"
	@echo "make clean        remove build artefacts"
	@echo "make clean-results  remove simulation logs as well"

test:
	$(MAKE) -C tests

build:
	$(PY) sim/run_experiments.py --build-only --run-seconds 58

run:
	$(PY) sim/run_experiments.py --nodes $(NODES) --seeds $(SEEDS) --jobs $(JOBS)

run-all: run
	$(PY) sim/run_experiments.py --sensitivity --nodes 41 --seeds $(SEEDS) --jobs $(JOBS)
	$(PY) sim/run_experiments.py --variants legacy capred tacfpext tacfpext-t1 ptacfpext \
	      abl-notrend abl-noonset abl-nosab --nodes 21 41 61 --seeds $(SEEDS) \
	      --jobs $(JOBS) --burst 6 --tag b6 --rebuild
	$(PY) sim/run_experiments.py --variants legacy capred tacfpext tacfpext-t1 ptacfpext \
	      --nodes 41 --seeds $(SEEDS) --jobs $(JOBS) --period-bi 4 --burst-bi 1 \
	      --tag sb --rebuild

analyse:
	$(PY) analysis/report.py
	$(PY) analysis/make_results_doc.py

figures:
	$(VENV) analysis/plots.py

clean:
	rm -rf sim/variants/*/build sim/variants/*/prebuilt.cooja
	$(MAKE) -C tests clean

clean-results:
	rm -rf results/*/ analysis/out
