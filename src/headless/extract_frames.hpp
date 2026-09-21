#pragma once
class VDPState;
namespace sor {
// Host analysis: SOR_EXTRACT_FRAMES=dir saves each distinct object frame drawn.
void extract_frames(const VDPState &,unsigned frame);
void extract_frames_finish();
// The round being played: recorded with each frame seen (per-round art loading).
void extract_frames_round(unsigned round);
// The ROM, for rendering whole animation sets (SOR_EXTRACT_FRAMES).
void extract_frames_rom(const char *path);
}
