"""Checked text replacement for the source patches applied to the pinned inputs."""


def replace_once(text, old, new):
    """Replace exactly one occurrence of `old`, or fail: the pinned source has changed."""
    if text.count(old) != 1:
        raise RuntimeError(f'pinned source changed: expected exactly one occurrence of {old[:70]!r}')
    return text.replace(old, new)
