# Ternary State Space

## GF(3) Arithmetic

All computation operates in the Galois Field of order 3.

| Symbol | Value | Modulo-3 Behavior |
|---|---|---|
| `+1` | Positive | 1 mod 3 = 1 |
| `0` | Zero | 0 mod 3 = 0 |
| `-1` | Negative | -1 mod 3 = 2 |

## Trit Encoding

Each trit is a signed 8-bit integer restricted to `{-1, 0, +1}`.

```cpp
enum class Trit : int8_t {
    NEGATIVE = -1,
    ZERO     =  0,
    POSITIVE =  1
};
```

## 5-Trit Packing

Five trits encode 3^5 = 243 states, packing into 8 bits (256 values).

**Entropy efficiency**: 243 / 256 = **99.06%**

## Energy Comparison

| Operation | Energy |
|---|---|
| FP32 multiply | ~3.7 pJ |
| Trit operation | <1 pJ |
| Trit pack/unpack | <0.1 pJ |

## See Also

- [Stabilizer Tableau](stabilizer-tableau.md) — uses GF(3) for gate operations
- [Forward-Forward Learning](forward-forward.md) — ternary goodness metrics