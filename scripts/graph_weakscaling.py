import sys;
import subprocess;
import os;
import os.path;

def run_weakscaling(exe, dir, graph, idx="7", extra_args=[]):
    graph_file = os.path.basename(graph)

    threads = []
    i = 1
    while i < os.cpu_count():
        threads.append(i)
        i *= 2
    threads.append(os.cpu_count())
    max_thread = sys.maxsize

    for i in threads:
        full_graph_file = f"{graph}{i}.gr"
        if not os.path.isfile(full_graph_file):
            max_thread = i
            break
        arr = [exe, idx, full_graph_file, "-n", "-t", str(i)]
        subprocess.run(arr + extra_args, check=True, cwd=dir)

    outfile = os.path.join(dir, "weakscaling-" + graph_file + ".csv")
    with open(outfile, "w") as out:
        for i in threads:
            if i >= max_thread:
                break
            files = [os.path.join(dir, f) for f in os.listdir(dir) if os.path.isfile(os.path.join(dir, f)) and f"{graph_file}{i}.gr" in f]
            files.sort(reverse=True)
            print(files[0])
            with open(files[0]) as input:
                for line in input:
                    out.write(line)

    print(f"Combined results written to {outfile}")
    return outfile

if __name__ == "__main__":
    exe = sys.argv[1] if len(sys.argv) >= 2 else input("Please enter the path of the executable file: ")
    graph = sys.argv[2] if len(sys.argv) >= 3 else input("Please enter the path of the graph: ")
    idx = sys.argv[3] if len(sys.argv) >= 4 else "7"
    run_weakscaling(exe, ".", graph, idx, sys.argv[4:])
