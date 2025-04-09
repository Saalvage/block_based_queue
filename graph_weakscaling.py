import sys;
import subprocess;
import os;

exe = sys.argv[1] if len(sys.argv) >= 2 else input("Please enter the path of the executable file: ")
graph = sys.argv[2] if len(sys.argv) >= 3 else input("Please enter the path of the executable file: ")
graph_file = os.path.basename(graph)

i = 1
while i <= os.cpu_count():
    subprocess.run([exe, "7", graph + str(i) + ".gr", "-t", str(i)] + sys.argv[3:], check=True)
    i *= 2

outfile = "weakscaling-" + graph_file + ".csv", "w"
with open(outfile, "w") as out:
    i = 1
    while i <= os.cpu_count():
        files = [f for f in os.listdir(".") if os.path.isfile(f) and graph_file + str(i) + ".gr" in f]
        files.sort(reverse=True)
        print(files[0])
        with open(files[0]) as input:
            for line in input:
                out.write(line)
        i *= 2

print(f"Combined results written to {outfile}")
