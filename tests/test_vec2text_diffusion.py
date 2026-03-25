"""Vec2Text / ESI diffusion: pytest-discovered tests.

The interactive CLI harness with timeouts remains at the repository root as
``test_vec2text_diffusion.py``; this module holds CI-friendly assertions.
"""

from __future__ import annotations

import unittest


class TestVec2TextModuleImports(unittest.TestCase):
    def test_conditional_masked_diffusion_constructible(self):
        from qminiwasm.inference.vec2text import ConditionalMaskedDiffusion

        m = ConditionalMaskedDiffusion(model_size=8_000_000, embedding_dim=256, num_timesteps=100)
        self.assertIsNotNone(m)


if __name__ == "__main__":
    unittest.main()
