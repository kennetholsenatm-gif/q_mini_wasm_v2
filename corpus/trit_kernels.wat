;; Ternary pack (MSB-first) and call_indirect dispatch helpers for tests / embedding.
;; Matches qminiwasm.wasm.trit_pack: d' = w + 1 in {0,1,2}, P = sum_j d'_j * 3^(4-j).

(module
  (type $v_v (func (result i32)))
  (type $ii_i (func (param i32 i32) (result i32)))
  (type $iii_i (func (param i32 i32 i32) (result i32)))

  (func $to_digit (param $w i32) (result i32)
    (if (result i32) (i32.lt_s (local.get $w) (i32.const 0))
      (then (i32.const 0))
      (else
        (if (result i32) (i32.eq (local.get $w) (i32.const 0))
          (then (i32.const 1))
          (else (i32.const 2))))))

  (func $pack5_msb
    (param $w0 i32) (param $w1 i32) (param $w2 i32) (param $w3 i32) (param $w4 i32)
    (result i32)
    (i32.add
      (i32.mul (call $to_digit (local.get $w0)) (i32.const 81))
      (i32.add
        (i32.mul (call $to_digit (local.get $w1)) (i32.const 27))
        (i32.add
          (i32.mul (call $to_digit (local.get $w2)) (i32.const 9))
          (i32.add
            (i32.mul (call $to_digit (local.get $w3)) (i32.const 3))
            (call $to_digit (local.get $w4)))))))

  ;; Scalar dot: n, w_ptr, a_ptr -> sum_i digit(w[i]) * (i8 sign-ext a[i]) - offset * n
  ;; Linear memory: w and a are byte offsets; reads i8 from memory.
  (memory 1)
  (func $dot_u8_scalar
    (param $n i32) (param $w_off i32) (param $a_off i32) (param $offset_per_lane i32)
    (result i32)
    (local $i i32) (local $acc i32) (local $wi i32) (local $ai i32)
    (local.set $i (i32.const 0))
    (local.set $acc (i32.const 0))
    (block $done
      (loop $L
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $wi (i32.load8_u (i32.add (local.get $w_off) (local.get $i))))
        (local.set $ai (i32.load8_s (i32.add (local.get $a_off) (local.get $i))))
        (local.set $acc
          (i32.add (local.get $acc)
            (i32.sub (i32.mul (local.get $wi) (local.get $ai)) (local.get $offset_per_lane))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $L)))
    (local.get $acc))

  ;; Table dispatch: 0 = add a+b, 1 = sub a-b, 2 = xor (dummy third kernel)
  (table $kernels 3 funcref)
  (elem (i32.const 0) $k_add $k_sub $k_xor)

  (func $k_add (param i32 i32) (result i32)
    (i32.add (local.get 0) (local.get 1)))
  (func $k_sub (param i32 i32) (result i32)
    (i32.sub (local.get 0) (local.get 1)))
  (func $k_xor (param i32 i32) (result i32)
    (i32.xor (local.get 0) (local.get 1)))

  ;; (kernel_idx, a, b) -> result
  (func $dispatch_binop (param $idx i32) (param $a i32) (param $b i32) (result i32)
    (local.get $a)
    (local.get $b)
    (local.get $idx)
    call_indirect (type $ii_i))

  (export "memory" (memory 0))
  (export "pack5_msb" (func $pack5_msb))
  (export "dot_u8_scalar" (func $dot_u8_scalar))
  (export "dispatch_binop" (func $dispatch_binop))
)
