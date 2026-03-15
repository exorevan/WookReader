#!/bin/bash
# Script to generate font object files for MuPDF on Windows
# This script creates .obj files from .cff font files using bin2coff.exe

# Directory containing font files
FONT_DIR="resources/fonts/urw"
OUTPUT_DIR="generated/resources/fonts/urw"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Find all .cff files
find "$FONT_DIR" -name "*.cff" | while read -r font_file; do
    # Get the base filename without extension
    base_name=$(basename "$font_file" .cff)
    
    # Generate the object file name
    obj_file="$OUTPUT_DIR/$base_name.cff.obj"
    
    # Generate the symbol name (replace - with _)
    symbol_name=$(echo "$base_name" | tr '-' '_')
    
    echo "Processing: $font_file -> $obj_file"
    
    # Use bin2coff.exe to create the object file
    # Note: This assumes bin2coff.exe is in the PATH or current directory
    bin2coff.exe "$font_file" "$obj_file" "_binary_${symbol_name}_cff"
    
    # Also create 64-bit version
    obj_file_64="$OUTPUT_DIR/$base_name.cff.x64.obj"
    bin2coff.exe "$font_file" "$obj_file_64" "_binary_${symbol_name}_cff" 64bit
    
    # Generate size symbol files as well
    size_symbol="_binary_${symbol_name}_cff_size"
    bin2coff.exe "$font_file" "$OUTPUT_DIR/${base_name}_size.cff.obj" "$size_symbol"
    bin2coff.exe "$font_file" "$OUTPUT_DIR/${base_name}_size.cff.x64.obj" "$size_symbol" 64bit
    
    # Add to the list of generated files
    echo "$obj_file"
echo "$obj_file_64"
echo "$OUTPUT_DIR/${base_name}_size.cff.obj"
echo "$OUTPUT_DIR/${base_name}_size.cff.x64.obj"
done