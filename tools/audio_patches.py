"""Small, checked adaptations of the pinned BSD-licensed ymfm source."""
def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned ymfm source changed; review single-channel patch')
    return text.replace(old, new)


def patch(name, text):
    if name == 'ymfm_fm.h':
        old='\tvoid output(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chanmask) const;'
        text=replace_once(text, old, old+'\n\tvoid output_single(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chnum) const;')
        text=replace_once(text, '\tuint32_t m_phase;                      // current phase value (10.10 format)',
            '\tuint32_t m_phase;                      // current phase value (10.10 format)\n'
            '\tuint32_t m_sor_attenuation = 0;\n\tbool m_sor_am = false, m_sor_ssg = false;\n'
            '\tvoid sor_cache_attenuation() { uint32_t v=m_env_attenuation >> m_cache.eg_shift; '
            'if (RegisterType::EG_HAS_SSG && m_ssg_inverted) v=(0x200-v)&0x3ff; '
            'm_sor_attenuation=v+m_cache.total_level; }')
        old='\tvoid clock(uint32_t env_counter, int32_t lfo_raw_pm);'
        assert text.count(old)==2
        text=text.replace(old, '\ttemplate<bool Envelope> void clock_fast(uint32_t env_counter, int32_t lfo_raw_pm);\n'
            '\tvoid clock(uint32_t e, int32_t l) { if ((e&3)==0) clock_fast<true>(e,l); else clock_fast<false>(e,l); }')
        return text
    if name == 'ymfm_fm.ipp':
        marker='template<class RegisterType>\nvoid fm_engine_base<RegisterType>::output('
        implementation='''// SoR port: YM2612 requests one channel at a time for DAC discontinuity.
// Avoid a channel-mask scan without changing any operator or mixing arithmetic.
template<class RegisterType>
void fm_engine_base<RegisterType>::output_single(output_data &result, uint32_t rshift, int32_t clipmax, uint32_t chnum) const
{
    assert(chnum < CHANNELS);
    uint32_t mask = 1U << chnum;
    if (m_regs.rhythm_enable() || YMFM_DEBUG_LOG_WAVFILES)
    {
        output(result, rshift, clipmax, mask);
        return;
    }
    if (!(mask & debug::GLOBAL_FM_CHANNEL_MASK & m_active_channels))
        return;
    if (m_channel[chnum]->is4op())
        m_channel[chnum]->output_4op(result, rshift, clipmax);
    else
        m_channel[chnum]->output_2op(result, rshift, clipmax);
}

'''
        text=replace_once(text, marker, implementation+marker)
        text=replace_once(text, '\tm_regs.cache_operator_data(m_choffs, m_opoffs, m_cache);',
            '\tm_regs.cache_operator_data(m_choffs, m_opoffs, m_cache);\n'
            '\tm_sor_am=m_regs.op_lfo_am_enable(m_opoffs);\n\tm_sor_ssg=m_regs.op_ssg_eg_enable(m_opoffs);')
        text=replace_once(text, '\tm_keyon_live &= ~(1 << KEYON_CSM);',
            '\tm_keyon_live &= ~(1 << KEYON_CSM);\n\tsor_cache_attenuation();')
        start=text.index('void fm_operator<RegisterType>::clock(uint32_t env_counter')
        end=text.index('//  compute_volume',start)
        part=text[start:end]
        part=part.replace('if (m_regs.op_ssg_eg_enable(m_opoffs))','bool refresh=m_sor_ssg || m_ssg_inverted || bitfield(env_counter, 0, 2)==0;\n\tif (m_sor_ssg)')
        part=part.replace('\t// clock the phase','\tif (refresh) sor_cache_attenuation();\n\n\t// clock the phase')
        text=text[:start]+part+text[end:]
        text=replace_once(text, 'uint32_t env_attenuation = envelope_attenuation(am_offset) << 2;',
            'uint32_t env_attenuation = std::min<uint32_t>(m_sor_attenuation + (m_sor_am ? am_offset : 0), 0x3ff) << 2;')
        text=replace_once(text, 'template<class RegisterType>\nfm_operator<RegisterType>::fm_operator(',
            '''// Construct in static storage before playback: no half-megabyte stack temporary
// and no local-static guard in the per-operator hot path.
struct sor_fm_volume_table {
    uint16_t data[1024*256];
    sor_fm_volume_table() {
        for (unsigned env=0;env<1024;env++) for (unsigned p=0;p<256;p++)
            data[env*256+p]=attenuation_to_volume(abs_sin_attenuation(p)+(env<<2));
    }
};
inline const sor_fm_volume_table sor_fm_volumes;

template<class RegisterType>
fm_operator<RegisterType>::fm_operator(''')
        # Decide the envelope phase once per chip sample rather than 24 times.
        for cls in ('fm_operator','fm_channel'):
            old='template<class RegisterType>\nvoid '+cls+'<RegisterType>::clock(uint32_t env_counter, int32_t lfo_raw_pm)'
            text=replace_once(text,old,'template<class RegisterType>\ntemplate<bool Envelope>\nvoid '+cls+'<RegisterType>::clock_fast(uint32_t env_counter, int32_t lfo_raw_pm)')
        text=replace_once(text,'bool refresh=m_sor_ssg || m_ssg_inverted || bitfield(env_counter, 0, 2)==0;',
            'bool refresh=m_sor_ssg || m_ssg_inverted || Envelope;')
        text=replace_once(text,'if (bitfield(env_counter, 0, 2) == 0)','if constexpr (Envelope)')
        text=replace_once(text,'m_op[opnum]->clock(env_counter, lfo_raw_pm);','m_op[opnum]->template clock_fast<Envelope>(env_counter, lfo_raw_pm);')
        old='''for (uint32_t chnum = 0; chnum < CHANNELS; chnum++)
\t\tif (bitfield(chanmask, chnum))
\t\t\tm_channel[chnum]->clock(m_env_counter, lfo_raw_pm);'''
        text=replace_once(text,old,'''if ((m_env_counter&3)==0) {
        for (uint32_t chnum=0;chnum<CHANNELS;chnum++) if (bitfield(chanmask,chnum))
            m_channel[chnum]->template clock_fast<true>(m_env_counter,lfo_raw_pm);
    } else {
        for (uint32_t chnum=0;chnum<CHANNELS;chnum++) if (bitfield(chanmask,chnum))
            m_channel[chnum]->template clock_fast<false>(m_env_counter,lfo_raw_pm);
    }''')
        # A bounded 512 KiB quarter-wave table trades retail RAM for SH-4 work.
        # It is derived from the pinned chip tables, never from game assets.
        text=replace_once(text, 'int32_t result = attenuation_to_volume((sin_attenuation & 0x7fff) + env_attenuation);',
            '''int32_t result;
    if constexpr (RegisterType::WAVEFORMS == 1 && RegisterType::WAVEFORM_LENGTH == 1024)
    {
        unsigned p=phase&255;if (phase&256) p^=255;
        result=sor_fm_volumes.data[(env_attenuation>>2)*256+p];
    }
    else result = attenuation_to_volume((sin_attenuation & 0x7fff) + env_attenuation);''')
        # Keep this small hot operator calculation inline on SH-4. Arithmetic is unchanged.
        text=replace_once(text, 'int32_t fm_operator<RegisterType>::compute_volume(uint32_t phase, uint32_t am_offset) const',
            'inline __attribute__((always_inline)) int32_t fm_operator<RegisterType>::compute_volume(uint32_t phase, uint32_t am_offset) const')
        return text
    if name == 'ymfm_opn.h':
        marker='class ym2612'
        offset=text.index(marker)
        end=text.index("\n};",offset)
        tail=text[offset:end]
        old='void generate(output_data *output, uint32_t numsamples = 1);'
        tail=replace_once(tail,old,old+'\n\tvoid dac_component(output_data &output);')
        return text[:offset]+tail+text[end:]
    if name == 'ymfm_opn.cpp':
        marker='void ym2612::generate(output_data *output, uint32_t numsamples)'
        component="""// SoR port: expose the DAC stem for synchronized AICA hardware mixing.
void ym2612::dac_component(output_data &output)
{
    output.clear();
    if (!m_dac_enable) return;
    int32_t value = dac_discontinuity(int16_t(m_dac_data << 7) >> 7);
    output.data[0] = m_fm.regs().ch_output_0(0x102) ? value : dac_discontinuity(0);
    output.data[1] = m_fm.regs().ch_output_1(0x102) ? value : dac_discontinuity(0);
    for (int c = 0; c < 2; c++) output.data[c] = (output.data[c] * 128) * 64 / (6 * 65);
}

"""
        text=replace_once(text,marker,component+marker)
        return replace_once(text, 'm_fm.output(temp.clear(), 5, 256, 1 << chan);',
                            'm_fm.output_single(temp.clear(), 5, 256, chan);')
    return text
