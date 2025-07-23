import sys;
import csv;

file_fill = sys.argv[1] if len(sys.argv) >= 2 else input("Please enter the fill .csv data file: ")
file_empty = sys.argv[2] if len(sys.argv) >= 3 else input("Please enter the empty .csv data file: ")

with open("fill-empty-ratio.csv", "w") as out:
    with open(file_fill) as fill:
        with open(file_empty) as empty:
            lines_fill = csv.reader(fill)
            lines_empty = csv.reader(empty)
            for (row_fill, row_empty) in zip(lines_fill, lines_empty):
                if row_fill[1].isnumeric() and row_fill[0] == row_empty[0] and row_fill[1] == row_empty[1]:
                    out.write(row_fill[0] + "," + row_fill[1] + "," + str(float(row_fill[2]) / float(row_empty[2])) + "\n")
