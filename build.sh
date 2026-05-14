#!/bin/bash
echo "🕷️ Building Pulse Walker..."

# Check PyInstaller
if ! command -v pyinstaller &> /dev/null; then
    echo "❌ PyInstaller not found. Install: pip install pyinstaller"
    exit 1
fi

# Clean previous builds
rm -rf build dist pulse-walker.spec 2>/dev/null

# Copy spec file if missing
if [ ! -f "bridge-gui.spec" ]; then
    echo "⚠️ bridge-gui.spec not found. Creating basic one..."
    pyi-makespec --onefile --windowed --name pulse-walker bridge-gui.py
    echo "✅ Created. Edit the file and run again."
    exit 0
fi

# Build
echo "🔄 Running PyInstaller..."
pyinstaller bridge-gui.spec

# Result
echo ""
if [ -f "dist/pulse-walker" ]; then
    echo "✅ Build successful!"
    echo "📦 Executable: dist/pulse-walker"
    chmod +x dist/pulse-walker
else
    echo "❌ Build failed. Check logs above."
fi