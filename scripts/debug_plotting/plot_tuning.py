import csv
import matplotlib.pyplot as plt
import sys
import statistics

def first(x):
    return next(iter(x))

class impl:
    def __init__(self):
        self.values = { }

file_performance = sys.argv[1] if len(sys.argv) == 2 else input("Please enter the performance .csv data file: ")
file_quality = sys.argv[1] if len(sys.argv) == 2 else input("Please enter the quality .csv data file: ")

def aggregate(file):
    impls = { }
    with open(file) as file:
        lines = csv.reader(file)
        for row in lines:
            if len(row) < 3:
                continue
            index_offset = 1 if row[1].isdigit() else 0
            name = row[1 + index_offset]
            if name not in impls:
                impls[name] = impl()
            x = row[0]
            if index_offset != 0:
                x += ',' + row[1]
            y = float(row[3 + index_offset])
            if (x not in impls[name].values):
                impls[name].values[x] = [y]
            else:
                impls[name].values[x].append(y)
    return impls

p = aggregate(file_performance)
q = aggregate(file_quality)

impls = { }

for name in p.keys():
    impls[name] = impl()
    for k in p[name].values.keys():
        impls[name].values[q[name].values[k][0]] = p[name].values[k][0]

for k, v in impls.items():
    values = v.values.values()
    #avgs = list(map(statistics.mean, values))
    #std = list(map(statistics.stdev, values)) if len(first(values)) > 1 else ([0] * len(values))
    #xs, ys, std = zip(*sorted(zip(v.values.keys(), avgs, std)))

    plt.plot(v.values.keys(), v.values.values(), "o", label=k)
    plt.xlabel("Quality")
    plt.title("FIFO-Queue Comparison")

plt.xscale("log")
plt.yscale("log")
plt.ylabel("Performance")
plt.grid()
plt.legend()
plt.show()
