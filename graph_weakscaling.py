import sys;
import subprocess;
import os;

exe = sys.argv[1] if len(sys.argv) >= 2 else input("Please enter the path of the executable file: ")
graph = sys.argv[2] if len(sys.argv) >= 3 else input("Please enter the path of the executable file: ")
graph_file = os.path.basename(graph)

threads = []
i = 1
while i < os.cpu_count():
    threads.append(i)
    i *= 2
threads.append(os.cpu_count())

for i in threads:
    subprocess.run([exe, "7", graph + str(i) + ".gr", "-n", "-t", str(i)] + sys.argv[3:], check=True)

outfile = "weakscaling-" + graph_file + ".csv"
with open(outfile, "w") as out:
    for i in threads:
        files = [f for f in os.listdir(".") if os.path.isfile(f) and graph_file + str(i) + ".gr" in f]
        files.sort(reverse=True)
        print(files[0])
        with open(files[0]) as input:
            for line in input:
                out.write(line)

print(f"Combined results written to {outfile}")
