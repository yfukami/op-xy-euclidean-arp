# OP-XY Euclidean Arpeggiator Player Rules

- **SDK Location:** The Open-XY SDK lives at `../open-xy`. Always inspect existing Player machines and SDK headers in `../open-xy` before writing code so parameter registration, MIDI event structs, clock/transport hooks, and SDK 1.4 two-page header UI helpers match the exact API.
- **Target & Memory Constraints:** Embedded C++ for Analog Devices Blackfin DSP (OP-XY OS 1.2.1). Zero dynamic heap allocation (`new`, `malloc`, `std::vector`, `std::string`) inside audio/MIDI callbacks or screen rendering; pre-allocate all state inside the machine struct.
- **Verification Command (Run after every code edit):**
  `../open-xy/.venv/bin/python ../open-xy/sdk.py test --machine machines/euclidean_arp --cxx /opt/homebrew/opt/llvm/bin/clang++`
- **Desktop Preview Command:**
  `../open-xy/.venv/bin/python ../open-xy/sdk.py preview --machine machines/euclidean_arp --cxx /opt/homebrew/opt/llvm/bin/clang++`
- **Git Hygiene:** Commit working milestones cleanly to this repository only; do not modify files inside `../open-xy`.
