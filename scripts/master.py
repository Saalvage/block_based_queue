import subprocess
import os
import platform
import sys

from graph_weakscaling import *

include = sys.argv[3:] if len(sys.argv) > 3 else [".*bbq.*", ".*multififo.*", ".*kfifo.*", ".*dcbo.*", ".*lcrq", ".*faaaqueue.*"]
repeats = 2
used_threads = int(sys.argv[1]) if len(sys.argv) > 1 else os.cpu_count() # How many threads to use for fixed-thread benchmarks (parameter tuning and prodcon)

experiments = sys.argv[2].split(",") if len(sys.argv) > 2 else None
has_tuning = "tuning" in experiments or experiments is None
has_perf = "performance" in experiments or experiments is None
has_qual = "quality" in experiments or experiments is None
has_prodcon = "prodcon" in experiments or experiments is None
has_bfs = "bfs" in experiments or experiments is None

cwd = os.path.dirname(os.path.realpath(__file__))

arch = platform.machine().lower()
is_arm = "arm" in arch or "aarch" in arch
build_dir = "build_arm" if is_arm else "build"
extra_params = " -DBBQ_IS_ARM=ON" if is_arm else ""

def build():
    subprocess.run(f"cmake -B {build_dir} -S . -DCMAKE_BUILD_TYPE=Release -G Ninja{extra_params}".split(), cwd=os.path.join(cwd, ".."), check=True)
    subprocess.run(f"cmake --build {build_dir}".split(), cwd=os.path.join(cwd, '..'), check=True)

root_path = os.path.join(cwd, "..")
exe_path = os.path.join(root_path, build_dir, "relaxed_concurrent_fifo", "relaxed_concurrent_fifo" + (".exe" if sys.platform == "win32" else ""))
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

def list_files(path):
    return [g for g in os.listdir(path) if os.path.isfile(os.path.join(path, g))]

def bfs(fifo):
    for graph in list_files(ss_graphs_path):
        try:
            print_indented("Running BFS for " + graph)

            bfs = run_thing(fifo, "bfs", 7, os.path.join(ss_graphs_path, graph))

            if bfs != "":
                converter(os.path.join(data_path, "ss", graph), os.path.join(raw_path, bfs), "bfs-ss")
        except Exception as err:
            print_error("BFS failed on graph " + graph)
            print(err)

    for graph in list_files(ws_graphs_path):
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

def run_benchmark(fifo, i, name, on_success):
    try:
        res = run_thing(fifo, name, i)

        if res != "":
            on_success(res)
    except Exception as err:
        print_error(name + " failed!")
        print(err)
        return None

def run_generate():
    for fifo in include:
        if has_tuning:
            try:
                parameter_tuning(fifo)
            except Exception as err:
                print_error("Parameter tuning failed!")
                print(err)

        if has_bfs:
            bfs(fifo)

        if has_perf:
            run_benchmark(fifo, 1, "performance", lambda x: converter(data_path, os.path.join(raw_path, x), "performance"))

        if has_qual:
            run_benchmark(fifo, 2, "quality", lambda x: converter(data_path, os.path.join(raw_path, x), "quality"))

        if has_prodcon:
            run_benchmark(fifo, 6, "prodcon", lambda x: prodcon_postprocess())

def generate_plots():
    latex : str
    with open("template.tex") as f:
        latex = f.read()

    def sanitize_separators(str):
        return str.replace("\\", "/")

    def write_doc(title, x_axis, y_axis, file_prefix, data_path, axis_options=""):
        print(f"Generating plot for {title}")
        my_latex = latex
        my_latex = my_latex.replace("{TITLE}", title.replace("_", "\\_"))
        my_latex = my_latex.replace("{X_LABEL}", x_axis)
        my_latex = my_latex.replace("{Y_LABEL}", y_axis)
        my_latex = my_latex.replace("{AXIS_OPTIONS}", axis_options)
        my_latex = my_latex.replace("{TABLES}", "\n".join([f"\\addplot table {{{sanitize_separators(os.path.relpath(os.path.join(data_path, f), 'plots'))}}};\n\\addlegendentry{{{f.replace(file_prefix, '').replace('.dat', '')}}};"
                                                        for f in list_files(data_path) if f.startswith(file_prefix)]))
        file = title + ".tex"
        os.makedirs("plots", exist_ok=True)
        with open("plots/" + file, "w") as f:
            f.write(my_latex)
        subprocess.run(["pdflatex", file, "-interaction=batchmode"], cwd="plots", universal_newlines=True)

    write_doc("Parameter Tuning", "Avg. Rank Error", "Iterations", "tuning_", data_path, "only marks,")
    write_doc("Performance", "Threads", "Iterations", "performance-", data_path)
    write_doc("Quality", "Threads", "Avg. Rank Error", "quality-", data_path)
    write_doc("Producer-Consumer", "Consumers", "Iterations", "prodcon-", data_path)

    ss_path = os.path.join(data_path, "ss")
    for graph in os.listdir(ss_path):
        write_doc(graph, "Threads", "Time (ns)", "bfs-ss-", os.path.join(ss_path, graph))

    ws_path = os.path.join(data_path, "ws")
    for graph in os.listdir(ws_path):
        write_doc(graph, "Threads", "Time (ns)", "bfs-ws-", os.path.join(ws_path, graph))

build()
run_generate()
generate_plots()
