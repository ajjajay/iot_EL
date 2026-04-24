#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# setup.sh — IoT Smart Monitor — Full Development Environment Setup
# Run from the project root: bash scripts/setup.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'

info()    { echo -e "${GREEN}[✓]${NC} $1"; }
warn()    { echo -e "${YELLOW}[!]${NC} $1"; }
error()   { echo -e "${RED}[✗]${NC} $1"; exit 1; }
section() { echo -e "\n${GREEN}━━ $1 ━━${NC}"; }

section "IoT Smart Monitor — Setup"
echo "This script installs all Python and optional Node.js dependencies."
echo "It does NOT configure secrets — edit .env manually after completion."

# ── Python ────────────────────────────────────────────────────────────────────
section "Python TinyML dependencies"
command -v python3 >/dev/null 2>&1 || error "python3 not found. Install Python 3.9+"
PYTHON_VER=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
info "Python version: $PYTHON_VER"

if [[ "$PYTHON_VER" < "3.9" ]]; then
  error "Python 3.9+ required (found $PYTHON_VER)"
fi

python3 -m pip install --upgrade pip -q
python3 -m pip install -r models/requirements.txt
info "Python ML dependencies installed"

# ── Generate mock dataset ────────────────────────────────────────────────────
section "Generating mock dataset"
if [[ ! -f models/mock_dataset.csv ]]; then
  python3 models/mock_data_generator.py --samples 5000
  info "mock_dataset.csv generated"
else
  warn "mock_dataset.csv already exists — skipping"
fi

# ── Train model (optional) ───────────────────────────────────────────────────
section "TinyML model training"
read -rp "Train TinyML model now? This takes ~1 min [y/N]: " TRAIN
if [[ "$TRAIN" =~ ^[Yy]$ ]]; then
  python3 models/model_training.py --epochs 80
  python3 models/model_converter.py
  info "Model trained and converted to tinyml_model.h"
else
  warn "Skipped. Run manually: python models/model_training.py && python models/model_converter.py"
fi

# ── Firebase CLI (optional) ──────────────────────────────────────────────────
section "Firebase CLI (optional)"
if command -v npm >/dev/null 2>&1; then
  read -rp "Install Firebase CLI via npm? [y/N]: " FBCLI
  if [[ "$FBCLI" =~ ^[Yy]$ ]]; then
    npm install -g firebase-tools
    info "Firebase CLI installed. Run: firebase login"
  fi
else
  warn "npm not found — skipping Firebase CLI. Install Node.js to get it."
fi

# ── .env setup ───────────────────────────────────────────────────────────────
section "Environment file"
if [[ ! -f .env ]]; then
  cp .env.example .env
  info ".env created from .env.example"
  warn "IMPORTANT: Edit .env and fill in your real credentials before proceeding"
else
  warn ".env already exists — skipping (check it's up to date with .env.example)"
fi

# ── Final summary ─────────────────────────────────────────────────────────────
section "Setup Complete"
echo "Next steps:"
echo "  1. Edit .env with your WiFi + Firebase credentials"
echo "  2. Create config.json per device (see firmware/README.md)"
echo "  3. Install Arduino libraries (see firmware/README.md)"
echo "  4. Flash firmware to ESP32"
echo "  5. Configure Firebase rules (see firebase/README.md)"
echo "  6. Open frontend/index.html in a browser"
echo ""
echo "  Full docs: cat claude.md"
