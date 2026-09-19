"""Checked adaptations of the pinned hand-written game functions.

Hand-written routines replace 68000 code but cost no emulated CPU time. These
patches charge their original time (docs/CADENCE.md): the decompressors path by
path (tools/decoder_patches.py), others from their disassembly or, marked as
such, from Genesis Plus GX profiles, which include DRAM refresh and so are
charged with charge() rather than pace().
"""

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned game source changed; review cadence patch: ' + old[:60])
    return text.replace(old, new)


# Hand-written routines charged their mean cost per call (Genesis Plus GX
# profile of 3,000 Round 1 frames): file -> (entry line, ROM address, cycles).
ENTRY_CHARGES = {
    'SoRControls.cpp': [('    traceEnter(0x568Au);\n', 0x568A, 125)],                 # remap_player_gameplay_input
    'SoRManualFunctions.cpp': [('    traceEnter(0x41EAu);\n', 0x41EA, 229)],          # compute_player_attack_descriptor
    'SoRSound.cpp': [('    traceEnter(0x0001069Eu);\n', 0x1069E, 197)],               # queue_sound_id
}


def charge_entries(name, text):
    for line, address, cycles in ENTRY_CHARGES.get(name, []):
        text = replace_once(text, line, line + '    pcHistogram(0x%Xu, %d); charge(%d);\n' % (address, cycles, cycles))
    return text


def patch(name, text):
    text = charge_entries(name, text)
    if name == 'SoRSound.cpp':
        # sound_ym2612_acquire ($73298), accesses at each instruction's start:
        # MOVE.W #$100,BUSREQ (20); BTST/BNE on the grant (28); BTST #7,($A01FFD)
        # (20); while the DAC driver is busy, BEQ.S (8), MOVE.W #0,BUSREQ (20),
        # three NOPs and BRA.S (22); otherwise BEQ.S (10), then YM busy polls
        # MOVE.B/BTST/BNE.S (34) and RTS (16).
        text = replace_once(text, '''    while (!shouldQuit()) {
        memory().writeWord(kZ80Busreq, 0x0100u);
        memory().waitForByteValue(kZ80Busreq, 0, waitForHardware);''', '''    while (!shouldQuit()) {
        memory().writeWord(kZ80Busreq, 0x0100u);
        pcHistogram(0x73298u, 20); pace(20);
        memory().waitForByteValue(kZ80Busreq, 0, waitForHardware);
        pcHistogram(0x73298u, 28); pace(28);''')
        text = replace_once(text, '''            memory().writeWord(kZ80Busreq, 0);
            continue;''', '''            pcHistogram(0x73298u, 28); pace(28);
            memory().writeWord(kZ80Busreq, 0);
            pcHistogram(0x73298u, 42); pace(42);
            continue;''')
        text = replace_once(text, '''        for (;;) {
            const m_byte status = memory().readByte(kYm2612A0);''', '''        pcHistogram(0x73298u, 30); pace(30);
        for (;;) {
            const m_byte status = memory().readByte(kYm2612A0);
            pcHistogram(0x73298u, 34); pace(34);''')
        return replace_once(text, '''        break;
    }

    cpu().ssp += 4;
}''', '''        break;
    }

    pcHistogram(0x73298u, 16); pace(16);
    cpu().ssp += 4;
}''')
    if name == 'SoRControls.cpp':
        # sample_all_joypads ($810C) holds the Z80 bus while it reads both pads:
        # the requesting MOVE.W (20), the two sample_one_joypad calls and their
        # setup (492), then the releasing MOVE.W (20) and RTS (16).
        text = replace_once(text, '''    memory().writeWord(kZ80BusRequest, 0x0100u);
    cpu().a[1] = kIoPlayer1DataPort;''', '''    memory().writeWord(kZ80BusRequest, 0x0100u);
    pcHistogram(0x810Cu, 512); pace(512);
    cpu().a[1] = kIoPlayer1DataPort;''')
        return replace_once(text, '''    memory().writeWord(kZ80BusRequest, 0);
    setMoveWordFlags(cpu(), 0);''', '''    memory().writeWord(kZ80BusRequest, 0);
    pcHistogram(0x810Cu, 36); pace(36);
    setMoveWordFlags(cpu(), 0);''')
    if name == 'SoRManualFunctions.cpp':
        # wait_vblank_and_upload_graphics ($10502) / wait_vblank_without_graphics_upload
        # ($10514): MOVE.B to the mailbox and MOVE to SR (16 each) before spinning
        # on it, then TST.B, BNE.S and RTS (36) once the VBlank handler clears it.
        for mailbox in ('1', '2'):
            entry = '10502u' if mailbox == '1' else '10514u'
            text = replace_once(text, '''    memory().writeByte(kVBlankMailbox, %s);
    cpu().setStatus(kStatusIrqEnabled);
''' % mailbox, '''    memory().writeByte(kVBlankMailbox, %s);
    pcHistogram(0x%s, 16); pace(16);
    cpu().setStatus(kStatusIrqEnabled);
    pcHistogram(0x%s, 16); pace(16);
''' % (mailbox, entry, entry))
        for entry in ('0x00010502u', '0x00010514u'):
            start = text.index('    traceEnter(%s);' % entry)
            end = text.index('    cpu().ssp += 4;\n}', start)
            text = text[:end] + '    pcHistogram(%s, 36); pace(36);\n' % entry + text[end:]
        # game_infinite_loop ($3A2): moveq, move.w, add.w, move.l table, movea,
        # jsr (a0), jsr and bra.s per pass (88 cycles).
        return replace_once(text, '''        // moveq #0,d0 / move.w (game_state).w,d0 / add.w d0,d0
        const m_word state''', '''        // moveq #0,d0 / move.w (game_state).w,d0 / add.w d0,d0
        pcHistogram(0x3A2u, 88); pace(88);
        const m_word state''')
    if name == 'SoRInteractions.cpp':
        # player_normal_attack_input ($3028): 80 per call on average.
        text = replace_once(text, '''void StreetsOfRage::player_normal_attack_input(m_long entry_) {
    traceEnter(entry_);
''', '''void StreetsOfRage::player_normal_attack_input(m_long entry_) {
    traceEnter(entry_);
    pcHistogram(0x3028u, 80); pace(80);
''')
        # find_close_interaction_target ($3136): 58 when the player is already
        # lifting; otherwise 144 to set up the box, 44 per slot scanned (all 68
        # when nothing is found) and 24 to return.
        return replace_once(text, '''    const m_long target = findPickupTarget(memory(), player);
    if (target == 0u) {''', '''    const m_long target = findPickupTarget(memory(), player);
    {
        unsigned cost = 58;
        if ((memory().readByte(player + kObjState) & 0xFEu) != 0x28u)
            cost = 168 + 44 * (target ? (target - kObjectTable) / kObjectSlotSize + 1 : kInteractionScanSlots);
        pcHistogram(0x3136u, cost); pace(cost);
    }
    if (target == 0u) {''')
    if name == 'SoRMainMenus.cpp':
        # restore_player_continues ($1199E): two MOVE.W and RTS (48). The port's
        # top-10 re-seed decode is not in the ROM routine and costs no time.
        text = replace_once(text, '''    traceEnter(0x0001199Eu);
''', '''    traceEnter(0x0001199Eu);
    pcHistogram(0x1199Eu, 48); pace(48);
''')
        text = replace_once(text, '''    enigmadec(0x00012832u);
    cpu().ssp += 4;''', '''    extern bool sorUnchargedDecode;
    sorUnchargedDecode = true;
    enigmadec(0x00012832u);
    sorUnchargedDecode = false;
    cpu().ssp += 4;''')
        # Update_PlayerObj's slot loop ($AE12): 40 cycles per empty slot and 154
        # per active slot around the handler calls (which charge themselves).
        # Outside the slot loop the routine costs 348 per update, plus 118 per
        # present player and 40 per absent one (Genesis Plus GX profile of the
        # Round 1 replay).
        text = replace_once(text, '''            const m_byte type = memory().readByte(cpu().a[0]);
            cpu().d[0]        = type;
            cpu().setFlag(CPU68K::FlagZ, type == 0);''', '''            const m_byte type = memory().readByte(cpu().a[0]);
            pcHistogram(0xAD8Eu, (player == 0 ? 348 : 0) + (type != 0 ? 118 : 40));
            charge((player == 0 ? 348 : 0) + (type != 0 ? 118 : 40));
            cpu().d[0]        = type;
            cpu().setFlag(CPU68K::FlagZ, type == 0);''')
        return replace_once(text, '''        const m_byte type = memory().readByte(cpu().a[0]);
        cpu().d[0]        = type;
        cpu().setNZClearVC(type, 0x80u);''', '''        const m_byte type = memory().readByte(cpu().a[0]);
        pcHistogram(0xAD8Eu, type != 0 ? 154 : 40); charge(type != 0 ? 154 : 40);
        cpu().d[0]        = type;
        cpu().setNZClearVC(type, 0x80u);''')
    if name == 'SoRDecompress.cpp':
        from decoder_patches import patch as patch_decoders
        return patch_decoders(text)
    return text
