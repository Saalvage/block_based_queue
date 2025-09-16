import csv
import sys
import statistics

class impl:
    def __init__(self):
        self.values = { }

impls = { }

file = sys.argv[1] if len(sys.argv) >= 2 else input("Please enter the .csv data file: ")
output_prefix = sys.argv[2] if len(sys.argv) >= 3 else input("Please enter the output file prefix: ")

with open(file) as file:
    lines = csv.reader(file)
    for row in lines:
        name = row[0]
        if name not in impls:
            impls[name] = impl()
        x = int(row[1])
        y = float(row[2])
        if (x not in impls[name].values):
            impls[name].values[x] = [y]
        else:
            impls[name].values[x].append(y)

for k, v in impls.items():
    with open(output_prefix + "-" + k + ".dat", "w") as file:
        file.write("threads its std\n")
        for x, y in sorted(v.values.items()):
            file.write(str(x) + " " + str(statistics.mean(y)) + " " + str(statistics.stdev(y)) + "\n")
