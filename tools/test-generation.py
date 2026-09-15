#!/usr/bin/env python3
"""Regression for a manual mid-function entry lost through repartitioning."""
from pathlib import Path
from tempfile import TemporaryDirectory
from generate import verify_sprite_entry

with TemporaryDirectory() as tmp:
    directory=Path(tmp)
    source=directory/'SoR-test.cpp'
    # Valid instruction seeds can still move AE96 out of the manual call's owner.
    source.write_text('''void StreetsOfRage::enqueue_object_render_bucket(m_long entry_) {
    switch(entry_) { case 0xAE72u: break; default: break; }
}
void StreetsOfRage::different_owner(m_long entry_) {
    switch(entry_) { case 0xAE96u: break; default: break; }
}
''')
    try: verify_sprite_entry(directory)
    except RuntimeError: pass
    else: raise AssertionError('Accepted unreachable sprite entry')
    source.write_text('''void StreetsOfRage::enqueue_object_render_bucket(m_long entry_) {
    owner(entry_);
}
void StreetsOfRage::owner(m_long entry_) {
    switch(entry_) { case 0xAE96u: break; default: break; }
}
''')
    verify_sprite_entry(directory)
print('Generation: lost sprite entry rejected; routed alias accepted')
