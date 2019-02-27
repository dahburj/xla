#pragma once

#include "torch_xla/csrc/ir.h"

namespace torch_xla {
namespace ir {
namespace ops {

class Narrow : public Node {
 public:
  Narrow(const Value& input, xla::int64 dim, xla::int64 start,
         xla::int64 length);

  std::string ToString() const override;

  XlaOpVector Lower(LoweringContext* loctx) const override;

  xla::int64 dim() const { return dim_; };

  xla::int64 start() const { return start_; };

  xla::int64 length() const { return length_; };

 private:
  xla::int64 dim_;
  xla::int64 start_;
  xla::int64 length_;
};

}  // namespace ops
}  // namespace ir
}  // namespace torch_xla
