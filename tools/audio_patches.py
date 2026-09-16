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
        old='\tvoid output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const;'
        text=replace_once(text,old,old+'\n\ttemplate<int Algorithm> void output_4op_fixed(output_data &output, uint32_t rshift, int32_t clipmax) const;')
        text=replace_once(text,'\tmutable int16_t m_feedback_in;',
            '\tuint8_t m_sor_algorithm=0, m_sor_feedback=0, m_sor_pan=0;\n\tmutable int16_t m_feedback_in;')
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
        text=replace_once(text,'bool fm_channel<RegisterType>::prepare()\n{',
            '''bool fm_channel<RegisterType>::prepare()
{
    m_sor_algorithm=m_regs.ch_algorithm(m_choffs);
    m_sor_feedback=m_regs.ch_feedback(m_choffs);
    m_sor_pan=m_regs.ch_output_any(m_choffs);''')
        marker='template<class RegisterType>\nvoid fm_channel<RegisterType>::output_4op(output_data &output, uint32_t rshift, int32_t clipmax) const'
        wrapper=marker+''' {
    switch(m_sor_algorithm) {
        case 0: output_4op_fixed<0>(output,rshift,clipmax);break;
        case 1: output_4op_fixed<1>(output,rshift,clipmax);break;
        case 2: output_4op_fixed<2>(output,rshift,clipmax);break;
        case 3: output_4op_fixed<3>(output,rshift,clipmax);break;
        case 4: output_4op_fixed<4>(output,rshift,clipmax);break;
        case 5: output_4op_fixed<5>(output,rshift,clipmax);break;
        case 6: output_4op_fixed<6>(output,rshift,clipmax);break;
        case 7: output_4op_fixed<7>(output,rshift,clipmax);break;
        default: output_4op_fixed<-1>(output,rshift,clipmax);break;
    }
}

template<class RegisterType>
template<int Algorithm>
void fm_channel<RegisterType>::output_4op_fixed(output_data &output, uint32_t rshift, int32_t clipmax) const'''
        text=replace_once(text,marker,wrapper)
        a=text.index('void fm_channel<RegisterType>::output_4op_fixed(')
        b=text.index('//  output_rhythm_ch6',a)
        part=text[a:b].replace('m_regs.ch_feedback(m_choffs)','m_sor_feedback').replace('m_regs.ch_output_any(m_choffs)','m_sor_pan')
        part=part.replace('s_algorithm_ops[m_regs.ch_algorithm(m_choffs)]','s_algorithm_ops[Algorithm < 0 ? m_sor_algorithm : Algorithm]')
        text=text[:a]+part+text[b:]
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
