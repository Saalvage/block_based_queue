import csv;
import sys;
import os;

threads_raw = sys.argv[1] if len(sys.argv) >= 2 else input(f"Please enter the thread count file ({os.cpu_count()}): ")
threads = os.cpu_count() if threads_raw == "" else int(threads_raw)

step = int(threads / 16)

with open(f"producer-consumer-{threads}.csv", "w") as out:
    i = step
    while i < threads:
        files = [f for f in os.listdir(".") if os.path.isfile(f) and "fifo-prodcon-" + str(i) + "-" + str(threads-i) + "-" in f]
        files.sort(reverse=True)
        print(files[0])
        with open(files[0]) as input:
            lines = csv.reader(input)
            for row in lines:
                if row[1].isnumeric():
                    out.write(row[0] + "," + str(i) + "," + row[2] + "\n")
        i += step
