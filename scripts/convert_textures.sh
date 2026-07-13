#!/usr/bin/env bash

find romfs -type f -name "*.png" | while IFS= read -r file; do
    if tex3ds -o "${file%.png}.t3x" "$file"; then
        rm "$file"
    else
        echo "Failed to convert: $file" >&2
    fi
done
