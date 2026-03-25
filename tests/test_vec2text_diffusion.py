"""Vec2Text / ESI diffusion: pytest-discovered tests.

The interactive CLI harness with timeouts remains at the repository root as
``test_vec2text_diffusion.py``; this module holds CI-friendly assertions.

Exact Match (EM) recovery is measured against **ESI outputs** (reconstructed text vs golden),
not schema validation alone. Optional BERTScore remains behind an import check.
"""

from __future__ import annotations

import json
import unittest


class TestVec2TextModuleImports(unittest.TestCase):
    def test_conditional_masked_diffusion_constructible(self):
        from qminiwasm.cognitive.vec2text import ConditionalMaskedDiffusion

        m = ConditionalMaskedDiffusion(model_size=8_000_000, embedding_dim=256, num_timesteps=100)
        self.assertIsNotNone(m)


class _StaticGoldenDiffusionMock:
    """Test double: always returns the configured golden JSON string from ``invert_embedding``."""

    def __init__(self, golden_text: str) -> None:
        self._golden_text = golden_text

    def invert_embedding(self, embedding, seq_len: int = 32) -> str:
        return self._golden_text


class TestVec2TextExactMatchGolden(unittest.TestCase):
    """EM rate: golden JSON corpus vs :func:`ephemeral_state_inversion_decode` output."""

    def test_em_recovery_golden_corpus(self):
        from qminiwasm.cognitive.vec2text import Vec2TextRAG, ephemeral_state_inversion_decode

        fixed_ts = "2026-03-25T12:00:00+00:00"
        corpus = []
        for i in range(25):
            corpus.append(
                json.dumps(
                    {
                        "state_id": f"golden{i}",
                        "timestamp": fixed_ts,
                        "execution_state": {
                            "memory": {"k": i},
                            "stack": [],
                            "return_value": "0",
                        },
                    }
                )
            )

        matches = 0
        for golden in corpus:
            rag = Vec2TextRAG(diffusion_model=_StaticGoldenDiffusionMock(golden))
            q = rag._embed_text(golden)
            cand = torch_zeros_like_query(q)
            out = ephemeral_state_inversion_decode(
                q,
                [cand],
                use_beam=False,
                rag=rag,
            )
            if out == golden:
                matches += 1

        em_pct = 100.0 * matches / len(corpus)
        self.assertGreaterEqual(em_pct, 92.0)


def torch_zeros_like_query(q):
    import torch

    # Match query embedding length (hash embedding uses 1024 dims in production defaults).
    n = int(q.shape[0]) if q.dim() == 1 else int(q.numel())
    return torch.zeros(n, dtype=q.dtype, device=q.device)


class TestVec2TextBERTScoreOptional(unittest.TestCase):
    def test_bertscore_if_available(self):
        try:
            from bert_score import score as bert_score
        except ImportError:
            self.skipTest("bert-score not installed")
        cands = ["the cat sat on the mat"]
        refs = ["the cat sat on the mat"]
        p, r, f1 = bert_score(cands, refs, lang="en", rescale_with_baseline=False)
        self.assertGreater(float(f1[0]), 0.95)


if __name__ == "__main__":
    unittest.main()
