import csv
import sys
import statistics
import numpy as np

class ParameterSet:
    def __init__(self):
        self.performance = []
        self.quality = []

file_performance = sys.argv[1] if len(sys.argv) > 1 else input("Please enter the performance .csv data file: ")
file_quality = sys.argv[2] if len(sys.argv) > 2 else input("Please enter the quality .csv data file: ")

values = { }

def is_data(entry):
    return entry.replace(".", "", 1).isdigit()

def get_key(row):
    i = 0
    while is_data(row[i]):
        i += 1
    return tuple(row[:i])

with open(file_performance) as file:
    lines = csv.reader(file)
    for row in lines:
        key = get_key(row)
        inner_values = values.setdefault(row[len(key)], { })
        inner_values.setdefault(key, ParameterSet()).performance.append(float(row[len(key) + 2]))

with open(file_quality) as file:
    lines = csv.reader(file)
    for row in lines:
        key = get_key(row)
        values[row[len(key)]][key].quality.append(float(row[len(key) + 2]))

for k, parameters in values.items():
    xstd = list(map(lambda x: statistics.stdev(x.quality), parameters.values()))
    x = list(map(lambda x: statistics.mean(x.quality), parameters.values()))
    ystd = np.array(list(map(lambda x: statistics.stdev(x.performance), parameters.values())))
    y = np.array(list(map(lambda x: statistics.mean(x.performance), parameters.values())))

    print(k)

    for parameter, x, xstd, y, ystd in zip(parameters.keys(), x, xstd, y, ystd):
        print(str(x) + " " + str(y) + " " + str(xstd) + " " + str(ystd) + " " + " ".join(parameter))

    print()
