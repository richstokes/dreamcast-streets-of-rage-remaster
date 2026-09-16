"""Small, checked adaptations of the pinned BSD-licensed ymfm source."""
def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned ymfm source changed; review single-channel patch')
    return text.replace(old, new)


def patch(name, text):
    if name == 'ymfm_fm.h':
        old='\tvoid output(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chanmask) const;'
        return replace_once(text, old, old+'\n\tvoid output_single(output_data &output, uint32_t rshift, int32_t clipmax, uint32_t chnum) const;')
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
        return replace_once(text, marker, implementation+marker)
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
