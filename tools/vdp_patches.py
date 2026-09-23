"""Small, checked timing corrections to the pinned MegaDriveEnvironment VDP."""


from patching import replace_once


def patch(name, text):
    if name == 'VDPPort.hpp':
        return replace_once(text, '    void executeDMACopy();', '    void executeDMACopy();\n    uint64_t dmaDuration(uint64_t units, int blankRate, int activeRate);')
    if name == 'VDPState.hpp':
        return replace_once(text, '    uint64_t dmaEndCycle_ = 0;',
                            '    uint64_t dmaEndCycle_ = 0;\n'
                            '    // SoR port: start of the frame containing the last counter update.\n'
                            '    uint64_t sorFrameBase_ = 0;\n    int sorFrameCycles_ = 0;\n'
                            '    // SoR port: VRAM change tracking for the renderer caches. Every VRAM\n'
                            '    // write bumps the generation and marks its 32-byte tile; the renderer\n'
                            '    // clears tile marks as it checks them.\n'
                            '    uint32_t vramGeneration_ = 0;\n    m_byte tileDirty_[VRAM_SIZE / 32]{};\n'
                            '    void markVRAM(unsigned addr) { ++vramGeneration_; tileDirty_[(addr & 0xFFFF) >> 5] = 1; }\n'
                            '    void markAllVRAM() { ++vramGeneration_; std::memset(tileDirty_, 1, sizeof(tileDirty_)); }')
    if name == 'VDPState.cpp':
        text = replace_once(text, '    std::memset(vram_, 0, sizeof(vram_));\n',
                            '    std::memset(vram_, 0, sizeof(vram_));\n    markAllVRAM();\n')
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
    # A transfer started in vertical blank continues at active-display rates
    # once the next frame's display begins (Genesis Plus GX vdp_dma_update()).
    text = replace_once(text, 'void VDPPort::executeDMACopy() {', '''uint64_t VDPPort::dmaDuration(uint64_t units, int blankRate, int activeRate) {
    VDPState &s = *state_;
    const bool pal = env_ != nullptr && env_->isPal50Hz();
    const uint64_t now = currentMasterCycles(), line = VDPState::MASTER_CYCLES_PER_LINE;
    s.updateCountersFromCycles(now, pal);
    if (!s.displayEnabled())
        return std::max<uint64_t>(1, units * line / blankRate);
    if (s.vCounter_ < s.activeHeight())
        return std::max<uint64_t>(1, units * line / activeRate);
    const uint64_t frame = uint64_t(s.linesPerFrame(pal)) * line;
    const uint64_t left = frame - now % frame;            // until line 0
    const uint64_t blankUnits = left * blankRate / line;
    if (units <= blankUnits)
        return std::max<uint64_t>(1, units * line / blankRate);
    return left + (units - blankUnits) * line / activeRate;
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
    text = replace_once(text, '''    const int slotsPerLine = s.h40Mode() ? 18 : 16;
    s.dmaEndCycle_ = currentMasterCycles()
                   + std::max<uint64_t>(1, (static_cast<uint64_t>(count) * VDPState::MASTER_CYCLES_PER_LINE)
                                               / static_cast<uint64_t>(slotsPerLine));

    std::vector<uint8_t> buf''', '''    const uint64_t units = (s.code_ & 0x0F) == 0x01 ? 2ull * count : uint64_t(count);
    const uint64_t duration = dmaDuration(units, s.h40Mode() ? 204 : 166, s.h40Mode() ? 18 : 16);
    s.dmaEndCycle_ = currentMasterCycles() + duration;
    if (env_) env_->stallCpu(duration);

    std::vector<uint8_t> buf''')
    # Fill and VRAM copy (byte counts): duration from dmaDuration().
    for active, blank in (('s.h40Mode() ? 17 : 15', 's.h40Mode() ? 203 : 165'),
                          ('s.h40Mode() ? 9 : 8', 's.h40Mode() ? 102 : 83')):
        text = replace_once(text, '''    const int slotsPerLine = %s;
    s.dmaEndCycle_ = currentMasterCycles()
                   + std::max<uint64_t>(1, (static_cast<uint64_t>(count) * VDPState::MASTER_CYCLES_PER_LINE)
                                               / static_cast<uint64_t>(slotsPerLine));''' % active,
                            '''    s.dmaEndCycle_ = currentMasterCycles() + dmaDuration(uint64_t(count), %s, %s);''' % (blank, active))
    # VRAM change tracking (VDPState::markVRAM): word writes (also 68K DMA),
    # fill and VRAM copy.
    text = replace_once(text, '''    s.vram_[addr & 0xFFFE] = (value >> 8) & 0xFF;
    s.vram_[addr | 0x0001] = value & 0xFF;''', '''    s.vram_[addr & 0xFFFE] = (value >> 8) & 0xFF;
    s.vram_[addr | 0x0001] = value & 0xFF;
    s.markVRAM(addr);''')
    text = replace_once(text, '''    s.vram_[s.address_ | 0x0001] = fillWord & 0xFF;''', '''    s.vram_[s.address_ | 0x0001] = fillWord & 0xFF;
    s.markVRAM(s.address_);''')
    text = replace_once(text, '''        s.vram_[target]  = fillByte;''', '''        s.vram_[target]  = fillByte;
        s.markVRAM(unsigned(target));''')
    text = replace_once(text, '''        s.vram_[s.address_ ^ 1] = s.vram_[srcAddr ^ 1];''', '''        s.vram_[s.address_ ^ 1] = s.vram_[srcAddr ^ 1];
        s.markVRAM(s.address_);''')
    return text
