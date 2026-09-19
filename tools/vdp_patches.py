"""Small, checked timing corrections to the pinned MegaDriveEnvironment VDP."""


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned VDP source changed; review timing patch')
    return text.replace(old, new)


def patch(name, text):
    if name == 'VDPPort.hpp':
        return replace_once(text, '    void executeDMACopy();', '    void executeDMACopy();\n    bool dmaInBlanking();')
    if name == 'VDPState.hpp':
        return replace_once(text, '    uint64_t dmaEndCycle_ = 0;',
                            '    uint64_t dmaEndCycle_ = 0;\n'
                            '    // SoR port: start of the frame containing the last counter update.\n'
                            '    uint64_t sorFrameBase_ = 0;\n    int sorFrameCycles_ = 0;')
    if name == 'VDPState.cpp':
        # Status and HV reads recompute counters with three 64-bit divisions,
        # which SH-4 performs in software (~1,400 cycles per status poll).
        # Cache the containing frame's start; the counters are unchanged.
        return replace_once(text, '''    const int frameLines = linesPerFrame(pal);
    uint64_t  frameCycle = masterCycles % static_cast<uint64_t>(frameLines * MASTER_CYCLES_PER_LINE);
    vCounter_ = static_cast<m_word>(frameCycle / MASTER_CYCLES_PER_LINE);

    const uint64_t lineCycle = frameCycle % MASTER_CYCLES_PER_LINE;
    hCounter_ = static_cast<m_word>((lineCycle * 256) / MASTER_CYCLES_PER_LINE);''', '''    const int frameCycles = linesPerFrame(pal) * MASTER_CYCLES_PER_LINE;
    if (frameCycles != sorFrameCycles_ || masterCycles < sorFrameBase_ ||
        masterCycles - sorFrameBase_ >= static_cast<uint64_t>(frameCycles)) {
        sorFrameCycles_ = frameCycles;
        sorFrameBase_   = masterCycles - masterCycles % static_cast<uint64_t>(frameCycles);
    }
    const uint32_t frameCycle = static_cast<uint32_t>(masterCycles - sorFrameBase_);
    vCounter_ = static_cast<m_word>(frameCycle / MASTER_CYCLES_PER_LINE);

    const uint32_t lineCycle = frameCycle % MASTER_CYCLES_PER_LINE;
    hCounter_ = static_cast<m_word>((lineCycle * 256) / MASTER_CYCLES_PER_LINE);''')
    if name != 'VDPPort.cpp':
        return text
    # Blanking: display forcibly disabled, or the raster is in vertical blank
    # (Genesis Plus GX: `(status & 8) || !(reg[1] & 0x40)`). VBlank-handler DMA
    # (sprite tables, player art) runs at blanking rates on the console.
    text = replace_once(text, 'void VDPPort::executeDMACopy() {', '''bool VDPPort::dmaInBlanking() {
    VDPState &s = *state_;
    s.updateCountersFromCycles(currentMasterCycles(), env_ != nullptr && env_->isPal50Hz());
    return !s.displayEnabled() || s.vCounter_ >= s.activeHeight();
}

void VDPPort::executeDMACopy() {''')
    # With the display forcibly blanked (register 1 bit 6 clear) every line is
    # a blanking line, so DMA runs at the blanking transfer counts documented in
    # Genesis Plus GX's vdp_dma_update(): 68K>VRAM 166/204, fill 165/203 and
    # copy 83/102 per H32/H40 line. The pinned model always used the active
    # display counts, stretching SoR's 64 KiB VRAM clear to ~15 frames.
    # Registers cannot change during the fill: decode the increment and SAT
    # base once instead of per byte. Written bytes and SAT shadow are unchanged.
    text = replace_once(text, '''    s.address_ += static_cast<uint16_t>(s.autoIncrement());
    for (int i = 1; i < count; ++i) {
        s.vram_[s.address_ ^ 1] = fillByte;
        updateSATShadow(s.address_ ^ 1);
        s.address_ += static_cast<uint16_t>(s.autoIncrement());
    }''', '''    const uint16_t increment = static_cast<uint16_t>(s.autoIncrement());
    const int      satBase   = s.satBase();
    s.address_ += increment;
    for (int i = 1; i < count; ++i) {
        const int target = s.address_ ^ 1;
        s.vram_[target]  = fillByte;
        if (static_cast<unsigned>(target - satBase) < static_cast<unsigned>(VDPState::SAT_SIZE)) {
            s.sat_[target - satBase] = fillByte;
        }
        s.address_ += increment;
    }''')
    # 68K-to-VDP DMA: counts are words, and each VRAM word is two byte slots
    # (CRAM/VSRAM counts are in words already). The 68000 is halted meanwhile.
    text = replace_once(text, '''    s.dmaEndCycle_ = currentMasterCycles()
                   + std::max<uint64_t>(1, (static_cast<uint64_t>(count) * VDPState::MASTER_CYCLES_PER_LINE)
                                               / static_cast<uint64_t>(slotsPerLine));

    std::vector<uint8_t> buf''', '''    const uint64_t units = (s.code_ & 0x0F) == 0x01 ? 2ull * count : uint64_t(count);
    const uint64_t duration = std::max<uint64_t>(1, units * VDPState::MASTER_CYCLES_PER_LINE / static_cast<uint64_t>(slotsPerLine));
    s.dmaEndCycle_ = currentMasterCycles() + duration;
    if (env_) env_->stallCpu(duration);

    std::vector<uint8_t> buf''')
    for old, blank in (('s.h40Mode() ? 18 : 16', 's.h40Mode() ? 204 : 166'),
                       ('s.h40Mode() ? 17 : 15', 's.h40Mode() ? 203 : 165'),
                       ('s.h40Mode() ? 9 : 8', 's.h40Mode() ? 102 : 83')):
        # Anchor on the DMA busy flag so the separate FIFO model is untouched.
        text = replace_once(text, 's.status_ |= 0x0002;\n    const int slotsPerLine = ' + old + ';',
                            's.status_ |= 0x0002;\n    const int slotsPerLine = dmaInBlanking() ? (' + blank + ') : (' + old + ');')
    return text
