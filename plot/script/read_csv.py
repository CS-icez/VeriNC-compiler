import csv
from collections import defaultdict

def read_csv(file_path):
    """
    Reads a CSV file and returns the data as a list of tuples.
    
    Args:
        file_path (str): The path to the CSV file.
        
    Returns:
        tuple: Depending on the CSV format:
            - For 2-column CSV: A tuple (x, y) where x is a list of integers and y is a list of floats.
            - For 3-column CSV: A tuple (x, y) where x and y are defaultdicts with integer keys,
              and values are lists of integers (for x) and lists of floats (for y).
    """
    with open(file_path, 'r') as file:
        reader = csv.reader(file)
        header = next(reader)  # Skip the header
        if len(header) == 2:
            return _read_xy(reader)
        elif len(header) == 3:
            return _read_cxy(reader)
        else:
            raise ValueError(f"Invalid header length: {len(header)}. Expected 2 or 3.")

def _read_xy(reader):
    x = []
    y = []
    for row in reader:
        x.append(int(row[0]))
        y.append(float(row[1]))
    return x, y

def _read_cxy(reader):
    x = defaultdict(list)
    y = defaultdict(list)
    for row in reader:
        x[int(row[0])].append(int(row[1]))
        y[int(row[0])].append(float(row[2]))
    return x, y