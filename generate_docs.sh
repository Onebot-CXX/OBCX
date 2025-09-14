#!/bin/bash

# OBCX Framework Documentation Generator
# This script generates documentation in both Chinese and English

echo "OBCX Framework Documentation Generator"
echo "======================================"

# Check if doxygen is installed
if ! command -v doxygen &> /dev/null; then
    echo "Error: doxygen is not installed. Please install it first."
    exit 1
fi

# Create docs directory if it doesn't exist
mkdir -p docs

# Generate Chinese documentation
echo "Generating Chinese documentation..."
# Copy main config file
cp Doxyfile Doxyfile.zh

# Modify config for Chinese
sed -i 's/ENABLED_SECTIONS       = /ENABLED_SECTIONS       = CHINESE/' Doxyfile.zh
sed -i 's/HTML_OUTPUT            = html/HTML_OUTPUT            = html-zh/' Doxyfile.zh
sed -i 's/PROJECT_BRIEF          = "OneBot C++ eXtension Framework"/PROJECT_BRIEF          = "OneBot C++ 扩展框架"/' Doxyfile.zh

# Generate Chinese docs
doxygen Doxyfile.zh

echo "Chinese documentation generated in docs/html-zh/"

# Generate English documentation
echo "Generating English documentation..."
# Copy main config file
cp Doxyfile Doxyfile.en

# Modify config for English
sed -i 's/ENABLED_SECTIONS       = /ENABLED_SECTIONS       = ENGLISH/' Doxyfile.en
sed -i 's/HTML_OUTPUT            = html/HTML_OUTPUT            = html-en/' Doxyfile.en

# Generate English docs
doxygen Doxyfile.en

echo "English documentation generated in docs/html-en/"

# Clean up temporary files
rm -f Doxyfile.zh Doxyfile.en

echo ""
echo "Documentation generation complete!"
echo "Chinese docs: docs/html-zh/index.html"
echo "English docs: docs/html-en/index.html" 