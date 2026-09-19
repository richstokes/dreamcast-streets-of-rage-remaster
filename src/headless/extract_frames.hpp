#pragma once
class VDPState;
namespace sor {
// Host analysis: SOR_EXTRACT_FRAMES=dir saves each distinct object frame drawn.
void extract_frames(const VDPState &,unsigned frame);
void extract_frames_finish();
}
