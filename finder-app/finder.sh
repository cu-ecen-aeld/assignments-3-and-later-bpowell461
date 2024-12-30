#!/bin/bash
# Check if the correct number of arguments is provided
if [ "$#" -ne 2 ]; then
    echo "Error: Two arguments required. Usage: $0 <directory> <search_string>"
    exit 1
fi
filesdir=$1
searchstr=$2
# Check if the provided directory exists and is a directory
if [ ! -d "$filesdir" ]; then
    echo "Error: Directory $filesdir does not exist."
    exit 1
fi
# Find the number of files in the directory and subdirectories
num_files=$(find "$filesdir" -type f | wc -l)
# Find the number of matching lines in the files
num_matching_lines=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)
# Print the result
echo "The number of files are $num_files and the number of matching lines are $num_matching_lines"