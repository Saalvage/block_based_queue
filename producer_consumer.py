import os;
import csv;

step = int(os.cpu_count() / 16)

with open("producer-consumer.csv", "w") as out:
    i = step
    while i < os.cpu_count():
        files = [f for f in os.listdir(".") if os.path.isfile(f) and "fifo-prodcon-" + str(i) + "-" in f]
        files.sort(reverse=True)
        print(files[0])
        with open(files[0]) as input:
            lines = csv.reader(input)
            for row in lines:
                if row[1].isnumeric():
                    out.write(row[0] + "," + str(i) + "," + row[2] + "\n")
        i += step
