package edgeartifacts

// PACK_ENCODING_VERSION matches qminiwasm.wasm_host.trit_pack.PACK_ENCODING_VERSION (MSB-first, 5 trits/byte).
const PACK_ENCODING_VERSION = 2

const tritsPerByte = 5

var pow3ByK = map[int][]int{
	1: {1},
	2: {3, 1},
	3: {9, 3, 1},
	4: {27, 9, 3, 1},
	5: {81, 27, 9, 3, 1},
}

func signedToDigit(w int) (int, error) {
	switch w {
	case -1:
		return 0, nil
	case 0:
		return 1, nil
	case 1:
		return 2, nil
	default:
		return 0, errInvalidTrit
	}
}

// PackTernaryList packs {-1,0,1} values into bytes (MSB-first per group, k≤5 on last byte).
func PackTernaryList(weights []int) ([]byte, error) {
	n := len(weights)
	out := make([]byte, 0, (n+tritsPerByte-1)/tritsPerByte)
	i := 0
	for i < n {
		k := tritsPerByte
		if n-i < k {
			k = n - i
		}
		powers, ok := pow3ByK[k]
		if !ok {
			return nil, errInvalidTrit
		}
		byteVal := 0
		for j := 0; j < k; j++ {
			d, err := signedToDigit(weights[i+j])
			if err != nil {
				return nil, err
			}
			byteVal += d * powers[j]
		}
		out = append(out, byte(byteVal&0xFF))
		i += k
	}
	return out, nil
}
