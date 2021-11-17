// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/IR/HALTypes.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace {

// Legalize the type from operand() -> result() for tie_shape op.
// At this level, we preserve any remaining tie_shapes since they may still
// provide information in some contexts.
class ElideTieShapePattern : public OpConversionPattern<Shape::TieShapeOp> {
 public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      Shape::TieShapeOp op, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<Shape::TieShapeOp>(op, adaptor.operand(),
                                                   adaptor.shape());
    return success();
  }
};

struct BufferViewDimPattern : public OpConversionPattern<tensor::DimOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      tensor::DimOp dimOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!adaptor.source().getType().isa<IREE::HAL::BufferViewType>()) {
      return failure();
    }
    Optional<int64_t> index = dimOp.getConstantIndex();
    assert(index.hasValue() && "expect constant index in `std.dim` operation");
    rewriter.replaceOpWithNewOp<IREE::HAL::BufferViewDimOp>(
        dimOp, dimOp.getResult().getType(), adaptor.source(),
        rewriter.getIndexAttr(index.getValue()));
    return success();
  }
};

struct BufferViewRankPattern : public OpConversionPattern<mlir::RankOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(
      mlir::RankOp rankOp, OpAdaptor adaptor,
      ConversionPatternRewriter &rewriter) const override {
    if (!adaptor.memrefOrTensor().getType().isa<IREE::HAL::BufferViewType>()) {
      return failure();
    }
    rewriter.replaceOpWithNewOp<IREE::HAL::BufferViewRankOp>(
        rankOp, rankOp.getResult().getType(), adaptor.memrefOrTensor());
    return success();
  }
};

}  // namespace

void populateStandardShapeToHALPatterns(MLIRContext *context,
                                        ConversionTarget &conversionTarget,
                                        OwningRewritePatternList &patterns,
                                        TypeConverter &typeConverter) {
  // Ensure all shape related ops are fully converted as we should no longer
  // have any types they are valid to be used on after this conversion.
  conversionTarget.addIllegalOp<mlir::RankOp>();
  conversionTarget.addIllegalOp<tensor::DimOp>();

  patterns.insert<BufferViewDimPattern, BufferViewRankPattern,
                  ElideTieShapePattern>(context);
}

}  // namespace iree_compiler
}  // namespace mlir