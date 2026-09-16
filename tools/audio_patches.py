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
    if name == 'ymfm_opn.cpp':
        return replace_once(text, 'm_fm.output(temp.clear(), 5, 256, 1 << chan);',
                            'm_fm.output_single(temp.clear(), 5, 256, chan);')
    return text
