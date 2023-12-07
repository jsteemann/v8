// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPMASKS_H_
#define V8_COMPILER_TURBOSHAFT_OPMASKS_H_

#include "src/compiler/turboshaft/operations.h"

// The Opmasks allow performing a type check or cast with an operation mask
// that doesn't only encode the opcode but also additional properties, i.e.
// fields of an operation.
// The type check will be expressed by masking out the first 8 bytes of the
// object based on a generic Opmask and then comparing it against a specific
// shape of that mask.
//
// Given the following operation and mask definitions:
//
//   struct ConvertOp : FixedArityOperationT<1, ConvertOp> {
//     enum Type : int8_t {kBool, kInt, kFloat};
//     Type from;
//     Type to;
//   };
//
//   using ConvertOpMask =
//     MaskBuilder<ConvertOp, FIELD(ConvertOp, from), FIELD(ConvertOp, to)>;
//   using ConvertOpTargetMask = MaskBuilder<ConvertOp, FIELD(ConvertOp, to)>;
//
//   using ConvertFloatToInt =
//     ConvertOpMask::For<ConvertOp::kFloat, ConvertOp::kInt>;
//   using ConvertToInt =
//     ConvertOpTargetMask::For<ConvertOp::kInt>;
//
// The masks can be used in the following way:
//
//    const Operation& my_op = ...;
//    bool is_float_to_int = my_op.Is<ConvertFloatToInt>();
//    const ConvertOp* to_int = my_op.TryCast<ConvertToInt>();
//
// Where to_int will be non-null iff my_op is a ConvertOp *and* the target type
// is int.

namespace v8::internal::compiler::turboshaft::Opmask {

template <typename T, size_t Offset>
struct OpMaskField {
  using type = T;
  static constexpr size_t offset = Offset;
  static constexpr size_t size = sizeof(T);

  static_assert(offset + size <= sizeof(uint64_t));
};

template <typename T>
constexpr uint64_t encode_for_mask(T value) {
  return static_cast<uint64_t>(value);
}

template <typename T>
struct UnwrapRepresentation {
  using type = T;
};
template <>
struct UnwrapRepresentation<WordRepresentation> {
  using type = WordRepresentation::Enum;
};
template <>
struct UnwrapRepresentation<FloatRepresentation> {
  using type = FloatRepresentation::Enum;
};
template <>
struct UnwrapRepresentation<RegisterRepresentation> {
  using type = RegisterRepresentation::Enum;
};

template <typename Op, typename... Fields>
struct MaskBuilder {
  static constexpr uint64_t BuildBaseMask() {
    static_assert(OFFSET_OF(Operation, opcode) == 0);
    static_assert(sizeof(Operation::opcode) == sizeof(uint8_t));
    static_assert(sizeof(Operation) == 4);
    return static_cast<uint64_t>(0xFF);
  }

  static constexpr uint64_t EncodeBaseValue(Opcode opcode) {
    static_assert(OFFSET_OF(Operation, opcode) == 0);
    return static_cast<uint64_t>(opcode);
  }

  static constexpr uint64_t BuildMask() {
    constexpr uint64_t base_mask = BuildBaseMask();
    return (base_mask | ... | BuildFieldMask<Fields>());
  }

  static constexpr uint64_t EncodeValue(typename Fields::type... args) {
    constexpr uint64_t base_mask =
        EncodeBaseValue(operation_to_opcode_map<Op>::value);
    return (base_mask | ... | EncodeFieldValue<Fields>(args));
  }

  template <typename F>
  static constexpr uint64_t BuildFieldMask() {
    static_assert(F::size < sizeof(uint64_t));
    static_assert(F::offset + F::size <= sizeof(uint64_t));
    constexpr uint64_t ones = static_cast<uint64_t>(-1) >>
                              ((sizeof(uint64_t) - F::size) * kBitsPerByte);
    return ones << (F::offset * kBitsPerByte);
  }

  template <typename F>
  static constexpr uint64_t EncodeFieldValue(typename F::type value) {
    return encode_for_mask(value) << (F::offset * kBitsPerByte);
  }

  template <typename Fields::type... Args>
  using For = OpMaskT<Op, BuildMask(), EncodeValue(Args...)>;
};

#define FIELD(op, field_name)                                       \
  OpMaskField<UnwrapRepresentation<decltype(op::field_name)>::type, \
              OFFSET_OF(op, field_name)>

// === Definitions of masks for Turboshaft operations === //

using WordBinopMask =
    MaskBuilder<WordBinopOp, FIELD(WordBinopOp, kind), FIELD(WordBinopOp, rep)>;
using WordBinopKindMask = MaskBuilder<WordBinopOp, FIELD(WordBinopOp, kind)>;

using kWord32Add =
    WordBinopMask::For<WordBinopOp::Kind::kAdd, WordRepresentation::Word32()>;
using kWord32Sub =
    WordBinopMask::For<WordBinopOp::Kind::kSub, WordRepresentation::Word32()>;
using kWord32Mul =
    WordBinopMask::For<WordBinopOp::Kind::kMul, WordRepresentation::Word32()>;
using kWord32BitwiseAnd = WordBinopMask::For<WordBinopOp::Kind::kBitwiseAnd,
                                             WordRepresentation::Word32()>;
using kWord64Add =
    WordBinopMask::For<WordBinopOp::Kind::kAdd, WordRepresentation::Word64()>;
using kWord64Sub =
    WordBinopMask::For<WordBinopOp::Kind::kSub, WordRepresentation::Word64()>;
using kWord64Mul =
    WordBinopMask::For<WordBinopOp::Kind::kMul, WordRepresentation::Word64()>;
using kWord64BitwiseAnd = WordBinopMask::For<WordBinopOp::Kind::kBitwiseAnd,
                                             WordRepresentation::Word64()>;

using kBitwiseAnd = WordBinopKindMask::For<WordBinopOp::Kind::kBitwiseAnd>;
using kBitwiseXor = WordBinopKindMask::For<WordBinopOp::Kind::kBitwiseXor>;

using FloatUnaryMask = MaskBuilder<FloatUnaryOp, FIELD(FloatUnaryOp, kind),
                                   FIELD(FloatUnaryOp, rep)>;

using kFloat64Abs = FloatUnaryMask::For<FloatUnaryOp::Kind::kAbs,
                                        FloatRepresentation::Float64()>;

using ShiftMask =
    MaskBuilder<ShiftOp, FIELD(ShiftOp, kind), FIELD(ShiftOp, rep)>;
using ShiftKindMask = MaskBuilder<ShiftOp, FIELD(ShiftOp, kind)>;

using kWord32ShiftRightArithmetic =
    ShiftMask::For<ShiftOp::Kind::kShiftRightArithmetic,
                   WordRepresentation::Word32()>;
using kWord32ShiftRightLogical =
    ShiftMask::For<ShiftOp::Kind::kShiftRightLogical,
                   WordRepresentation::Word32()>;
using kWord64ShiftRightArithmetic =
    ShiftMask::For<ShiftOp::Kind::kShiftRightArithmetic,
                   WordRepresentation::Word64()>;
using kShiftLeft = ShiftKindMask::For<ShiftOp::Kind::kShiftLeft>;

using ConstantMask = MaskBuilder<ConstantOp, FIELD(ConstantOp, kind)>;

using kWord32Constant = ConstantMask::For<ConstantOp::Kind::kWord32>;
using kWord64Constant = ConstantMask::For<ConstantOp::Kind::kWord64>;
using kExternalConstant = ConstantMask::For<ConstantOp::Kind::kExternal>;

using ProjectionMask = MaskBuilder<ProjectionOp, FIELD(ProjectionOp, index)>;

using kProjection0 = ProjectionMask::For<0>;
using kProjection1 = ProjectionMask::For<1>;

using EqualMask = MaskBuilder<EqualOp, FIELD(EqualOp, rep)>;

using kWord32Equal = EqualMask::For<WordRepresentation::Word32()>;
using kWord64Equal = EqualMask::For<WordRepresentation::Word64()>;

using ChangeOpMask =
    MaskBuilder<ChangeOp, FIELD(ChangeOp, kind), FIELD(ChangeOp, assumption),
                FIELD(ChangeOp, from), FIELD(ChangeOp, to)>;

using kChangeInt32ToInt64 = ChangeOpMask::For<
    ChangeOp::Kind::kSignExtend, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Word32(), RegisterRepresentation::Word64()>;
using kChangeUint32ToUint64 = ChangeOpMask::For<
    ChangeOp::Kind::kZeroExtend, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Word32(), RegisterRepresentation::Word64()>;

#undef FIELD

}  // namespace v8::internal::compiler::turboshaft::Opmask

#endif  // V8_COMPILER_TURBOSHAFT_OPMASKS_H_
