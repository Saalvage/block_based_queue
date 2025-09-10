import subprocess
import os
import os.path
import sys

from graph_weakscaling import *

include = sys.argv[1:] if len(sys.argv) > 1 else [".*bbq.*", ".*multififo.*", ".*kfifo.*", ".*dcbo.*", ".*lcrq", ".*faaaqueue.*"]
repeats = 2
used_threads = os.cpu_count() # How many threads to use for fixed-thread benchmarks (parameter tuning and prodcon)

cwd = os.path.dirname(os.path.realpath(__file__))

subprocess.run(f"cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -G Ninja".split(), cwd=os.path.join(cwd, ".."), check=True)
subprocess.run(f"cmake --build build".split(), cwd=os.path.join(cwd, '..'), check=True)

root_path = os.path.join(cwd, "..")
exe_path = os.path.join(root_path, "build", "relaxed_concurrent_fifo", "relaxed_concurrent_fifo.exe")
graphs_path = os.path.join(cwd, "graphs")
ss_graphs_path = os.path.join(graphs_path, "ss")
ws_graphs_path = os.path.join(graphs_path, "ws")
raw_path = os.path.join(cwd, "raw")
os.makedirs(raw_path, exist_ok=True)
data_path = os.path.join(cwd, "data")
os.makedirs(data_path, exist_ok=True)

def print_indented(str):
    print(">>> " + str)

def print_error(str):
    print("!!! " + str)

def run_thing(fifo, name, i, extra=""):
        print_indented(f"Running executable on {fifo} for {name}.")
        res = subprocess.run(f"{exe_path} {i} {extra} -r {repeats} -i {fifo}.* -q -n".split(), stderr=subprocess.PIPE, cwd=raw_path, universal_newlines=True)
        print_indented(f"Exited with {res.returncode}.")

        if res.returncode == 0:
            return res.stderr.strip()
        return ""

def converter(cwd, path, name):
    os.makedirs(cwd, exist_ok=True)
    subprocess.run(f"python {os.path.join(root_path, 'converter.py')} {path} {name}".split(), cwd=cwd, universal_newlines=True)

def parameter_tuning(fifo):
    extra = f"--parameter-tuning -t {used_threads}"
    performance = run_thing(fifo, "performance-pt", 1, extra + " -s 1")
    quality = run_thing(fifo, "quality-pt", 2, extra)

    if performance != "" and quality != "":
        subprocess.run(f"python {os.path.join(root_path, 'parameter_tuning.py')} {os.path.join(raw_path, performance)} {os.path.join(raw_path, quality)}".split(), cwd=data_path, universal_newlines=True)

def bfs(fifo):
    for graph in [g for g in os.listdir(ss_graphs_path) if os.path.isfile(os.path.join(ss_graphs_path, g))]:
        try:
            print_indented("Running BFS for " + graph)

            bfs = run_thing(fifo, "bfs", 7, os.path.join(ss_graphs_path, graph))

            if bfs != "":
                converter(os.path.join(data_path, "ss", graph), os.path.join(raw_path, bfs), "bfs-ss")
        except Exception as err:
            print_error("BFS failed on graph " + graph)
            print(err)

    for graph in [g for g in os.listdir(ws_graphs_path) if os.path.isfile(os.path.join(ws_graphs_path, g))]:
        try:
            if (not graph.endswith("_t_1.gr")):
                continue

            graph = graph[:-4]

            print_indented("Running WS BFS for " + graph)
            ws = run_weakscaling(exe_path, raw_path, os.path.join(ws_graphs_path, graph), "7", ["-i", fifo, "-q"])
            converter(os.path.join(data_path, "ws", graph), ws, "bfs-ws")
        except Exception as err:
            print_error("WS BFS failed on graph " + graph)
            print(err)

def prodcon_postprocess():
    subprocess.run(f"python {os.path.join(root_path, 'producer_consumer.py')} {used_threads}".split(), cwd=raw_path, universal_newlines=True)
    subprocess.run(f"python {os.path.join(root_path, 'converter.py')} {cwd}/raw/producer-consumer-{used_threads}.csv prodcon".split(), cwd=data_path, universal_newlines=True)

for fifo in include:
    try:
        parameter_tuning(fifo)
    except Exception as err:
        print_error("Parameter tuning failed!")
        print(err)

    bfs(fifo)

    for (i, name, on_success) in [
            (1, "performance", lambda x: converter(data_path, os.path.join(raw_path, x), "performance")),
            (2, "quality", lambda x: converter(data_path, os.path.join(raw_path, x), "quality")),
            (6, "prodcon", lambda x: prodcon_postprocess())]:
        try:
            res = run_thing(fifo, name, i)

            if res != "":
                on_success(res)
        except Exception as err:
            print_error(name + " failed!")
            print(err)
