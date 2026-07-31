#pragma once
#include "services/pos_app.hpp"

namespace rp {
// Runs the Dear ImGui POS shell. Returns process exit code.
int run_ui(PosApp& app, const char* title = "RestoPulse POS");
}
